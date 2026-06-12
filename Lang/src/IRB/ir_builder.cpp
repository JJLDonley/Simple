#include "IRB/ir_builder.h"

#include <utility>

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

void PopulateArtifactLayouts(const Simple::Lang::AST::Program& program, IrModule* out) {
  if (!out) return;
  out->artifact_layouts.clear();
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
  return Simple::Lang::EmitSir(*typed.resolved->program, &out->sir_text, error);
}

} // namespace Simple::Lang::IRB
