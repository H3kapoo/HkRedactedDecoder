#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace hk
{

// #define GetMap(x) std::get<hk::FieldMap>(x)
#define GetMap(x) (std::holds_alternative<hk::FieldMap>(x) ? std::get<hk::FieldMap>(x) : hk::FieldMap{})
#define GetStr(x) (std::holds_alternative<std::string>(x) ? std::get<std::string>(x) : std::string())
#define GetInt(x) (std::holds_alternative<uint64_t>(x) ? std::get<uint64_t>(x) : -1)
#define GetDbl(x) (std::holds_alternative<double>(x) ? std::get<double>(x) : -1)

struct Field;
using FieldMap = std::unordered_map<std::string, Field>;
using StringVec = std::vector<std::string>;
using IntegerVec = std::vector<uint64_t>;
using DoubleVec = std::vector<double>;
using FieldValue = std::variant<uint64_t, double, std::string, StringVec, IntegerVec, DoubleVec, FieldMap>;

struct Field
{
    FieldValue value;
};
} // namespace hk
