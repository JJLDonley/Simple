#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Simple::Lang {

struct TypeDim {
  bool is_list = false;
  bool has_size = false;
  uint64_t size = 0;
};

struct TypeRef {
  std::string name;
  std::vector<TypeRef> type_args;
  std::vector<TypeDim> dims;
};

} // namespace Simple::Lang
