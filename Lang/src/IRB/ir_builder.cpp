#include "IRB/ir_builder.h"

#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "IRE/sir_emitter.h"

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

std::string Trim(std::string text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.erase(text.begin());
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.pop_back();
  return text;
}

std::vector<std::string> SplitCommaTypes(const std::string& text) {
  std::vector<std::string> out;
  if (Trim(text).empty()) return out;
  std::stringstream ss(text);
  std::string item;
  while (std::getline(ss, item, ',')) out.push_back(Trim(item));
  return out;
}

IrSig ParseSignatureText(const std::string& text) {
  IrSig sig;
  const size_t lparen = text.find('(');
  const size_t rparen = text.find(')', lparen == std::string::npos ? 0 : lparen);
  if (lparen != std::string::npos && rparen != std::string::npos) {
    for (const auto& param : SplitCommaTypes(text.substr(lparen + 1, rparen - lparen - 1))) {
      sig.params.push_back({param});
    }
  }
  const size_t arrow = text.find("->", rparen == std::string::npos ? 0 : rparen);
  if (arrow != std::string::npos) {
    const std::string result = Trim(text.substr(arrow + 2));
    if (!result.empty() && result != "void") {
      sig.result = {result};
      sig.has_result = true;
    }
  }
  return sig;
}

uint32_t ParseHeaderValue(const std::string& token, const std::string& prefix) {
  if (token.rfind(prefix, 0) != 0) return 0;
  try {
    return static_cast<uint32_t>(std::stoul(token.substr(prefix.size())));
  } catch (...) {
    return 0;
  }
}

void PopulateSirLines(const std::string& sir, std::vector<std::string>* out) {
  if (!out) return;
  out->clear();
  std::istringstream in(sir);
  std::string line;
  while (std::getline(in, line)) out->push_back(line);
}

void PopulateAllocationsFromSir(const std::string& sir, IrModule* out) {
  if (!out) return;
  out->signatures.clear();
  out->imports.clear();
  out->globals.clear();
  out->functions.clear();
  out->stack_infos.clear();
  std::unordered_map<std::string, IrSig> sigs_by_name;
  std::istringstream in(sir);
  std::string line;
  while (std::getline(in, line)) {
    if (line.rfind("  sig ", 0) == 0) {
      const size_t colon = line.find(':', 6);
      if (colon == std::string::npos) continue;
      IrSignature sig;
      sig.name = Trim(line.substr(6, colon - 6));
      sig.signature = ParseSignatureText(line.substr(colon + 1));
      sigs_by_name[sig.name] = sig.signature;
      out->signatures.push_back(std::move(sig));
      continue;
    }
    if (line.rfind("  global ", 0) == 0) {
      std::istringstream global_line(line);
      std::string indent_tag;
      IrGlobal global;
      global_line >> indent_tag >> global.name >> global.type.name;
      std::string token;
      while (global_line >> token) {
        if (token.rfind("init=", 0) == 0) global.init = token.substr(5);
      }
      out->globals.push_back(std::move(global));
      continue;
    }
    if (line.rfind("  import ", 0) == 0) {
      std::istringstream import_line(line);
      std::string indent_tag;
      IrImport import;
      import_line >> indent_tag >> import.name >> import.module >> import.symbol;
      std::string token;
      while (import_line >> token) {
        if (token.rfind("sig=", 0) == 0) import.signature_name = token.substr(4);
      }
      auto sig_it = sigs_by_name.find(import.signature_name);
      if (sig_it != sigs_by_name.end()) import.signature = sig_it->second;
      out->imports.push_back(std::move(import));
      continue;
    }
    if (line.rfind("func ", 0) == 0) {
      std::istringstream header(line);
      std::string tag;
      IrStackInfo info;
      IrFunction fn;
      header >> tag >> info.function;
      fn.name = info.function;
      std::string signature_name;
      std::string token;
      while (header >> token) {
        if (token.rfind("locals=", 0) == 0) info.locals = ParseHeaderValue(token, "locals=");
        if (token.rfind("stack=", 0) == 0) info.max_stack = ParseHeaderValue(token, "stack=");
        if (token.rfind("sig=", 0) == 0) signature_name = token.substr(4);
      }
      auto sig_it = sigs_by_name.find(signature_name);
      if (sig_it != sigs_by_name.end()) fn.signature = sig_it->second;
      out->functions.push_back(std::move(fn));
      out->stack_infos.push_back(std::move(info));
    }
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
  out->sir_text.clear();
  if (!Simple::Lang::EmitSir(*typed.resolved->program, &out->sir_text, error)) return false;
  PopulateSirLines(out->sir_text, &out->sir_lines);
  PopulateAllocationsFromSir(out->sir_text, &out->ir);
  PopulateArtifactLayouts(*typed.resolved->program, &out->ir);
  return true;
}

} // namespace Simple::Lang::IRB
