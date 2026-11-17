/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

//
// Created by Wangyunlai on 2022/5/22.
//

#include "sql/stmt/filter_stmt.h"
#include "common/lang/string.h"
#include "common/lang/utility.h"
#include "common/log/log.h"
#include "common/sys/rc.h"
#include "session/session.h"
#include "sql/expr/tuple.h"
#include "sql/optimizer/logical_plan_generator.h"
#include "sql/optimizer/physical_plan_generator.h"
#include "sql/stmt/select_stmt.h"
#include "storage/db/db.h"
#include "storage/table/table.h"
#include "storage/trx/trx.h"

static RC execute_subquery(Db *db, const shared_ptr<SelectSqlNode> &subquery_sql, vector<Value> &values)
{
  values.clear();
  if (nullptr == db || nullptr == subquery_sql) {
    return RC::INVALID_ARGUMENT;
  }

  Session *session = Session::current_session();
  if (nullptr == session) {
    LOG_WARN("no current session for sub query");
    return RC::INTERNAL;
  }

  Stmt *child_stmt = nullptr;
  RC    rc         = SelectStmt::create(db, *subquery_sql, child_stmt);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to create sub query statement. rc=%s", strrc(rc));
    return rc;
  }
  unique_ptr<Stmt> child_stmt_guard(child_stmt);

  SelectStmt *select_stmt = static_cast<SelectStmt *>(child_stmt);
  if (select_stmt->query_expressions().size() != 1) {
    LOG_WARN("sub query must return exactly one column, got %d", static_cast<int>(select_stmt->query_expressions().size()));
    return RC::INVALID_ARGUMENT;
  }

  LogicalPlanGenerator  logical_gen;
  unique_ptr<LogicalOperator> logical_oper;
  rc = logical_gen.create(child_stmt, logical_oper);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to create sub query logical plan. rc=%s", strrc(rc));
    return rc;
  }

  PhysicalPlanGenerator physical_gen;
  unique_ptr<PhysicalOperator> physical_oper;
  rc = physical_gen.create(*logical_oper, physical_oper, session);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to create sub query physical plan. rc=%s", strrc(rc));
    return rc;
  }

  Trx *trx = session->current_trx();
  if (nullptr == trx) {
    LOG_WARN("no current transaction for sub query");
    return RC::INTERNAL;
  }
  rc = trx->start_if_need();
  if (rc != RC::SUCCESS) {
    return rc;
  }

  rc = physical_oper->open(trx);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to open sub query physical operator. rc=%s", strrc(rc));
    return rc;
  }

  TupleSchema schema;
  rc = physical_oper->tuple_schema(schema);
  if (rc != RC::SUCCESS || schema.cell_num() != 1) {
    LOG_WARN("sub query must return exactly one column, schema cell num=%d", schema.cell_num());
    physical_oper->close();
    return RC::INVALID_ARGUMENT;
  }

  while (RC::SUCCESS == (rc = physical_oper->next())) {
    Tuple *tuple = physical_oper->current_tuple();
    if (nullptr == tuple) {
      physical_oper->close();
      return RC::INTERNAL;
    }
    Value value;
    rc = tuple->cell_at(0, value);
    if (rc != RC::SUCCESS) {
      physical_oper->close();
      return rc;
    }
    values.emplace_back(std::move(value));
  }
  physical_oper->close();
  if (rc != RC::RECORD_EOF) {
    return rc;
  }
  return RC::SUCCESS;
}

FilterStmt::~FilterStmt()
{
  for (vector<FilterUnit *> &filter_group : filter_groups_) {
    for (FilterUnit *unit : filter_group) {
      delete unit;
    }
  }
  filter_groups_.clear();
}

RC FilterStmt::create(Db *db, Table *default_table, unordered_map<string, Table *> *tables,
    const WhereSqlNode &where, FilterStmt *&stmt)
{
  RC rc = RC::SUCCESS;
  stmt  = nullptr;

  FilterStmt *tmp_stmt = new FilterStmt();
  for (const vector<ConditionSqlNode> &condition_group : where.groups) {
    vector<FilterUnit *> filter_group;
    for (const ConditionSqlNode &condition : condition_group) {
      FilterUnit *filter_unit = nullptr;

      rc = create_filter_unit(db, default_table, tables, condition, filter_unit);
      if (rc != RC::SUCCESS) {
        delete filter_unit;
        for (FilterUnit *unit : filter_group) {
          delete unit;
        }
        delete tmp_stmt;
        LOG_WARN("failed to create filter unit");
        return rc;
      }
      filter_group.push_back(filter_unit);
    }
    tmp_stmt->filter_groups_.push_back(std::move(filter_group));
  }

  stmt = tmp_stmt;
  return rc;
}

