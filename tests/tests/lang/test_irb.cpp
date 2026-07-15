#include "test_utils.h"

#include "AST/lower_cast.h"
#include "CAST/parser.h"
#include "IRB/ir_builder.h"
#include "IRE/sir_emitter.h"
#include "RAST/resolver.h"
#include "TAST/type_checker.h"

namespace Simple::VM::Tests {
namespace {

bool LangSplitIrbBuildsModule() {
  Simple::Lang::Program cast_program;
  Simple::Lang::AST::Program ast_program;
  Simple::Lang::RAST::ResolvedProgram resolved;
  Simple::Lang::TAST::TypedProgram typed;
  Simple::Lang::IRB::Module module;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString("main : () -> i32 { return 3; }", &cast_program, &error)) {
    return false;
  }
  if (!Simple::Lang::AST::LowerCastProgram(cast_program, &ast_program, &error)) return false;
  if (!Simple::Lang::RAST::ResolveProgram(ast_program, &resolved, &error)) return false;
  if (!Simple::Lang::TAST::CheckResolvedProgram(resolved, &typed, &error)) return false;
  if (!Simple::Lang::IRB::BuildModule(typed, &module, &error)) return false;
  return !module.sir_text.empty() && !module.ir.functions.empty() && module.ir.functions[0].name == "main";
}

bool LangIrbIrePipelineEmitsRunnableSir() {
  const char* src =
      "Box :: artifact {\n"
      "  v : i32\n"
      "  score : () -> i32 { return self.v + 40; }\n"
      "}\n"
      "main : () -> i32 { b : Box = { 2 }; return b.score(); }\n";
  Simple::Lang::Program cast_program;
  Simple::Lang::AST::Program ast_program;
  Simple::Lang::RAST::ResolvedProgram resolved;
  Simple::Lang::TAST::TypedProgram typed;
  Simple::Lang::IRB::Module module;
  std::string sir;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &cast_program, &error)) return false;
  if (!Simple::Lang::AST::LowerCastProgram(cast_program, &ast_program, &error)) return false;
  if (!Simple::Lang::RAST::ResolveProgram(ast_program, &resolved, &error)) return false;
  if (!Simple::Lang::TAST::CheckResolvedProgram(resolved, &typed, &error)) return false;
  if (!Simple::Lang::IRB::BuildModule(typed, &module, &error)) return false;
  if (module.ir.artifact_layouts.size() != 1) return false;
  if (module.ir.artifact_layouts[0].name != "Box") return false;
  if (module.ir.artifact_layouts[0].fields.size() != 1) return false;
  if (module.ir.artifact_layouts[0].fields[0].name != "v") return false;
  if (module.ir.artifact_layouts[0].fields[0].type.name != "i32") return false;
  bool saw_main_stack = false;
  for (const auto& stack : module.ir.stack_infos) {
    if (stack.function == "main" && stack.locals > 0 && stack.max_stack > 0) saw_main_stack = true;
  }
  if (!saw_main_stack) return false;
  if (!Simple::Lang::IRE::EmitSirModule(module, &sir, &error)) return false;
  return RunSirTextExpectExit(sir, 42);
}


bool LangIrbStructuredIrSkeletonStoresModuleShape() {
  Simple::Lang::IRB::Module module;
  Simple::Lang::IRB::IrImport import;
  import.module = "env";
  import.symbol = "puts";
  import.signature.params.push_back({"string"});
  import.signature.result = {"i32"};
  import.signature.has_result = true;
  module.ir.imports.push_back(import);

  Simple::Lang::IRB::IrFunction fn;
  fn.name = "main";
  fn.signature.result = {"i32"};
  fn.signature.has_result = true;
  Simple::Lang::IRB::IrBlock block;
  block.label = "entry";
  block.instructions.push_back({"const i32", {"42"}});
  block.instructions.push_back({"ret", {}});
  fn.blocks.push_back(block);
  module.ir.functions.push_back(fn);

  return module.ir.imports.size() == 1 &&
         module.ir.functions.size() == 1 &&
         module.ir.functions[0].blocks.size() == 1 &&
         module.ir.functions[0].blocks[0].instructions.size() == 2 &&
         module.ir.functions[0].signature.result.name == "i32" &&
         module.ir.artifact_layouts.empty();
}


bool LangIrbCollectsAllocationMetadata() {
  const char* src =
      "extern host.consume : (value : i32) -> i32\n"
      "g : i32 = 7\n"
      "main : () -> i32 { host.consume(7); return g; }\n";
  Simple::Lang::Program cast_program;
  Simple::Lang::AST::Program ast_program;
  Simple::Lang::RAST::ResolvedProgram resolved;
  Simple::Lang::TAST::TypedProgram typed;
  Simple::Lang::IRB::Module module;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &cast_program, &error)) return false;
  if (!Simple::Lang::AST::LowerCastProgram(cast_program, &ast_program, &error)) return false;
  if (!Simple::Lang::RAST::ResolveProgram(ast_program, &resolved, &error)) return false;
  if (!Simple::Lang::TAST::CheckResolvedProgram(resolved, &typed, &error)) return false;
  if (!Simple::Lang::IRB::BuildModule(typed, &module, &error)) return false;
  bool saw_main_sig = false;
  for (const auto& sig : module.ir.signatures) {
    if (sig.name == "main" && sig.signature.has_result && sig.signature.result.name == "i32") saw_main_sig = true;
  }
  bool saw_global = false;
  for (const auto& global : module.ir.globals) {
    if (global.name == "g" && global.type.name == "i32") saw_global = true;
  }
  bool saw_import = false;
  for (const auto& import : module.ir.imports) {
    if (import.module == "host" && import.symbol == "consume" && import.signature.params.size() == 1 &&
        import.signature.params[0].name == "i32" && import.signature.has_result &&
        import.signature.result.name == "i32") saw_import = true;
  }
  bool saw_function = false;
  for (const auto& fn : module.ir.functions) {
    if (fn.name == "main" && fn.signature.has_result && fn.signature.result.name == "i32") saw_function = true;
  }
  return saw_main_sig && saw_global && saw_import && saw_function;
}


