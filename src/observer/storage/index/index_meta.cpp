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
// Created by Wangyunlai.wyl on 2021/5/18.
//

#include "storage/index/index_meta.h"
#include "common/lang/string.h"
#include "common/lang/utility.h"
#include "common/log/log.h"
#include "storage/field/field_meta.h"
#include "storage/table/table_meta.h"
#include "json/json.h"

const static Json::StaticString FIELD_NAME("name");
const static Json::StaticString FIELD_FIELD_NAME("field_name");
const static Json::StaticString FIELD_FIELDS("fields");

RC IndexMeta::init(const char *name, const vector<string> &field_names)
{
  if (common::is_blank(name)) {
    LOG_ERROR("Failed to init index, name is empty.");
    return RC::INVALID_ARGUMENT;
  }
  if (field_names.empty()) {
    LOG_ERROR("Failed to init index, fields are empty. index=%s", name);
    return RC::INVALID_ARGUMENT;
  }

  for (const string &field_name : field_names) {
    if (common::is_blank(field_name.c_str())) {
      LOG_ERROR("Failed to init index, field name is empty. index=%s", name);
      return RC::INVALID_ARGUMENT;
    }
  }

  name_   = name;
  fields_ = field_names;
  return RC::SUCCESS;
}

RC IndexMeta::init(const char *name, const FieldMeta &field)
{
  vector<string> field_names;
  field_names.emplace_back(field.name());
  return init(name, field_names);
}

void IndexMeta::to_json(Json::Value &json_value) const
{
  json_value[FIELD_NAME] = name_;

  Json::Value fields_value;
  for (const string &field : fields_) {
    fields_value.append(field);
  }
  json_value[FIELD_FIELDS] = std::move(fields_value);
}

RC IndexMeta::from_json(const TableMeta &table, const Json::Value &json_value, IndexMeta &index)
{
  const Json::Value &name_value = json_value[FIELD_NAME];
  if (!name_value.isString()) {
    LOG_ERROR("Index name is not a string. json value=%s", name_value.toStyledString().c_str());
    return RC::INTERNAL;
  }

  vector<string> field_names;
  const Json::Value &fields_value = json_value[FIELD_FIELDS];
  if (fields_value.isArray() && !fields_value.empty()) {
    for (const Json::Value &field_value : fields_value) {
      if (!field_value.isString()) {
        LOG_ERROR("Field name of index [%s] is not a string. json value=%s",
            name_value.asCString(), field_value.toStyledString().c_str());
        return RC::INTERNAL;
      }
      field_names.emplace_back(field_value.asCString());
    }
  } else {
    // 兼容旧的单字段索引元数据格式
    const Json::Value &field_value = json_value[FIELD_FIELD_NAME];
    if (!field_value.isString()) {
      LOG_ERROR("Field name of index [%s] is not a string. json value=%s",
          name_value.asCString(), field_value.toStyledString().c_str());
      return RC::INTERNAL;
    }
    field_names.emplace_back(field_value.asCString());
  }

  for (const string &field_name : field_names) {
    const FieldMeta *field = table.field(field_name.c_str());
    if (nullptr == field) {
      LOG_ERROR("Deserialize index [%s]: no such field: %s", name_value.asCString(), field_name.c_str());
      return RC::SCHEMA_FIELD_MISSING;
    }
  }

  return index.init(name_value.asCString(), field_names);
}

const char *IndexMeta::name() const { return name_.c_str(); }

const char *IndexMeta::field() const
{
  if (fields_.empty()) {
    return "";
  }
  return fields_[0].c_str();
}

void IndexMeta::desc(ostream &os) const
{
  os << "index name=" << name_ << ", fields=";
  for (size_t i = 0; i < fields_.size(); i++) {
    if (i != 0) {
      os << ",";
    }
    os << fields_[i];
  }
}
