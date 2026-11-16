/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "common/type/date_type.h"

#include <stdio.h>

#include "common/log/log.h"
#include "common/time/datetime.h"
#include "common/value.h"

namespace {

bool parse_number(const char *&ptr, int &value)
{
  if (*ptr < '0' || *ptr > '9') {
    return false;
  }

  value = 0;
  while (*ptr >= '0' && *ptr <= '9') {
    value = value * 10 + (*ptr - '0');
    if (value > 100000) {
      // 位数过多，直接认为非法，避免整数溢出
      return false;
    }
    ++ptr;
  }
  return true;
}

}  // namespace

int DateType::max_day_in_month(int year, int month)
{
  static const int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) {
    return 0;
  }

  if (month == 2) {
    const bool leap_year = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    return leap_year ? 29 : 28;
  }
  return days_in_month[month - 1];
}

RC DateType::to_julian_day(const char *date_str, int &julian_day)
{
  if (date_str == nullptr) {
    return RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }

  const char *ptr   = date_str;
  int         year  = 0;
  int         month = 0;
  int         day   = 0;

  if (!parse_number(ptr, year) || *ptr != '-') {
    return RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }
  ++ptr;
  if (!parse_number(ptr, month) || *ptr != '-') {
    return RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }
  ++ptr;
  if (!parse_number(ptr, day) || *ptr != '\0') {
    return RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }

  // 年份范围与 MiniOB 参考实现保持一致：1 ~ 9999；月份和日期需要严格校验
  if (year < 1 || year > 9999 || month < 1 || month > 12) {
    return RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }
  if (day < 1 || day > max_day_in_month(year, month)) {
    return RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }

  julian_day = common::DateTime::julian_date(year, month, day);
  return RC::SUCCESS;
}

int DateType::compare(const Value &left, const Value &right) const
{
  if (left.attr_type() != AttrType::DATES || right.attr_type() != AttrType::DATES) {
    return INT32_MAX;
  }

  const int left_value  = left.get_int();
  const int right_value = right.get_int();
  if (left_value < right_value) {
    return -1;
  }
  if (left_value > right_value) {
    return 1;
  }
  return 0;
}

RC DateType::cast_to(const Value &val, AttrType type, Value &result) const
{
  switch (type) {
    case AttrType::DATES: {
      if (val.attr_type() == AttrType::DATES) {
        result.set_int(val.get_int());
        result.set_type(AttrType::DATES);
        return RC::SUCCESS;
      }

      if (val.attr_type() == AttrType::CHARS) {
        int  julian_day = 0;
        RC   rc         = to_julian_day(val.get_string().c_str(), julian_day);
        if (OB_FAIL(rc)) {
          return rc;
        }
        result.set_int(julian_day);
        result.set_type(AttrType::DATES);
        return RC::SUCCESS;
      }
      return RC::SCHEMA_FIELD_TYPE_MISMATCH;
    }
    case AttrType::CHARS: {
      string str;
      RC     rc = to_string(val, str);
      if (OB_FAIL(rc)) {
        return rc;
      }
      result.set_string(str.c_str());
      return RC::SUCCESS;
    }
    default: {
      return RC::SCHEMA_FIELD_TYPE_MISMATCH;
    }
  }
}

RC DateType::set_value_from_str(Value &val, const string &data) const
{
  int julian_day = 0;
  RC  rc         = to_julian_day(data.c_str(), julian_day);
  if (OB_FAIL(rc)) {
    return rc;
  }

  val.set_int(julian_day);
  val.set_type(AttrType::DATES);
  return RC::SUCCESS;
}

RC DateType::to_string(const Value &val, string &result) const
{
  if (val.attr_type() != AttrType::DATES) {
    return RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }

  int year  = 0;
  int month = 0;
  int day   = 0;
  common::DateTime::get_ymd(val.get_int(), year, month, day);

  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", year, month, day);
  result.assign(buffer);
  return RC::SUCCESS;
}
