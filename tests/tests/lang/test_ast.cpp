#include "test_utils.h"

#include <unordered_map>

#include "AST/lower_cast.h"
#include "CAST/parser.h"

namespace Simple::VM::Tests {
namespace {

bool LangSplitAstLowersCastProgram() {
  Simple::Lang::Program cast_program;
  Simple::Lang::AST::Program ast_program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString("main : () -> i32 { x : i32 = 1; return x; }", &cast_program, &error)) {
    return false;
  }
  if (!Simple::Lang::AST::LowerCastProgram(cast_program, &ast_program, &error)) return false;
  if (ast_program.decls.size() != 1) return false;
  const auto& decl = ast_program.decls[0];
  if (decl.kind != Simple::Lang::DeclKind::Function) return false;
  return decl.func.body.size() == 2 &&
         decl.func.body[0].kind == Simple::Lang::StmtKind::VarDecl &&
         decl.func.body[0].var_decl.name == "x" &&
         decl.func.body[1].kind == Simple::Lang::StmtKind::Return;
}

bool LangAstLowerCastPreservesProgramShape() {
  const char* src =
      "main : () -> i32 {\n"
      "  x : i32 = 40 + 2;\n"
      "  return x;\n"
      "}\n";
  Simple::Lang::Program cast_program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &cast_program, &error)) return false;
  Simple::Lang::AST::Program ast_program;
  if (!Simple::Lang::AST::LowerCastProgram(cast_program, &ast_program, &error)) return false;
  if (ast_program.decls.size() != 1) return false;
  if (ast_program.decls[0].kind != Simple::Lang::DeclKind::Function) return false;
  if (ast_program.decls[0].func.body.size() != 2) return false;
  return ast_program.decls[0].func.body[0].kind == Simple::Lang::StmtKind::VarDecl &&
         ast_program.decls[0].func.body[1].kind == Simple::Lang::StmtKind::Return;
}

bool LangAstNormalizesTopLevelScriptBody() {
  const char* src =
      "add : (a : i32, b : i32) -> i32 { return a + b; }\n"
      "x = add(40, 2);\n"
      "x = x + 1;\n";
  Simple::Lang::Program cast_program;
  Simple::Lang::AST::NormalizedProgram ast_program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &cast_program, &error)) return false;
  if (!Simple::Lang::AST::LowerCastProgramNormalized(cast_program, &ast_program, &error)) return false;
  if (ast_program.decls.size() != 1) return false;
  if (ast_program.script_body.statements.size() != 2) return false;
  return ast_program.script_body.statements[0].kind == Simple::Lang::StmtKind::Assign &&
         ast_program.script_body.statements[1].kind == Simple::Lang::StmtKind::Assign;
}

bool LangAstNormalizesFnLiteralDeclarations() {
  const char* src =
      "main : () -> i32 {\n"
      "  f : fn (a : i32,  b : i32) -> i32 = (a, b) { return a + b; };\n"
      "  return f(1, 2);\n"
      "}\n";
  Simple::Lang::Program cast_program;
  Simple::Lang::AST::NormalizedProgram ast_program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &cast_program, &error)) return false;
  if (!Simple::Lang::AST::LowerCastProgramNormalized(cast_program, &ast_program, &error)) return false;
  if (ast_program.fn_literals.size() != 1) return false;
  const auto& fn = ast_program.fn_literals[0];
  if (fn.binding_name != "f") return false;
  if (!fn.signature.is_proc || !fn.signature.proc_return) return false;
  if (fn.signature.proc_return->name != "i32") return false;
  if (fn.signature.proc_params.size() != 2) return false;
  if (fn.params.size() != 2 || fn.params[0].name != "a" || fn.params[1].name != "b") return false;
  return !fn.body.empty();
}