bool LangIrbCollectsAbiFlatteningMetadata() {
  const char* src =
      "Inner :: artifact { x : i32; y : i32 }\n"
      "Outer :: artifact { inner : Inner; z : f64 }\n"
      "main : () -> i32 { return 0; }\n";
  Simple::Lang::Program cast_program;
  Simple::Lang::AST::Program ast_program;
  Simple::Lang::RAST::ResolvedProgram resolved;
  Simple::Lang::TAST::TypedProgram typed;
  Simple::Lang::IRB::Module module;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &cast_program, &error)) return false;
  if (!Simple::Lang::AST::LowerCastProgram(cast_program, &ast_program, &error)) return false;
  if (!Simple::Lang::RAST::ResolveProgram(ast_program, &resolved, &error)) return false;
  if (!Simple::Lang::TAST::CheckResolvedProgram(resolved, &typed, &error)) return false;
  if (!Simple::Lang::IRB::BuildModule(typed, &module, &error)) return false;
  for (const auto& abi : module.ir.abi_types) {
    if (abi.name != "Outer$abi") continue;
    if (abi.fields.size() != 3) return false;
    return abi.fields[0].name == "inner.x" && abi.fields[0].type.name == "i32" &&
           abi.fields[1].name == "inner.y" && abi.fields[1].type.name == "i32" &&
           abi.fields[2].name == "z" && abi.fields[2].type.name == "f64";
  }
  return false;
}


bool LangIrbIreKeepsSirOutputStable() {
  const char* src =
      "Box :: artifact { v : i32; score : () -> i32 { return self.v + 40; } }\n"
      "main : () -> i32 { b : Box = { 2 }; return b.score(); }\n";
  Simple::Lang::Program cast_program;
  Simple::Lang::AST::Program ast_program;
  Simple::Lang::RAST::ResolvedProgram resolved;
  Simple::Lang::TAST::TypedProgram typed;
  Simple::Lang::IRB::Module module;
  std::string direct_sir;
  std::string pipeline_sir;
  std::string error;
  if (!Simple::Lang::IRE::EmitSirFromString(src, &direct_sir, &error)) return false;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &cast_program, &error)) return false;
  if (!Simple::Lang::AST::LowerCastProgram(cast_program, &ast_program, &error)) return false;
  if (!Simple::Lang::RAST::ResolveProgram(ast_program, &resolved, &error)) return false;
  if (!Simple::Lang::TAST::CheckResolvedProgram(resolved, &typed, &error)) return false;
  if (!Simple::Lang::IRB::BuildModule(typed, &module, &error)) return false;
  if (!Simple::Lang::IRE::EmitSirModule(module, &pipeline_sir, &error)) return false;
  return direct_sir == pipeline_sir && RunSirTextExpectExit(pipeline_sir, 42);
}


bool LangIrbRejectsMissingTypedInput() {
  Simple::Lang::TAST::TypedProgram typed;
  Simple::Lang::IRB::Module module;
  std::string error;
  if (Simple::Lang::IRB::BuildModule(typed, &module, &error)) return false;
  return error.find("missing typed program input") != std::string::npos;
}



const TestCase kLangIrbTests[] = {
  {"lang_split_irb_builds_module", LangSplitIrbBuildsModule},
  {"lang_irb_ire_pipeline_emits_runnable_sir", LangIrbIrePipelineEmitsRunnableSir},
  {"lang_irb_structured_ir_skeleton_stores_module_shape", LangIrbStructuredIrSkeletonStoresModuleShape},
  {"lang_irb_collects_allocation_metadata", LangIrbCollectsAllocationMetadata},
  {"lang_irb_collects_abi_flattening_metadata", LangIrbCollectsAbiFlatteningMetadata},
  {"lang_irb_ire_keeps_sir_output_stable", LangIrbIreKeepsSirOutputStable},
  {"lang_irb_rejects_missing_typed_input", LangIrbRejectsMissingTypedInput},
};

const TestSection kLangIrbSections[] = {
  {"lang_irb", kLangIrbTests, sizeof(kLangIrbTests) / sizeof(kLangIrbTests[0])},
};

} // namespace

const TestSection* GetLangIrbSections(size_t* count) {
  if (count) *count = sizeof(kLangIrbSections) / sizeof(kLangIrbSections[0]);
  return kLangIrbSections;
}

} // namespace Simple::VM::Tests
