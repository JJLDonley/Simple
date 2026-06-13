#include "native/registry.h"

#include <cstring>
#include <utility>

#include "native/random.h"

namespace Simple::VM::Native {
namespace {

Slot PackI32(int32_t value) {
  return static_cast<uint32_t>(value);
}

int32_t UnpackI32(Slot value) {
  return static_cast<int32_t>(static_cast<uint32_t>(value));
}

int64_t UnpackI64(Slot value) {
  return static_cast<int64_t>(value);
}

Slot PackF64(double value) {
  uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

NativeCallResult RandomSeed(NativeCallContext& context) {
  Random::Seed(static_cast<uint64_t>(UnpackI64(context.args[0])));
  NativeCallResult result;
  result.has_value = false;
  return result;
}

NativeCallResult RandomI32(NativeCallContext&) {
  NativeCallResult result;
  result.value = PackI32(Random::I32());
  return result;
}

NativeCallResult RandomRange(NativeCallContext& context) {
  NativeCallResult result;
  result.value = PackI32(Random::Range(UnpackI32(context.args[0]), UnpackI32(context.args[1])));
  return result;
}

NativeCallResult RandomF64(NativeCallContext&) {
  NativeCallResult result;
  result.value = PackF64(Random::F64());
  return result;
}

NativeFunctionSpec MakeSpec(const char* module_name, const char* symbol_name,
                            std::vector<Simple::Byte::TypeKind> params,
                            Simple::Byte::TypeKind result_type,
                            NativeFunctionHandler handler) {
  NativeFunctionSpec spec;
  spec.module_name = module_name;
  spec.symbol_name = symbol_name;
  spec.parameter_types = std::move(params);
  spec.result_type = result_type;
  spec.handler = std::move(handler);
  return spec;
}

} // namespace

bool NativeRegistry::Register(NativeFunctionSpec spec) {
  if (spec.module_name.empty() || spec.symbol_name.empty() || !spec.handler) return false;
  if (Find(spec.module_name, spec.symbol_name)) return false;
  functions_.push_back(std::move(spec));
  return true;
}

const NativeFunctionSpec* NativeRegistry::Find(const std::string& module_name,
                                               const std::string& symbol_name) const {
  for (const NativeFunctionSpec& spec : functions_) {
    if (spec.module_name == module_name && spec.symbol_name == symbol_name) return &spec;
  }
  return nullptr;
}

size_t NativeRegistry::Size() const {
  return functions_.size();
}

void RegisterCoreRandom(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  registry.Register(MakeSpec("core.random", "seed", {TypeKind::I64}, TypeKind::Unspecified,
                             RandomSeed));
  registry.Register(MakeSpec("core.random", "i32", {}, TypeKind::I32, RandomI32));
  registry.Register(MakeSpec("core.random", "range", {TypeKind::I32, TypeKind::I32}, TypeKind::I32,
                             RandomRange));
  registry.Register(MakeSpec("core.random", "f64", {}, TypeKind::F64, RandomF64));
}

NativeRegistry BuildDefaultRegistry() {
  NativeRegistry registry;
  RegisterCoreRandom(registry);
  return registry;
}

} // namespace Simple::VM::Native