bool LangAstNormalizesLoopShorthand() {
  const char* src =
      "main : () -> i32 {\n"
      "  total : i32 = 0;\n"
      "  for (i; i < 3; i++) { total += i; }\n"
      "  while (total < 10) { total += 1; }\n"
      "  return total;\n"
      "}\n";
  Simple::Lang::Program cast_program;
  Simple::Lang::AST::NormalizedProgram ast_program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &cast_program, &error)) return false;
  if (!Simple::Lang::AST::LowerCastProgramNormalized(cast_program, &ast_program, &error)) return false;
  if (ast_program.loops.size() != 2) return false;
  const auto& for_loop = ast_program.loops[0];
  if (for_loop.kind != Simple::Lang::AST::NormalizedLoopKind::For) return false;
  if (!for_loop.has_initializer || !for_loop.has_loop_var_decl) return false;
  if (for_loop.loop_var_decl.name != "i") return false;
  if (for_loop.loop_var_decl.type.name != "i32") return false;
  if (!for_loop.loop_var_decl.has_init_expr || for_loop.loop_var_decl.init_expr.text != "0") return false;
  if (for_loop.condition.kind != Simple::Lang::ExprKind::Binary || for_loop.condition.op != "<") return false;
  if (for_loop.step.kind != Simple::Lang::ExprKind::Unary || for_loop.step.op != "post++") return false;
  return ast_program.loops[1].kind == Simple::Lang::AST::NormalizedLoopKind::While;
}

bool LangAstNormalizesIfChain() {
  const char* src =
      "main : () -> i32 {\n"
      "  x : i32 = 1;\n"
      "  |> (x == 0) { return 0; }\n"
      "  |> (x == 1) { return 1; }\n"
      "  |> default { return 2; }\n"
      "}\n";
  Simple::Lang::Program cast_program;
  Simple::Lang::AST::NormalizedProgram ast_program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &cast_program, &error)) return false;
  if (!Simple::Lang::AST::LowerCastProgramNormalized(cast_program, &ast_program, &error)) return false;
  if (ast_program.if_chains.size() != 1) return false;
  const auto& chain = ast_program.if_chains[0];
  if (chain.branches.size() != 2) return false;
  if (chain.else_branch.size() != 1) return false;
  if (chain.branches[0].condition.kind != Simple::Lang::ExprKind::Binary) return false;
  if (chain.branches[0].condition.op != "==") return false;
  return chain.branches[0].body.size() == 1 && chain.branches[1].body.size() == 1;
}

bool LangAstNormalizesSwitchBranches() {
  const char* src =
      "main : () -> i32 {\n"
      "  x : i32 = 1;\n"
      "  y : i32 = switch (x) {\n"
      "    x == 0 => 0\n"
      "    x == 1 => { while (true) { break; } while (true) { skip; } local : i32 = 1; return local }\n"
      "    default => return 2\n"
      "  };\n"
      "  return y;\n"
      "}\n";
  Simple::Lang::Program cast_program;
  Simple::Lang::AST::NormalizedProgram ast_program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &cast_program, &error)) return false;
  if (!Simple::Lang::AST::LowerCastProgramNormalized(cast_program, &ast_program, &error)) return false;
  if (ast_program.switches.size() != 1) return false;
  const auto& sw = ast_program.switches[0];
  if (sw.usage != Simple::Lang::AST::NormalizedSwitchUsage::Assignment) return false;
  if (sw.scrutinee.kind != Simple::Lang::ExprKind::Identifier || sw.scrutinee.text != "x") return false;
  if (sw.branches.size() != 3) return false;
  if (sw.branches[0].is_default || !sw.branches[0].has_inline_value || sw.branches[0].is_block) return false;
  if (sw.branches[0].result_kind != Simple::Lang::AST::NormalizedSwitchBranchResultKind::InlineValue) return false;
  if (!sw.branches[1].is_block || sw.branches[1].block.size() != 4) return false;
  if (sw.branches[1].result_kind != Simple::Lang::AST::NormalizedSwitchBranchResultKind::Block) return false;
  if (sw.branches[1].flow.may_fallthrough || !sw.branches[1].flow.always_returns) return false;
  if (!sw.branches[1].flow.may_break || !sw.branches[1].flow.may_skip) return false;
  for (const auto& branch : sw.branches) {
    if (branch.falls_through_to_next) return false;
  }
  return sw.branches[2].is_default && sw.branches[2].is_explicit_return && sw.branches[2].has_inline_value &&
         sw.branches[2].result_kind == Simple::Lang::AST::NormalizedSwitchBranchResultKind::SwitchBranchReturn;
}

