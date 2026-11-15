/* Copyright (c) OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "sql/operator/update_physical_operator.h"
#include "common/log/log.h"
#include "sql/expr/tuple.h"
#include "storage/table/table.h"
#include "storage/trx/trx.h"

RC UpdatePhysicalOperator::open(Trx *trx)
{
  if (children_.empty()) {
    return RC::SUCCESS;
  }

  unique_ptr<PhysicalOperator> &child = children_[0];

  RC rc = child->open(trx);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to open child operator: %s", strrc(rc));
    return rc;
  }

  trx_ = trx;

  // 先收集所有需要更新的记录，避免边扫描边修改
  while (OB_SUCC(rc = child->next())) {
    Tuple *tuple = child->current_tuple();
    if (nullptr == tuple) {
      LOG_WARN("failed to get current tuple");
      child->close();
      return RC::INTERNAL;
    }

    RowTuple *row_tuple = static_cast<RowTuple *>(tuple);
    Record   &record    = row_tuple->record();

    Record record_copy;
    rc = record_copy.copy_data(record.data(), table_->table_meta().record_size());
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to copy record. rc=%s", strrc(rc));
      child->close();
      return rc;
    }
    record_copy.set_rid(record.rid());
    record_copy.set_key(record.key());
    records_.emplace_back(std::move(record_copy));
  }

  child->close();

  if (rc != RC::RECORD_EOF) {
    LOG_WARN("failed to scan records. rc=%s", strrc(rc));
    return rc;
  }

  // 逐条执行更新：删除旧记录（自动清理索引）再插入新记录（自动维护索引）
  for (Record &old_record : records_) {
    Record new_record;
    rc = make_new_record(old_record, new_record);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to make new record. rc=%s", strrc(rc));
      return rc;
    }

    rc = trx_->delete_record(table_, old_record);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to delete old record. rc=%s", strrc(rc));
      return rc;
    }

    rc = trx_->insert_record(table_, new_record);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to insert new record. rc=%s", strrc(rc));
      return rc;
    }
  }

  return RC::SUCCESS;
}

RC UpdatePhysicalOperator::make_new_record(const Record &old_record, Record &new_record)
{
  RC rc = new_record.copy_data(old_record.data(), table_->table_meta().record_size());
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to copy record data. rc=%s", strrc(rc));
    return rc;
  }
  new_record.set_rid(old_record.rid());
  new_record.set_key(old_record.key());

  // 先清空目标字段区域，避免 char 类型残留旧内容
  rc = new_record.reset_filed(field_meta_->offset(), field_meta_->len());
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to reset field. rc=%s", strrc(rc));
    return rc;
  }

  return table_->set_value_to_record(new_record.data(), value_, field_meta_);
}

RC UpdatePhysicalOperator::next() { return RC::RECORD_EOF; }

RC UpdatePhysicalOperator::close() { return RC::SUCCESS; }
