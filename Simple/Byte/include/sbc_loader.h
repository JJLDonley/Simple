#ifndef SIMPLE_SBC_LOADER_H
#define SIMPLE_SBC_LOADER_H

#include <string>
#include <vector>

#include "simple_api.h"
#include "sbc_types.h"

namespace Simple::Byte {

SIMPLEVM_API LoadResult LoadModuleFromFile(const std::string& path);
SIMPLEVM_API LoadResult LoadModuleFromBytes(const std::vector<uint8_t>& bytes);

} // namespace Simple::Byte

#endif // SIMPLE_SBC_LOADER_H