bool LangAstClassifiesSwitchUsage() {
  const char* src =
      "main : () -> i32 {\n"
      "  x : i32 = 1;\n"
      "  y : i32 = 0;\n"
      "  switch (x) { default => 0 };\n"
      "  y = switch (x) { default => 1 };\n"
      "  return switch (x) { default => 2 };\n"
      "}\n";
  Simple::Lang::Program cast_program;
  Simple::Lang::AST::NormalizedProgram ast_program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &cast_program, &error)) return false;
  if (!Simple::Lang::AST::LowerCastProgramNormalized(cast_program, &ast_program, &error)) return false;
  bool saw_statement = false;
  bool saw_assignment = false;
  bool saw_expression = false;
  for (const auto& sw : ast_program.switches) {
    if (sw.usage == Simple::Lang::AST::NormalizedSwitchUsage::Statement) saw_statement = true;
    if (sw.usage == Simple::Lang::AST::NormalizedSwitchUsage::Assignment) saw_assignment = true;
    if (sw.usage == Simple::Lang::AST::NormalizedSwitchUsage::Expression) saw_expression = true;
  }
  return saw_statement && saw_assignment && saw_expression;
}

bool LangAstNormalizesCallMemberIndexShapes() {
  const char* src =
      "main : () -> i32 {\n"
      "  x : i32 = add(1, 2);\n"
      "  y : i32 = arr[0];\n"
      "  z : i32 = box.score();\n"
      "  return x + y + z;\n"
      "}\n";
  Simple::Lang::Program cast_program;
  Simple::Lang::AST::NormalizedProgram ast_program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &cast_program, &error)) return false;
  if (!Simple::Lang::AST::LowerCastProgramNormalized(cast_program, &ast_program, &error)) return false;
  bool saw_call = false;
  bool saw_member = false;
  bool saw_index = false;
  bool saw_member_call = false;
  for (const auto& shape : ast_program.expr_shapes) {
    if (shape.kind == Simple::Lang::AST::NormalizedExprShapeKind::Call &&
        shape.base.kind == Simple::Lang::ExprKind::Identifier && shape.base.text == "add" &&
        shape.args.size() == 2) saw_call = true;
    if (shape.kind == Simple::Lang::AST::NormalizedExprShapeKind::Member &&
        shape.base.kind == Simple::Lang::ExprKind::Identifier && shape.base.text == "box" &&
        shape.member == "score" && shape.op == ".") saw_member = true;
    if (shape.kind == Simple::Lang::AST::NormalizedExprShapeKind::Index &&
        shape.base.kind == Simple::Lang::ExprKind::Identifier && shape.base.text == "arr" &&
        shape.index.kind == Simple::Lang::ExprKind::Literal && shape.index.text == "0") saw_index = true;
    if (shape.kind == Simple::Lang::AST::NormalizedExprShapeKind::Call &&
        shape.base.kind == Simple::Lang::ExprKind::Member && shape.base.text == "score") saw_member_call = true;
  }
  return saw_call && saw_member && saw_index && saw_member_call;
}


