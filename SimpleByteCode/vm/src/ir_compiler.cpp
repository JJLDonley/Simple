#include "ir_compiler.h"

#include <cstring>

#include "sbc_emitter.h"
#include "sbc_types.h"

namespace simplevm::ir {
namespace {

void AppendDefaultI32Type(std::vector<uint8_t>* types) {
  simplevm::sbc::AppendU32(*types, 0); // name_str
  simplevm::sbc::AppendU8(*types, static_cast<uint8_t>(simplevm::TypeKind::I32));
  simplevm::sbc::AppendU8(*types, 0);  // flags
  simplevm::sbc::AppendU16(*types, 0); // reserved
  simplevm::sbc::AppendU32(*types, 4); // size
  simplevm::sbc::AppendU32(*types, 0); // field_start
  simplevm::sbc::AppendU32(*types, 0); // field_count
}

void BuildSigTable(const std::vector<simplevm::sbc::SigSpec>& sig_specs,
                   std::vector<uint8_t>* out_sigs) {
  std::vector<uint32_t> param_types;
  for (const auto& spec : sig_specs) {
    uint32_t param_type_start = static_cast<uint32_t>(param_types.size());
    simplevm::sbc::AppendU32(*out_sigs, spec.ret_type_id);
    simplevm::sbc::AppendU16(*out_sigs, spec.param_count);
    simplevm::sbc::AppendU16(*out_sigs, 0); // call_conv
    simplevm::sbc::AppendU32(*out_sigs, param_type_start);
    for (uint32_t type_id : spec.param_types) {
      param_types.push_back(type_id);
    }
  }
  for (uint32_t type_id : param_types) {
    simplevm::sbc::AppendU32(*out_sigs, type_id);
  }
}

} // namespace

bool CompileToSbc(const IrModule& module, std::vector<uint8_t>* out, std::string* error) {
  if (!out) {
    if (error) *error = "output buffer is null";
    return false;
  }
  if (module.functions.empty()) {
    if (error) *error = "IR module has no functions";
    return false;
  }

  std::vector<simplevm::sbc::SigSpec> sig_specs = module.sig_specs;
  if (sig_specs.empty()) {
    simplevm::sbc::SigSpec default_sig;
    default_sig.ret_type_id = 0;
    default_sig.param_count = 0;
    sig_specs.push_back(default_sig);
  }

  std::vector<uint8_t> types = module.types_bytes;
  if (types.empty()) {
    AppendDefaultI32Type(&types);
  }

  std::vector<uint8_t> const_pool = module.const_pool;
  if (const_pool.empty()) {
    uint32_t dummy_str_offset = static_cast<uint32_t>(simplevm::sbc::AppendStringToPool(const_pool, ""));
    uint32_t dummy_const_id = 0;
    simplevm::sbc::AppendConstString(const_pool, dummy_str_offset, &dummy_const_id);
  }

  std::vector<uint8_t> sigs;
  BuildSigTable(sig_specs, &sigs);

  std::vector<uint8_t> methods;
  std::vector<uint8_t> functions;
  std::vector<uint8_t> code;
  size_t offset = 0;
  for (size_t i = 0; i < module.functions.size(); ++i) {
    const auto& func = module.functions[i];
    uint32_t sig_id = func.sig_id;
    if (sig_id >= sig_specs.size()) {
      if (error) *error = "function sig_id out of range";
      return false;
    }
    simplevm::sbc::AppendU32(methods, 0); // name_str
    simplevm::sbc::AppendU32(methods, sig_id);
    simplevm::sbc::AppendU32(methods, static_cast<uint32_t>(offset));
    simplevm::sbc::AppendU16(methods, func.local_count);
    simplevm::sbc::AppendU16(methods, 0); // flags

    simplevm::sbc::AppendU32(functions, static_cast<uint32_t>(i)); // method_id
    simplevm::sbc::AppendU32(functions, static_cast<uint32_t>(offset));
    simplevm::sbc::AppendU32(functions, static_cast<uint32_t>(func.code.size()));
    simplevm::sbc::AppendU32(functions, func.stack_max);

    code.insert(code.end(), func.code.begin(), func.code.end());
    offset += func.code.size();
  }

  std::vector<simplevm::sbc::SectionData> sections;
  sections.push_back({1, types, static_cast<uint32_t>(types.size() / 20), 0});
  sections.push_back({2, module.fields_bytes, static_cast<uint32_t>(module.fields_bytes.size() / 16), 0});
  sections.push_back({3, methods, static_cast<uint32_t>(module.functions.size()), 0});
  sections.push_back({4, sigs, static_cast<uint32_t>(sig_specs.size()), 0});
  sections.push_back({5, const_pool, 0, 0});
  sections.push_back({6, module.globals_bytes, static_cast<uint32_t>(module.globals_bytes.size() / 16), 0});
  sections.push_back({7, functions, static_cast<uint32_t>(module.functions.size()), 0});
  if (!module.imports_bytes.empty()) {
    sections.push_back({10, module.imports_bytes, static_cast<uint32_t>(module.imports_bytes.size() / 16), 0});
  }
  if (!module.exports_bytes.empty()) {
    sections.push_back({11, module.exports_bytes, static_cast<uint32_t>(module.exports_bytes.size() / 16), 0});
  }
  sections.push_back({8, code, 0, 0});
  if (!module.debug_bytes.empty()) {
    sections.push_back({9, module.debug_bytes, 0, 0});
  }

  *out = simplevm::sbc::BuildModuleFromSections(sections, module.entry_method_id);
  return true;
}

} // namespace simplevm::ir
