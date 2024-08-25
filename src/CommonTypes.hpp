#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace hk
{

// #define GetMap(x) std::get<hk::FieldMap>(x)
#define GET_MAP(x) std::get<hk::FieldMap>(x)
#define GET_STR(x) (std::holds_alternative<std::string>(x) ? std::get<std::string>(x) : std::string())
#define GET_INT(x) (std::holds_alternative<uint64_t>(x) ? std::get<uint64_t>(x) : -1)
#define GET_DBL(x) (std::holds_alternative<double>(x) ? std::get<double>(x) : -1)
#define HOLDS_MAP(x) (std::holds_alternative<hk::FieldMap>(x) ? true : false)
#define HAS_FIELD(map, fieldName) map.contains(fieldName) ? true : false

class FieldMap;

using StringVec = std::vector<std::string>;
using IntegerVec = std::vector<uint64_t>;
using DoubleVec = std::vector<double>;
using FieldValue = std::variant<uint64_t, double, std::string, StringVec, IntegerVec, DoubleVec, FieldMap>;

class FieldMap : public std::unordered_map<std::string, FieldValue>
{};

} // namespace hk
