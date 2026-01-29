#ifndef SIMPLE_SBC_LOADER_H
#define SIMPLE_SBC_LOADER_H

#include <string>
#include <vector>

#include "sbc_types.h"

namespace simplevm {

LoadResult LoadModuleFromFile(const std::string& path);
LoadResult LoadModuleFromBytes(const std::vector<uint8_t>& bytes);

} // namespace simplevm

#endif // SIMPLE_SBC_LOADER_H
