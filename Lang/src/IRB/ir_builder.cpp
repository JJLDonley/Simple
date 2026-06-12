#include "IRB/ir_builder.h"

#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "lang_sir.h"

namespace Simple::Lang::IRB {
namespace {

IrType ToIrType(const Simple::Lang::AST::TypeRef& type) {
  IrType out;
  out.name = type.name;
  for (uint32_t i = 0; i < type.pointer_depth; ++i) out.name += "*";
  for (const auto& dim : type.dims) {
    out.name += dim.is_list ? "[]" : "{" + std::to_string(dim.size) + "}";
  }
  return out;
}

const Simple::Lang::AST::ArtifactDecl* FindArtifact(const Simple::Lang::AST::Program& program,
                                                     const std::string& name) {
  for (const auto& decl : program.decls) {
    if (decl.kind == Simple::Lang::AST::DeclKind::Artifact && decl.artifact.name == name) {
      return &decl.artifact;
    }
  }
  return nullptr;
}

void CollectAbiFields(const Simple::Lang::AST::Program& program,
                      const Simple::Lang::AST::ArtifactDecl& artifact,
                      const std::string& prefix,
                      std::vector<IrAbiField>* out) {
  if (!out) return;
  for (const auto& field : artifact.fields) {
    const auto* nested = FindArtifact(program, field.type.name);
    if (nested && field.type.pointer_depth == 0 && field.type.dims.empty()) {
      CollectAbiFields(program, *nested, prefix + field.name + ".", out);
      continue;
    }
    IrAbiField abi_field;
    abi_field.name = prefix + field.name;
    abi_field.type = ToIrType(field.type);
    out->push_back(std::move(abi_field));
  }
}

uint32_t ParseHeaderValue(const std::string& token, const std::string& prefix) {
  if (token.rfind(prefix, 0) != 0) return 0;
  try {
    return static_cast<uint32_t>(std::stoul(token.substr(prefix.size())));
  } catch (...) {
    return 0;
  }
}

void PopulateStackInfoFromSir(const std::string& sir, IrModule* out) {
  if (!out) return;
  out->stack_infos.clear();
  std::istringstream in(sir);
  std::string line;
  while (std::getline(in, line)) {
    if (line.rfind("func ", 0) != 0) continue;
    std::istringstream header(line);
    std::string tag;
    IrStackInfo info;
    header >> tag >> info.function;
    std::string token;
    while (header >> token) {
      if (token.rfind("locals=", 0) == 0) info.locals = ParseHeaderValue(token, "locals=");
      if (token.rfind("stack=", 0) == 0) info.max_stack = ParseHeaderValue(token, "stack=");
    }
    out->stack_infos.push_back(std::move(info));
  }
}

void PopulateArtifactLayouts(const Simple::Lang::AST::Program& program, IrModule* out) {
  if (!out) return;
  out->artifact_layouts.clear();
  out->abi_types.clear();
  for (const auto& decl : program.decls) {
    if (decl.kind != Simple::Lang::AST::DeclKind::Artifact) continue;
    IrArtifactLayout layout;
    layout.name = decl.artifact.name;
    layout.fields.reserve(decl.artifact.fields.size());
    for (size_t i = 0; i < decl.artifact.fields.size(); ++i) {
      const auto& field = decl.artifact.fields[i];
      IrArtifactField ir_field;
      ir_field.name = field.name;
      ir_field.type = ToIrType(field.type);
      ir_field.index = static_cast<uint32_t>(i);
      layout.fields.push_back(std::move(ir_field));
    }
    out->artifact_layouts.push_back(std::move(layout));

    IrAbiType abi;
    abi.name = decl.artifact.name + "$abi";
    CollectAbiFields(program, decl.artifact, {}, &abi.fields);
    out->abi_types.push_back(std::move(abi));
  }
}

} // namespace

bool BuildModule(const Simple::Lang::TAST::TypedProgram& typed,
                 Module* out,
                 std::string* error) {
  if (!out) {
    if (error) *error = "missing IRB output module";
    return false;
  }
  if (!typed.resolved || !typed.resolved->program) {
    if (error) *error = "missing typed program input";
    return false;
  }
  out->ir = {};
  PopulateArtifactLayouts(*typed.resolved->program, &out->ir);
  out->sir_text.clear();
  if (!Simple::Lang::EmitSir(*typed.resolved->program, &out->sir_text, error)) return false;
  PopulateStackInfoFromSir(out->sir_text, &out->ir);
  return true;
}

} // namespace Simple::Lang::IRB
