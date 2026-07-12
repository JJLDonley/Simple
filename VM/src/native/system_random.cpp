#include "native/registry.h"

#include "native/buffer.h"
#include "native/random.h"
#include "native/spec_builder.h"

namespace Simple::VM::Native {
namespace {

NativeCallResult RandomSeed(NativeCallContext& context) {
  int64_t seed = 0;
  if (!context.ArgI64(0, &seed)) return NativeCallResult::Error("System.Random.seed missing seed");
  Random::Seed(static_cast<uint64_t>(seed));
  return NativeCallResult::Void();
}

NativeCallResult RandomI32(NativeCallContext&) {
  return NativeCallResult::I32(Random::I32());
}

NativeCallResult RandomI64(NativeCallContext&) {
  return NativeCallResult::I64(Random::I64());
}

NativeCallResult RandomFillBytes(NativeCallContext& context) {
  uint32_t ref = 0;
  HeapObject* obj = context.heap && context.ArgRef(0, &ref) ? context.heap->Get(ref) : nullptr;
  if (!Buffer::IsBuffer(obj)) return NativeCallResult::I32(0);
  std::vector<uint8_t> bytes(Buffer::Len(obj));
  Random::FillBytes(&bytes);
  for (uint32_t i = 0; i < bytes.size(); ++i) Buffer::WriteLE(obj, i, 1u, bytes[i]);
  return NativeCallResult::I32(1);
}

NativeCallResult RandomRange(NativeCallContext& context) {
  int32_t min_value = 0;
  int32_t max_value = 0;
  if (!context.ArgI32(0, &min_value) || !context.ArgI32(1, &max_value)) {
    return NativeCallResult::Error("System.Random.range missing bounds");
  }
  return NativeCallResult::I32(Random::Range(min_value, max_value));
}

NativeCallResult RandomF64(NativeCallContext&) {
  return NativeCallResult::F64(Random::F64());
}

} // namespace

void RegisterSystemRandom(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  const auto module = Simple::Lang::ToLibraryModuleId(Simple::Lang::SystemModule::Random);
  registry.Register(WithCapability(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemRandomMember::Seed), {TypeKind::I64},
                                            TypeKind::Unspecified, RandomSeed),
                                   "randomness"));
  registry.Register(WithCapability(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemRandomMember::I32), {}, TypeKind::I32, RandomI32),
                                   "randomness"));
  registry.Register(WithCapability(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemRandomMember::I64), {}, TypeKind::I64, RandomI64),
                                   "randomness"));
  registry.Register(WithCapability(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemRandomMember::FillBytes), {TypeKind::Ref},
                                            TypeKind::I32, RandomFillBytes),
                                   "randomness"));
  registry.Register(WithCapability(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemRandomMember::Range), {TypeKind::I32, TypeKind::I32},
                                            TypeKind::I32, RandomRange),
                                   "randomness"));
  registry.Register(WithCapability(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemRandomMember::F64), {}, TypeKind::F64, RandomF64),
                                   "randomness"));
}

} // namespace Simple::VM::Native
