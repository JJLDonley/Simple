#include "runtime/print_any.h"

#include <cstdio>
#include <string>

#include "intrinsic_ids.h"
#include "runtime/values.h"

namespace Simple::VM::Runtime {
namespace {

void WriteStdoutText(const std::string& text) {
  if (!text.empty()) std::fwrite(text.data(), 1, text.size(), stdout);
}

} // namespace

bool PrintAny(Heap& heap, uint32_t tag, Simple::VM::Interpreter::Slot value, std::string* out_error) {
  switch (tag) {
    case kPrintAnyTagString: {
      uint32_t ref = UnpackRef(value);
      HeapObject* obj = heap.Get(ref);
      if (!obj || obj->header.kind != ObjectKind::String) {
        if (out_error) *out_error = "print_any: unsupported ref kind";
        return false;
      }
      WriteStdoutText(U16ToAscii(ReadString(obj)));
      return true;
    }
    case kPrintAnyTagI8:
      WriteStdoutText(std::to_string(static_cast<int32_t>(static_cast<int8_t>(UnpackI32(value)))));
      return true;
    case kPrintAnyTagI16:
      WriteStdoutText(std::to_string(static_cast<int32_t>(static_cast<int16_t>(UnpackI32(value)))));
      return true;
    case kPrintAnyTagI32:
      WriteStdoutText(std::to_string(static_cast<int32_t>(UnpackI32(value))));
      return true;
    case kPrintAnyTagI64:
      WriteStdoutText(std::to_string(static_cast<int64_t>(UnpackI64(value))));
      return true;
    case kPrintAnyTagU8:
      WriteStdoutText(std::to_string(static_cast<uint32_t>(static_cast<uint8_t>(UnpackI32(value)))));
      return true;
    case kPrintAnyTagU16:
      WriteStdoutText(std::to_string(static_cast<uint32_t>(static_cast<uint16_t>(UnpackI32(value)))));
      return true;
    case kPrintAnyTagU32:
      WriteStdoutText(std::to_string(static_cast<uint32_t>(UnpackI32(value))));
      return true;
    case kPrintAnyTagU64:
      WriteStdoutText(std::to_string(static_cast<uint64_t>(UnpackI64(value))));
      return true;
    case kPrintAnyTagF32: {
      float v = BitsToF32(UnpackU32Bits(value));
      WriteStdoutText(std::to_string(v));
      return true;
    }
    case kPrintAnyTagF64: {
      double v = BitsToF64(UnpackU64Bits(value));
      WriteStdoutText(std::to_string(v));
      return true;
    }
    case kPrintAnyTagBool:
      WriteStdoutText((UnpackI32(value) != 0) ? "true" : "false");
      return true;
    case kPrintAnyTagChar: {
      uint32_t ch = static_cast<uint32_t>(UnpackI32(value)) & 0xFFu;
      char c = static_cast<char>(ch);
      std::fwrite(&c, 1, 1, stdout);
      return true;
    }
    default:
      if (out_error) *out_error = "print_any: unknown tag";
      return false;
  }
}

} // namespace Simple::VM::Runtime