RC get_table_and_field(Db *db, Table *default_table, unordered_map<string, Table *> *tables,
    const RelAttrSqlNode &attr, Table *&table, const FieldMeta *&field)
{
  if (common::is_blank(attr.relation_name.c_str())) {
    table = default_table;
  } else if (nullptr != tables) {
    auto iter = tables->find(attr.relation_name);
    if (iter != tables->end()) {
      table = iter->second;
    }
  } else {
    table = db->find_table(attr.relation_name.c_str());
  }
  if (nullptr == table) {
    LOG_WARN("No such table: attr.relation_name: %s", attr.relation_name.c_str());
    return RC::SCHEMA_TABLE_NOT_EXIST;
  }

  field = table->table_meta().field(attr.attribute_name.c_str());
  if (nullptr == field) {
    LOG_WARN("no such field in table: table %s, field %s", table->name(), attr.attribute_name.c_str());
    table = nullptr;
    return RC::SCHEMA_FIELD_NOT_EXIST;
  }

  return RC::SUCCESS;
}

RC FilterStmt::create_filter_unit(Db *db, Table *default_table, unordered_map<string, Table *> *tables,
    const ConditionSqlNode &condition, FilterUnit *&filter_unit)
{
  RC rc = RC::SUCCESS;

  CompOp comp = condition.comp;
  if (comp < EQUAL_TO || comp >= NO_OP) {
    LOG_WARN("invalid compare operator : %d", comp);
    return RC::INVALID_ARGUMENT;
  }

  filter_unit = new FilterUnit;

  if (condition.left_is_attr == 1) {
    Table           *table = nullptr;
    const FieldMeta *field = nullptr;
    rc                     = get_table_and_field(db, default_table, tables, condition.left_attr, table, field);
    if (rc != RC::SUCCESS) {
      LOG_WARN("cannot find attr");
      delete filter_unit;
      filter_unit = nullptr;
      return rc;
    }
    FilterObj filter_obj;
    filter_obj.init_attr(Field(table, field));
    filter_unit->set_left(filter_obj);
  } else if (condition.left_is_attr == 0) {
    FilterObj filter_obj;
    filter_obj.init_value(condition.left_value);
    filter_unit->set_left(filter_obj);
  } else if (condition.left_is_attr == 2 && condition.left_subquery != nullptr) {
    vector<Value> values;
    rc = execute_subquery(db, condition.left_subquery, values);
    if (rc != RC::SUCCESS) {
      delete filter_unit;
      filter_unit = nullptr;
      return rc;
    }
    Value value;
    if (values.empty()) {
      value.set_null();
    } else if (values.size() == 1) {
      value = values[0];
    } else {
      delete filter_unit;
      filter_unit = nullptr;
      return RC::INVALID_ARGUMENT;
    }
    FilterObj filter_obj;
    filter_obj.init_value(value);
    filter_unit->set_left(filter_obj);
  } else {
    delete filter_unit;
    filter_unit = nullptr;
    return RC::INVALID_ARGUMENT;
  }

  if (condition.right_is_attr == 1) {
    Table           *table = nullptr;
    const FieldMeta *field = nullptr;
    rc                     = get_table_and_field(db, default_table, tables, condition.right_attr, table, field);
    if (rc != RC::SUCCESS) {
      LOG_WARN("cannot find attr");
      delete filter_unit;
      filter_unit = nullptr;
      return rc;
    }
    FilterObj filter_obj;
    filter_obj.init_attr(Field(table, field));
    filter_unit->set_right(filter_obj);
  } else if (condition.right_is_attr == 0) {
    FilterObj filter_obj;
    filter_obj.init_value(condition.right_value);
    filter_unit->set_right(filter_obj);
  } else if (condition.right_is_attr == 3) {
    if (comp != IN_OP && comp != NOT_IN) {
      delete filter_unit;
      filter_unit = nullptr;
      return RC::INVALID_ARGUMENT;
    }
    filter_unit->set_in_values(vector<Value>(condition.right_values), comp == NOT_IN);
  } else if (condition.right_is_attr == 2 && condition.right_subquery != nullptr) {
    vector<Value> values;
    rc = execute_subquery(db, condition.right_subquery, values);
    if (rc != RC::SUCCESS) {
      delete filter_unit;
      filter_unit = nullptr;
      return rc;
    }

    if (comp == IN_OP || comp == NOT_IN) {
      filter_unit->set_in_values(std::move(values), comp == NOT_IN);
    } else {
      Value value;
      if (values.empty()) {
        value.set_null();
      } else if (values.size() == 1) {
        value = values[0];
      } else {
        delete filter_unit;
        filter_unit = nullptr;
        return RC::INVALID_ARGUMENT;
      }
      FilterObj filter_obj;
      filter_obj.init_value(value);
      filter_unit->set_right(filter_obj);
    }
  } else {
    delete filter_unit;
    filter_unit = nullptr;
    return RC::INVALID_ARGUMENT;
  }

  if (filter_unit->is_in()) {
    AttrType target_type = AttrType::UNDEFINED;
    if (filter_unit->left().is_attr) {
      target_type = filter_unit->left().field.attr_type();
    } else {
      target_type = filter_unit->left().value.attr_type();
    }
    vector<Value> &values = filter_unit->in_values();
    for (Value &value : values) {
      if (value.is_null() || target_type == AttrType::UNDEFINED || value.attr_type() == target_type) {
        continue;
      }
      Value cast_value;
      if (RC::SUCCESS == Value::cast_to(value, target_type, cast_value)) {
        value = std::move(cast_value);
      }
    }
  }

  filter_unit->set_comp(comp);

  // 检查两个类型是否能够比较
  return rc;
}
