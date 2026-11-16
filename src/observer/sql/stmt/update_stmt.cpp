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

#include "sql/stmt/update_stmt.h"
#include "common/log/log.h"
#include "sql/stmt/filter_stmt.h"
#include "storage/db/db.h"
#include "storage/table/table.h"

UpdateStmt::UpdateStmt(Table *table, const FieldMeta *field_meta, const Value &value, FilterStmt *filter_stmt)
    : table_(table), field_meta_(field_meta), value_(value), filter_stmt_(filter_stmt)
{}

UpdateStmt::~UpdateStmt()
{
  if (nullptr != filter_stmt_) {
    delete filter_stmt_;
    filter_stmt_ = nullptr;
  }
}

RC UpdateStmt::create(Db *db, const UpdateSqlNode &update, Stmt *&stmt)
{
  stmt                   = nullptr;
  const char *table_name = update.relation_name.c_str();
  const char *field_name = update.attribute_name.c_str();
  if (nullptr == db || nullptr == table_name || nullptr == field_name) {
    LOG_WARN("invalid argument. db=%p, table_name=%p, field_name=%p", db, table_name, field_name);
    return RC::INVALID_ARGUMENT;
  }

  // 检查表是否存在
  Table *table = db->find_table(table_name);
  if (nullptr == table) {
    LOG_WARN("no such table. db=%s, table_name=%s", db->name(), table_name);
    return RC::SCHEMA_TABLE_NOT_EXIST;
  }

  // 检查字段是否存在，且不能更新系统隐藏字段（比如事务字段）
  const TableMeta &table_meta = table->table_meta();
  const FieldMeta *field_meta = table_meta.field(field_name);
  if (nullptr == field_meta || !field_meta->visible()) {
    LOG_WARN("no such field. table=%s, field=%s", table_name, field_name);
    return RC::SCHEMA_FIELD_NOT_EXIST;
  }

  // 更新值的类型需要与字段类型兼容，比如 char 类型的值不能赋给 int 字段
  Value value = update.value;
  if (value.attr_type() != field_meta->type()) {
    Value real_value;
    RC    rc = Value::cast_to(value, field_meta->type(), real_value);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to cast value. table=%s, field=%s, value=%s",
          table_name, field_name, value.to_string().c_str());
      return RC::SCHEMA_FIELD_TYPE_MISMATCH;
    }
    value = real_value;
  }

  // 解析 where 条件
  unordered_map<string, Table *> table_map;
  table_map.insert(pair<string, Table *>(string(table_name), table));

  FilterStmt *filter_stmt = nullptr;
  RC          rc          = FilterStmt::create(db, table, &table_map, update.conditions, filter_stmt);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to create filter statement. rc=%d:%s", rc, strrc(rc));
    return rc;
  }

  stmt = new UpdateStmt(table, field_meta, value, filter_stmt);
  return RC::SUCCESS;
}
