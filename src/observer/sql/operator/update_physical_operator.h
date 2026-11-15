/* Copyright (c) OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once

#include "common/value.h"
#include "sql/operator/physical_operator.h"
#include "storage/record/record.h"

class Table;
class Trx;
class FieldMeta;

/**
 * @brief 物理算子，更新
 * @ingroup PhysicalOperator
 * @details 先收集所有满足条件的记录，再对每条记录执行“删除旧记录 + 插入新记录”。
 *          索引的维护由存储引擎在插入/删除时自动完成。
 */
class UpdatePhysicalOperator : public PhysicalOperator
{
public:
  UpdatePhysicalOperator(Table *table, const FieldMeta *field_meta, const Value &value)
      : table_(table), field_meta_(field_meta), value_(value)
  {}

  virtual ~UpdatePhysicalOperator() = default;

  PhysicalOperatorType type() const override { return PhysicalOperatorType::UPDATE; }

  OpType get_op_type() const override { return OpType::UPDATE; }

  RC open(Trx *trx) override;
  RC next() override;
  RC close() override;

  Tuple *current_tuple() override { return nullptr; }

private:
  RC make_new_record(const Record &old_record, Record &new_record);

private:
  Table           *table_      = nullptr;
  const FieldMeta *field_meta_ = nullptr;
  Value            value_;
  Trx             *trx_ = nullptr;

  vector<Record> records_;
};
