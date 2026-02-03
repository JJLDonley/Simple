#ifndef SIMPLE_SBC_LOADER_H
#define SIMPLE_SBC_LOADER_H

#include <string>
#include <vector>

#include "sbc_types.h"

namespace Simple::Byte {

LoadResult LoadModuleFromFile(const std::string& path);
LoadResult LoadModuleFromBytes(const std::vector<uint8_t>& bytes);

} // namespace Simple::Byte

#endif // SIMPLE_SBC_LOADER_H
