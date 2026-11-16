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
// Created by wangyunlai.wyl on 2021/5/19.
//

#include "storage/index/bplus_tree_index.h"
#include "common/log/log.h"
#include "storage/table/table.h"
#include "storage/db/db.h"

BplusTreeIndex::~BplusTreeIndex() noexcept { close(); }

RC BplusTreeIndex::create(Table *table, const char *file_name, const IndexMeta &index_meta, const FieldMeta &field_meta)
{
  vector<FieldMeta> field_metas;
  field_metas.push_back(field_meta);
  return create(table, file_name, index_meta, field_metas);
}

RC BplusTreeIndex::create(
    Table *table, const char *file_name, const IndexMeta &index_meta, const vector<FieldMeta> &field_metas)
{
  if (inited_) {
    LOG_WARN("Failed to create index due to the index has been created before. file_name:%s, index:%s",
        file_name, index_meta.name());
    return RC::RECORD_OPENNED;
  }
  if (field_metas.empty()) {
    LOG_WARN("Failed to create index due to empty fields. file_name:%s, index:%s", file_name, index_meta.name());
    return RC::INVALID_ARGUMENT;
  }

  Index::init(index_meta, field_metas.front());

  fields_     = field_metas;
  key_length_ = 0;
  for (const FieldMeta &field_meta : fields_) {
    key_length_ += field_meta.len();
  }

  const bool     raw_key  = fields_.size() > 1;
  const AttrType key_type = raw_key ? AttrType::CHARS : fields_.front().type();

  BufferPoolManager &bpm = table->db()->buffer_pool_manager();
  RC rc = index_handler_.create(
      table->db()->log_handler(), bpm, file_name, key_type, key_length_, -1 /*internal_max_size*/, -1 /*leaf_max_size*/, raw_key);
  if (RC::SUCCESS != rc) {
    LOG_WARN("Failed to create index_handler, file_name:%s, index:%s, rc:%s", file_name, index_meta.name(), strrc(rc));
    return rc;
  }

  inited_ = true;
  table_  = table;
  LOG_INFO("Successfully create index, file_name:%s, index:%s", file_name, index_meta.name());
  return RC::SUCCESS;
}

RC BplusTreeIndex::open(Table *table, const char *file_name, const IndexMeta &index_meta, const FieldMeta &field_meta)
{
  vector<FieldMeta> field_metas;
  field_metas.push_back(field_meta);
  return open(table, file_name, index_meta, field_metas);
}

RC BplusTreeIndex::open(
    Table *table, const char *file_name, const IndexMeta &index_meta, const vector<FieldMeta> &field_metas)
{
  if (inited_) {
    LOG_WARN("Failed to open index due to the index has been initedd before. file_name:%s, index:%s",
        file_name, index_meta.name());
    return RC::RECORD_OPENNED;
  }
  if (field_metas.empty()) {
    LOG_WARN("Failed to open index due to empty fields. file_name:%s, index:%s", file_name, index_meta.name());
    return RC::INVALID_ARGUMENT;
  }

  Index::init(index_meta, field_metas.front());

  fields_     = field_metas;
  key_length_ = 0;
  for (const FieldMeta &field_meta : fields_) {
    key_length_ += field_meta.len();
  }

  BufferPoolManager &bpm = table->db()->buffer_pool_manager();
  RC rc                   = index_handler_.open(table->db()->log_handler(), bpm, file_name);
  if (RC::SUCCESS != rc) {
    LOG_WARN("Failed to open index_handler, file_name:%s, index:%s, rc:%s", file_name, index_meta.name(), strrc(rc));
    return rc;
  }

  inited_ = true;
  table_  = table;
  LOG_INFO("Successfully open index, file_name:%s, index:%s", file_name, index_meta.name());
  return RC::SUCCESS;
}

RC BplusTreeIndex::close()
{
  if (inited_) {
    LOG_INFO("Begin to close index, index:%s", index_meta_.name());
    index_handler_.close();
    inited_ = false;
  }
  LOG_INFO("Successfully close index.");
  return RC::SUCCESS;
}

RC BplusTreeIndex::build_key(const char *record, string &key) const
{
  key.assign(key_length_, '\0');

  int offset = 0;
  for (const FieldMeta &field_meta : fields_) {
    memcpy(key.data() + offset, record + field_meta.offset(), field_meta.len());
    offset += field_meta.len();
  }
  return RC::SUCCESS;
}

RC BplusTreeIndex::insert_entry(const char *record, const RID *rid)
{
  if (fields_.size() == 1) {
    return index_handler_.insert_entry(record + fields_[0].offset(), rid);
  }

  string key;
  RC     rc = build_key(record, key);
  if (OB_FAIL(rc)) {
    return rc;
  }
  return index_handler_.insert_entry(key.data(), rid);
}

RC BplusTreeIndex::delete_entry(const char *record, const RID *rid)
{
  if (fields_.size() == 1) {
    return index_handler_.delete_entry(record + fields_[0].offset(), rid);
  }

  string key;
  RC     rc = build_key(record, key);
  if (OB_FAIL(rc)) {
    return rc;
  }
  return index_handler_.delete_entry(key.data(), rid);
}

IndexScanner *BplusTreeIndex::create_scanner(
    const char *left_key, int left_len, bool left_inclusive, const char *right_key, int right_len, bool right_inclusive)
{
  if (fields_.size() != 1) {
    LOG_WARN("multi-column index scan is not supported. index=%s", index_meta_.name());
    return nullptr;
  }

  BplusTreeIndexScanner *index_scanner = new BplusTreeIndexScanner(index_handler_);
  RC rc = index_scanner->open(left_key, left_len, left_inclusive, right_key, right_len, right_inclusive);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to open index scanner. rc=%d:%s", rc, strrc(rc));
    delete index_scanner;
    return nullptr;
  }
  return index_scanner;
}

RC BplusTreeIndex::sync() { return index_handler_.sync(); }

////////////////////////////////////////////////////////////////////////////////
BplusTreeIndexScanner::BplusTreeIndexScanner(BplusTreeHandler &tree_handler) : tree_scanner_(tree_handler) {}

BplusTreeIndexScanner::~BplusTreeIndexScanner() noexcept { tree_scanner_.close(); }

RC BplusTreeIndexScanner::open(
    const char *left_key, int left_len, bool left_inclusive, const char *right_key, int right_len, bool right_inclusive)
{
  return tree_scanner_.open(left_key, left_len, left_inclusive, right_key, right_len, right_inclusive);
}

RC BplusTreeIndexScanner::next_entry(RID *rid) { return tree_scanner_.next_entry(*rid); }

RC BplusTreeIndexScanner::destroy()
{
  delete this;
  return RC::SUCCESS;
}
