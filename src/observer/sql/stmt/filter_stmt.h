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

#pragma once

#include <utility>

#include "common/lang/unordered_map.h"
#include "common/lang/vector.h"
#include "sql/expr/expression.h"
#include "sql/parser/parse_defs.h"
#include "sql/stmt/stmt.h"

class Db;
class Table;
class FieldMeta;

struct FilterObj
{
  bool  is_attr = false;
  Field field;
  Value value;

  void init_attr(const Field &field)
  {
    is_attr     = true;
    this->field = field;
  }

  void init_value(const Value &value)
  {
    is_attr     = false;
    this->value = value;
  }
};

class FilterUnit
{
public:
  FilterUnit() = default;
  ~FilterUnit() {}

  void set_comp(CompOp comp) { comp_ = comp; }

  CompOp comp() const { return comp_; }

  void set_left(const FilterObj &obj) { left_ = obj; }
  void set_right(const FilterObj &obj) { right_ = obj; }

  const FilterObj &left() const { return left_; }
  const FilterObj &right() const { return right_; }

  void set_in_values(vector<Value> &&values, bool is_not_in)
  {
    in_values_  = std::move(values);
    is_in_      = true;
    is_not_in_  = is_not_in;
  }
  bool is_in() const { return is_in_; }
  bool is_not_in() const { return is_not_in_; }
  const vector<Value> &in_values() const { return in_values_; }
  vector<Value>       &in_values() { return in_values_; }

private:
  CompOp         comp_ = NO_OP;
  FilterObj      left_;
  FilterObj      right_;
  bool           is_in_     = false;
  bool           is_not_in_ = false;
  vector<Value>  in_values_;
};

/**
 * @brief Filter/谓词/过滤语句
 * @ingroup Statement
 */
class FilterStmt
{
public:
  FilterStmt() = default;
  virtual ~FilterStmt();

public:
  const vector<vector<FilterUnit *>> &filter_groups() const { return filter_groups_; }

public:
  static RC create(
      Db *db, Table *default_table, unordered_map<string, Table *> *tables, const WhereSqlNode &where, FilterStmt *&stmt);

  static RC create_filter_unit(Db *db, Table *default_table, unordered_map<string, Table *> *tables,
      const ConditionSqlNode &condition, FilterUnit *&filter_unit);

private:
  vector<vector<FilterUnit *>> filter_groups_;  ///< 外层 OR，内层 AND
};
