#ifndef SIMPLE_VM_NATIVE_JSON_H
#define SIMPLE_VM_NATIVE_JSON_H

#include <optional>
#include <string>

namespace Simple::VM::Native::Json {

struct Document {
  std::string text;
};

std::optional<Document> Parse(const std::string& text);
bool Stringify(const Document& document, std::string* out);
bool IsValidText(const std::string& text);

} // namespace Simple::VM::Native::Json

#endif // SIMPLE_VM_NATIVE_JSON_H
