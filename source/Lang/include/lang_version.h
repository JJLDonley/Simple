#ifndef SIMPLE_LANG_VERSION_H
#define SIMPLE_LANG_VERSION_H

#include <cstdint>

#include "ir_lang.h"

namespace Simple::Lang {

constexpr uint16_t kLangSyntaxVersionMajor = 4;
constexpr uint16_t kLangSyntaxVersionMinor = 0;
constexpr uint16_t kSirVersionMajor = Simple::IR::Text::kSirVersionMajor;
constexpr uint16_t kSirVersionMinor = Simple::IR::Text::kSirVersionMinor;
constexpr uint16_t kStdlibVersionMajor = 2;
constexpr uint16_t kStdlibVersionMinor = 0;

} // namespace Simple::Lang

#endif // SIMPLE_LANG_VERSION_H