bool LangAstTypeCoverage() {
  const char* src =
      "a : i8; b : u8; c : i16; d : u16; e : i32; f : u32; g : i64; h : u64; "
      "i : f32; j : f64; k : bool; l : char; m : string; "
      "arr : i32{2}; list : i32[]; grid : i32[][]; "
      "proc : fn () -> i32; proc2 :: fn (a : i32,  b : f64) -> bool;";
  Simple::Lang::Program program;
  std::string error;
  if (!Simple::Lang::CAST::ParseProgramFromString(src, &program, &error)) return false;
  std::unordered_map<std::string, const Simple::Lang::VarDecl*> vars;
  for (const auto& decl : program.decls) {
    if (decl.kind != Simple::Lang::DeclKind::Variable) continue;
    vars.emplace(decl.var.name, &decl.var);
  }
  const char* primitives[] = {
    "i8","u8","i16","u16","i32","u32","i64","u64","f32","f64","bool","char","string"
  };
  const char* names[] = {
    "a","b","c","d","e","f","g","h","i","j","k","l","m"
  };
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
    auto it = vars.find(names[i]);
    if (it == vars.end()) return false;
    if (it->second->type.name != primitives[i]) return false;
  }
  {
    auto it = vars.find("arr");
    if (it == vars.end()) return false;
    if (it->second->type.name != "i32") return false;
    if (it->second->type.dims.size() != 1) return false;
    if (!it->second->type.dims[0].has_size || it->second->type.dims[0].size != 2) return false;
  }
  {
    auto it = vars.find("list");
    if (it == vars.end()) return false;
    if (it->second->type.name != "i32") return false;
    if (it->second->type.dims.size() != 1) return false;
    if (!it->second->type.dims[0].is_list) return false;
  }
  {
    auto it = vars.find("grid");
    if (it == vars.end()) return false;
    if (it->second->type.name != "i32") return false;
    if (it->second->type.dims.size() != 2) return false;
    if (!it->second->type.dims[0].is_list || !it->second->type.dims[1].is_list) return false;
  }
  {
    auto it = vars.find("proc");
    if (it == vars.end()) return false;
    if (!it->second->type.is_proc) return false;
    if (!it->second->type.proc_return) return false;
    if (it->second->type.proc_return->name != "i32") return false;
    if (!it->second->type.proc_params.empty()) return false;
  }
  {
    auto it = vars.find("proc2");
    if (it == vars.end()) return false;
    if (!it->second->type.is_proc) return false;
    if (it->second->type.proc_params.size() != 2) return false;
    if (it->second->type.proc_params[0].name != "i32") return false;
    if (it->second->type.proc_params[1].name != "f64") return false;
    if (!it->second->type.proc_return) return false;
    if (it->second->type.proc_return->name != "bool") return false;
  }
  return true;
}



const TestCase kLangAstTests[] = {
  {"lang_split_ast_lowers_cast_program", LangSplitAstLowersCastProgram},
  {"lang_ast_lower_cast_preserves_program_shape", LangAstLowerCastPreservesProgramShape},
  {"lang_ast_normalizes_top_level_script_body", LangAstNormalizesTopLevelScriptBody},
  {"lang_ast_normalizes_fn_literal_declarations", LangAstNormalizesFnLiteralDeclarations},
  {"lang_ast_normalizes_loop_shorthand", LangAstNormalizesLoopShorthand},
  {"lang_ast_normalizes_if_chain", LangAstNormalizesIfChain},
  {"lang_ast_normalizes_switch_branches", LangAstNormalizesSwitchBranches},
  {"lang_ast_classifies_switch_usage", LangAstClassifiesSwitchUsage},
  {"lang_ast_normalizes_call_member_index_shapes", LangAstNormalizesCallMemberIndexShapes},
  {"lang_ast_type_coverage", LangAstTypeCoverage},
};

const TestSection kLangAstSections[] = {
  {"lang_ast", kLangAstTests, sizeof(kLangAstTests) / sizeof(kLangAstTests[0])},
};

} // namespace

const TestSection* GetLangAstSections(size_t* count) {
  if (count) *count = sizeof(kLangAstSections) / sizeof(kLangAstSections[0]);
  return kLangAstSections;
}

} // namespace Simple::VM::Tests
