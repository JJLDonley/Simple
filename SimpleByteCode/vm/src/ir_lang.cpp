#include "ir_lang.h"

#include <cctype>
#include <sstream>
#include <unordered_map>

#include "ir_builder.h"

namespace simplevm::irtext {
namespace {

std::string Trim(const std::string& text) {
  size_t start = 0;
  while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) start++;
  size_t end = text.size();
  while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) end--;
  return text.substr(start, end - start);
}

std::string StripComment(const std::string& line) {
  size_t cut = line.find_first_of(";#");
  if (cut == std::string::npos) return line;
  return line.substr(0, cut);
}

std::vector<std::string> SplitTokens(const std::string& line) {
  std::vector<std::string> out;
  std::istringstream iss(line);
  std::string tok;
  while (iss >> tok) {
    out.push_back(tok);
  }
  return out;
}

bool ParseUint(const std::string& text, uint64_t* out) {
  if (!out) return false;
  try {
    size_t idx = 0;
    uint64_t value = std::stoull(text, &idx, 0);
    if (idx != text.size()) return false;
    *out = value;
    return true;
  } catch (...) {
    return false;
  }
}

bool ParseInt(const std::string& text, int64_t* out) {
  if (!out) return false;
  try {
    size_t idx = 0;
    int64_t value = std::stoll(text, &idx, 0);
    if (idx != text.size()) return false;
    *out = value;
    return true;
  } catch (...) {
    return false;
  }
}

bool ParseFloat(const std::string& text, double* out) {
  if (!out) return false;
  try {
    size_t idx = 0;
    double value = std::stod(text, &idx);
    if (idx != text.size()) return false;
    *out = value;
    return true;
  } catch (...) {
    return false;
  }
}

std::string Lower(const std::string& text) {
  std::string out = text;
  for (char& ch : out) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return out;
}

} // namespace

bool ParseIrTextModule(const std::string& text, IrTextModule* out, std::string* error) {
  if (!out) {
    if (error) *error = "output module is null";
    return false;
  }
  out->functions.clear();
  out->entry_name.clear();
  out->entry_index = 0;

  IrTextFunction* current = nullptr;
  std::istringstream input(text);
  std::string raw;
  size_t line_no = 0;
  while (std::getline(input, raw)) {
    line_no++;
    std::string line = Trim(StripComment(raw));
    if (line.empty()) continue;

    if (line.rfind("func ", 0) == 0) {
      std::vector<std::string> tokens = SplitTokens(line);
      if (tokens.size() < 2) {
        if (error) *error = "func missing name at line " + std::to_string(line_no);
        return false;
      }
      out->functions.push_back(IrTextFunction{});
      current = &out->functions.back();
      current->name = tokens[1];
      for (size_t i = 2; i < tokens.size(); ++i) {
        const std::string& kv = tokens[i];
        size_t eq = kv.find('=');
        if (eq == std::string::npos) continue;
        std::string key = kv.substr(0, eq);
        std::string val = kv.substr(eq + 1);
        uint64_t num = 0;
        if (key == "locals" && ParseUint(val, &num)) {
          current->locals = static_cast<uint16_t>(num);
        } else if (key == "stack" && ParseUint(val, &num)) {
          current->stack_max = static_cast<uint32_t>(num);
        } else if (key == "sig" && ParseUint(val, &num)) {
          current->sig_id = static_cast<uint32_t>(num);
        }
      }
      continue;
    }

    if (line == "end") {
      current = nullptr;
      continue;
    }

    if (line.rfind("entry ", 0) == 0) {
      std::vector<std::string> tokens = SplitTokens(line);
      if (tokens.size() != 2) {
        if (error) *error = "entry expects a function name at line " + std::to_string(line_no);
        return false;
      }
      out->entry_name = tokens[1];
      continue;
    }

    if (!current) {
      if (error) *error = "instruction outside func at line " + std::to_string(line_no);
      return false;
    }

    if (!line.empty() && line.back() == ':') {
      IrTextInst inst;
      inst.kind = InstKind::Label;
      inst.label = Trim(line.substr(0, line.size() - 1));
      current->insts.push_back(std::move(inst));
      continue;
    }

    std::vector<std::string> tokens = SplitTokens(line);
    if (tokens.empty()) continue;
    IrTextInst inst;
    inst.kind = InstKind::Op;
    inst.op = tokens[0];
    for (size_t i = 1; i < tokens.size(); ++i) {
      inst.args.push_back(tokens[i]);
    }
    current->insts.push_back(std::move(inst));
  }

  if (!out->entry_name.empty()) {
    for (size_t i = 0; i < out->functions.size(); ++i) {
      if (out->functions[i].name == out->entry_name) {
        out->entry_index = static_cast<uint32_t>(i);
        return true;
      }
    }
    if (error) *error = "entry function not found";
    return false;
  }
  return true;
}

bool LowerIrTextToModule(const IrTextModule& text, simplevm::ir::IrModule* out, std::string* error) {
  if (!out) {
    if (error) *error = "output module is null";
    return false;
  }
  out->functions.clear();
  out->entry_method_id = text.entry_index;

  for (const auto& fn : text.functions) {
    simplevm::IrBuilder builder;
    std::unordered_map<std::string, IrLabel> labels;
    for (const auto& inst : fn.insts) {
      if (inst.kind == InstKind::Label && !inst.label.empty()) {
        if (labels.find(inst.label) == labels.end()) {
          labels[inst.label] = builder.CreateLabel();
        }
      }
    }

    for (const auto& inst : fn.insts) {
      if (inst.kind == InstKind::Label) {
        auto it = labels.find(inst.label);
        if (it == labels.end()) {
          if (error) *error = "label missing: " + inst.label;
          return false;
        }
        if (!builder.BindLabel(it->second, error)) return false;
        continue;
      }

      std::string op = Lower(inst.op);
      if (op == "enter") {
        uint64_t locals = 0;
        if (inst.args.size() != 1 || !ParseUint(inst.args[0], &locals)) {
          if (error) *error = "enter expects locals";
          return false;
        }
        builder.EmitEnter(static_cast<uint16_t>(locals));
        continue;
      }
      if (op == "ret") {
        builder.EmitRet();
        continue;
      }
      if (op == "nop") {
        builder.EmitOp(simplevm::OpCode::Nop);
        continue;
      }
      if (op == "pop") {
        builder.EmitPop();
        continue;
      }
      if (op == "dup") {
        builder.EmitDup();
        continue;
      }
      if (op == "dup2") {
        builder.EmitDup2();
        continue;
      }
      if (op == "swap") {
        builder.EmitSwap();
        continue;
      }
      if (op == "rot") {
        builder.EmitRot();
        continue;
      }
      if (op == "const.i32") {
        int64_t value = 0;
        if (inst.args.size() != 1 || !ParseInt(inst.args[0], &value)) {
          if (error) *error = "const.i32 expects value";
          return false;
        }
        builder.EmitConstI32(static_cast<int32_t>(value));
        continue;
      }
      if (op == "const.i8") {
        int64_t value = 0;
        if (inst.args.size() != 1 || !ParseInt(inst.args[0], &value)) {
          if (error) *error = "const.i8 expects value";
          return false;
        }
        builder.EmitConstI8(static_cast<int8_t>(value));
        continue;
      }
      if (op == "const.i16") {
        int64_t value = 0;
        if (inst.args.size() != 1 || !ParseInt(inst.args[0], &value)) {
          if (error) *error = "const.i16 expects value";
          return false;
        }
        builder.EmitConstI16(static_cast<int16_t>(value));
        continue;
      }
      if (op == "const.i64") {
        int64_t value = 0;
        if (inst.args.size() != 1 || !ParseInt(inst.args[0], &value)) {
          if (error) *error = "const.i64 expects value";
          return false;
        }
        builder.EmitConstI64(value);
        continue;
      }
      if (op == "const.u8") {
        uint64_t value = 0;
        if (inst.args.size() != 1 || !ParseUint(inst.args[0], &value)) {
          if (error) *error = "const.u8 expects value";
          return false;
        }
        builder.EmitConstU8(static_cast<uint8_t>(value));
        continue;
      }
      if (op == "const.u16") {
        uint64_t value = 0;
        if (inst.args.size() != 1 || !ParseUint(inst.args[0], &value)) {
          if (error) *error = "const.u16 expects value";
          return false;
        }
        builder.EmitConstU16(static_cast<uint16_t>(value));
        continue;
      }
      if (op == "const.u32") {
        uint64_t value = 0;
        if (inst.args.size() != 1 || !ParseUint(inst.args[0], &value)) {
          if (error) *error = "const.u32 expects value";
          return false;
        }
        builder.EmitConstU32(static_cast<uint32_t>(value));
        continue;
      }
      if (op == "const.u64") {
        uint64_t value = 0;
        if (inst.args.size() != 1 || !ParseUint(inst.args[0], &value)) {
          if (error) *error = "const.u64 expects value";
          return false;
        }
        builder.EmitConstU64(value);
        continue;
      }
      if (op == "const.f32") {
        double value = 0.0;
        if (inst.args.size() != 1 || !ParseFloat(inst.args[0], &value)) {
          if (error) *error = "const.f32 expects value";
          return false;
        }
        builder.EmitConstF32(static_cast<float>(value));
        continue;
      }
      if (op == "const.f64") {
        double value = 0.0;
        if (inst.args.size() != 1 || !ParseFloat(inst.args[0], &value)) {
          if (error) *error = "const.f64 expects value";
          return false;
        }
        builder.EmitConstF64(value);
        continue;
      }
      if (op == "const.bool") {
        uint64_t value = 0;
        if (inst.args.size() != 1 || !ParseUint(inst.args[0], &value)) {
          if (error) *error = "const.bool expects value";
          return false;
        }
        builder.EmitConstBool(value != 0);
        continue;
      }
      if (op == "const.char") {
        uint64_t value = 0;
        if (inst.args.size() != 1 || !ParseUint(inst.args[0], &value)) {
          if (error) *error = "const.char expects value";
          return false;
        }
        builder.EmitConstChar(static_cast<uint16_t>(value));
        continue;
      }
      if (op == "const.string") {
        uint64_t value = 0;
        if (inst.args.size() != 1 || !ParseUint(inst.args[0], &value)) {
          if (error) *error = "const.string expects const_id";
          return false;
        }
        builder.EmitConstString(static_cast<uint32_t>(value));
        continue;
      }
      if (op == "const.null") {
        builder.EmitConstNull();
        continue;
      }
      if (op == "add.i32") {
        builder.EmitAddI32();
        continue;
      }
      if (op == "sub.i32") {
        builder.EmitSubI32();
        continue;
      }
      if (op == "mul.i32") {
        builder.EmitMulI32();
        continue;
      }
      if (op == "div.i32") {
        builder.EmitDivI32();
        continue;
      }
      if (op == "mod.i32") {
        builder.EmitModI32();
        continue;
      }
      if (op == "add.i64") {
        builder.EmitAddI64();
        continue;
      }
      if (op == "sub.i64") {
        builder.EmitSubI64();
        continue;
      }
      if (op == "mul.i64") {
        builder.EmitMulI64();
        continue;
      }
      if (op == "div.i64") {
        builder.EmitDivI64();
        continue;
      }
      if (op == "mod.i64") {
        builder.EmitModI64();
        continue;
      }
      if (op == "add.f32") {
        builder.EmitAddF32();
        continue;
      }
      if (op == "sub.f32") {
        builder.EmitSubF32();
        continue;
      }
      if (op == "mul.f32") {
        builder.EmitMulF32();
        continue;
      }
      if (op == "div.f32") {
        builder.EmitDivF32();
        continue;
      }
      if (op == "add.f64") {
        builder.EmitAddF64();
        continue;
      }
      if (op == "sub.f64") {
        builder.EmitSubF64();
        continue;
      }
      if (op == "mul.f64") {
        builder.EmitMulF64();
        continue;
      }
      if (op == "div.f64") {
        builder.EmitDivF64();
        continue;
      }
      if (op == "add.u32") {
        builder.EmitAddU32();
        continue;
      }
      if (op == "sub.u32") {
        builder.EmitSubU32();
        continue;
      }
      if (op == "mul.u32") {
        builder.EmitMulU32();
        continue;
      }
      if (op == "div.u32") {
        builder.EmitDivU32();
        continue;
      }
      if (op == "mod.u32") {
        builder.EmitModU32();
        continue;
      }
      if (op == "add.u64") {
        builder.EmitAddU64();
        continue;
      }
      if (op == "sub.u64") {
        builder.EmitSubU64();
        continue;
      }
      if (op == "mul.u64") {
        builder.EmitMulU64();
        continue;
      }
      if (op == "div.u64") {
        builder.EmitDivU64();
        continue;
      }
      if (op == "mod.u64") {
        builder.EmitModU64();
        continue;
      }
      if (op == "and.i32") {
        builder.EmitAndI32();
        continue;
      }
      if (op == "or.i32") {
        builder.EmitOrI32();
        continue;
      }
      if (op == "xor.i32") {
        builder.EmitXorI32();
        continue;
      }
      if (op == "shl.i32") {
        builder.EmitShlI32();
        continue;
      }
      if (op == "shr.i32") {
        builder.EmitShrI32();
        continue;
      }
      if (op == "and.i64") {
        builder.EmitAndI64();
        continue;
      }
      if (op == "or.i64") {
        builder.EmitOrI64();
        continue;
      }
      if (op == "xor.i64") {
        builder.EmitXorI64();
        continue;
      }
      if (op == "shl.i64") {
        builder.EmitShlI64();
        continue;
      }
      if (op == "shr.i64") {
        builder.EmitShrI64();
        continue;
      }
      if (op == "neg.i32") {
        builder.EmitNegI32();
        continue;
      }
      if (op == "neg.i64") {
        builder.EmitNegI64();
        continue;
      }
      if (op == "inc.i32") {
        builder.EmitIncI32();
        continue;
      }
      if (op == "dec.i32") {
        builder.EmitDecI32();
        continue;
      }
      if (op == "inc.i64") {
        builder.EmitIncI64();
        continue;
      }
      if (op == "dec.i64") {
        builder.EmitDecI64();
        continue;
      }
      if (op == "cmp.eq.i32") {
        builder.EmitCmpEqI32();
        continue;
      }
      if (op == "cmp.ne.i32") {
        builder.EmitCmpNeI32();
        continue;
      }
      if (op == "cmp.lt.i32") {
        builder.EmitCmpLtI32();
        continue;
      }
      if (op == "cmp.le.i32") {
        builder.EmitCmpLeI32();
        continue;
      }
      if (op == "cmp.gt.i32") {
        builder.EmitCmpGtI32();
        continue;
      }
      if (op == "cmp.ge.i32") {
        builder.EmitCmpGeI32();
        continue;
      }
      if (op == "cmp.eq.i64") {
        builder.EmitCmpEqI64();
        continue;
      }
      if (op == "cmp.ne.i64") {
        builder.EmitCmpNeI64();
        continue;
      }
      if (op == "cmp.lt.i64") {
        builder.EmitCmpLtI64();
        continue;
      }
      if (op == "cmp.le.i64") {
        builder.EmitCmpLeI64();
        continue;
      }
      if (op == "cmp.gt.i64") {
        builder.EmitCmpGtI64();
        continue;
      }
      if (op == "cmp.ge.i64") {
        builder.EmitCmpGeI64();
        continue;
      }
      if (op == "cmp.eq.u32") {
        builder.EmitCmpEqU32();
        continue;
      }
      if (op == "cmp.ne.u32") {
        builder.EmitCmpNeU32();
        continue;
      }
      if (op == "cmp.lt.u32") {
        builder.EmitCmpLtU32();
        continue;
      }
      if (op == "cmp.le.u32") {
        builder.EmitCmpLeU32();
        continue;
      }
      if (op == "cmp.gt.u32") {
        builder.EmitCmpGtU32();
        continue;
      }
      if (op == "cmp.ge.u32") {
        builder.EmitCmpGeU32();
        continue;
      }
      if (op == "cmp.eq.u64") {
        builder.EmitCmpEqU64();
        continue;
      }
      if (op == "cmp.ne.u64") {
        builder.EmitCmpNeU64();
        continue;
      }
      if (op == "cmp.lt.u64") {
        builder.EmitCmpLtU64();
        continue;
      }
      if (op == "cmp.le.u64") {
        builder.EmitCmpLeU64();
        continue;
      }
      if (op == "cmp.gt.u64") {
        builder.EmitCmpGtU64();
        continue;
      }
      if (op == "cmp.ge.u64") {
        builder.EmitCmpGeU64();
        continue;
      }
      if (op == "cmp.eq.f32") {
        builder.EmitCmpEqF32();
        continue;
      }
      if (op == "cmp.ne.f32") {
        builder.EmitCmpNeF32();
        continue;
      }
      if (op == "cmp.lt.f32") {
        builder.EmitCmpLtF32();
        continue;
      }
      if (op == "cmp.le.f32") {
        builder.EmitCmpLeF32();
        continue;
      }
      if (op == "cmp.gt.f32") {
        builder.EmitCmpGtF32();
        continue;
      }
      if (op == "cmp.ge.f32") {
        builder.EmitCmpGeF32();
        continue;
      }
      if (op == "cmp.eq.f64") {
        builder.EmitCmpEqF64();
        continue;
      }
      if (op == "cmp.ne.f64") {
        builder.EmitCmpNeF64();
        continue;
      }
      if (op == "cmp.lt.f64") {
        builder.EmitCmpLtF64();
        continue;
      }
      if (op == "cmp.le.f64") {
        builder.EmitCmpLeF64();
        continue;
      }
      if (op == "cmp.gt.f64") {
        builder.EmitCmpGtF64();
        continue;
      }
      if (op == "cmp.ge.f64") {
        builder.EmitCmpGeF64();
        continue;
      }
      if (op == "bool.not") {
        builder.EmitBoolNot();
        continue;
      }
      if (op == "bool.and") {
        builder.EmitBoolAnd();
        continue;
      }
      if (op == "bool.or") {
        builder.EmitBoolOr();
        continue;
      }
      if (op == "jmp") {
        if (inst.args.size() != 1) {
          if (error) *error = "jmp expects label";
          return false;
        }
        auto it = labels.find(inst.args[0]);
        if (it == labels.end()) {
          if (error) *error = "unknown label: " + inst.args[0];
          return false;
        }
        builder.EmitJmp(it->second);
        continue;
      }
      if (op == "jmp.true") {
        if (inst.args.size() != 1) {
          if (error) *error = "jmp.true expects label";
          return false;
        }
        auto it = labels.find(inst.args[0]);
        if (it == labels.end()) {
          if (error) *error = "unknown label: " + inst.args[0];
          return false;
        }
        builder.EmitJmpTrue(it->second);
        continue;
      }
      if (op == "jmp.false") {
        if (inst.args.size() != 1) {
          if (error) *error = "jmp.false expects label";
          return false;
        }
        auto it = labels.find(inst.args[0]);
        if (it == labels.end()) {
          if (error) *error = "unknown label: " + inst.args[0];
          return false;
        }
        builder.EmitJmpFalse(it->second);
        continue;
      }
      if (op == "jmptable") {
        if (inst.args.size() < 2) {
          if (error) *error = "jmptable expects default and cases";
          return false;
        }
        auto def_it = labels.find(inst.args[0]);
        if (def_it == labels.end()) {
          if (error) *error = "unknown label: " + inst.args[0];
          return false;
        }
        IrLabel def = def_it->second;
        std::vector<IrLabel> cases;
        for (size_t i = 1; i < inst.args.size(); ++i) {
          auto it = labels.find(inst.args[i]);
          if (it == labels.end()) {
            if (error) *error = "unknown label: " + inst.args[i];
            return false;
          }
          cases.push_back(it->second);
        }
        builder.EmitJmpTable(cases, def);
        continue;
      }
      if (op == "call") {
        if (inst.args.size() != 2) {
          if (error) *error = "call expects func_id arg_count";
          return false;
        }
        uint64_t func_id = 0;
        uint64_t arg_count = 0;
        if (!ParseUint(inst.args[0], &func_id) || !ParseUint(inst.args[1], &arg_count)) {
          if (error) *error = "call expects numeric args";
          return false;
        }
        builder.EmitCall(static_cast<uint32_t>(func_id), static_cast<uint8_t>(arg_count));
        continue;
      }
      if (op == "call.indirect") {
        if (inst.args.size() != 2) {
          if (error) *error = "call.indirect expects sig_id arg_count";
          return false;
        }
        uint64_t sig_id = 0;
        uint64_t arg_count = 0;
        if (!ParseUint(inst.args[0], &sig_id) || !ParseUint(inst.args[1], &arg_count)) {
          if (error) *error = "call.indirect expects numeric args";
          return false;
        }
        builder.EmitCallIndirect(static_cast<uint32_t>(sig_id), static_cast<uint8_t>(arg_count));
        continue;
      }
      if (op == "tailcall") {
        if (inst.args.size() != 2) {
          if (error) *error = "tailcall expects func_id arg_count";
          return false;
        }
        uint64_t func_id = 0;
        uint64_t arg_count = 0;
        if (!ParseUint(inst.args[0], &func_id) || !ParseUint(inst.args[1], &arg_count)) {
          if (error) *error = "tailcall expects numeric args";
          return false;
        }
        builder.EmitTailCall(static_cast<uint32_t>(func_id), static_cast<uint8_t>(arg_count));
        continue;
      }
      if (op == "conv.i32.i64") {
        builder.EmitConvI32ToI64();
        continue;
      }
      if (op == "conv.i64.i32") {
        builder.EmitConvI64ToI32();
        continue;
      }
      if (op == "conv.i32.f32") {
        builder.EmitConvI32ToF32();
        continue;
      }
      if (op == "conv.i32.f64") {
        builder.EmitConvI32ToF64();
        continue;
      }
      if (op == "conv.f32.i32") {
        builder.EmitConvF32ToI32();
        continue;
      }
      if (op == "conv.f64.i32") {
        builder.EmitConvF64ToI32();
        continue;
      }
      if (op == "conv.f32.f64") {
        builder.EmitConvF32ToF64();
        continue;
      }
      if (op == "conv.f64.f32") {
        builder.EmitConvF64ToF32();
        continue;
      }
      if (op == "ldloc" || op == "load.local") {
        uint64_t index = 0;
        if (inst.args.size() != 1 || !ParseUint(inst.args[0], &index)) {
          if (error) *error = "ldloc expects index";
          return false;
        }
        builder.EmitLoadLocal(static_cast<uint32_t>(index));
        continue;
      }
      if (op == "stloc" || op == "store.local") {
        uint64_t index = 0;
        if (inst.args.size() != 1 || !ParseUint(inst.args[0], &index)) {
          if (error) *error = "stloc expects index";
          return false;
        }
        builder.EmitStoreLocal(static_cast<uint32_t>(index));
        continue;
      }
      if (op == "callcheck") {
        builder.EmitCallCheck();
        continue;
      }
      if (op == "intrinsic") {
        uint64_t id = 0;
        if (inst.args.size() != 1 || !ParseUint(inst.args[0], &id)) {
          if (error) *error = "intrinsic expects id";
          return false;
        }
        builder.EmitIntrinsic(static_cast<uint32_t>(id));
        continue;
      }
      if (op == "syscall") {
        uint64_t id = 0;
        if (inst.args.size() != 1 || !ParseUint(inst.args[0], &id)) {
          if (error) *error = "syscall expects id";
          return false;
        }
        builder.EmitSysCall(static_cast<uint32_t>(id));
        continue;
      }
      if (op == "newobj") {
        uint64_t type_id = 0;
        if (inst.args.size() != 1 || !ParseUint(inst.args[0], &type_id)) {
          if (error) *error = "newobj expects type_id";
          return false;
        }
        builder.EmitNewObject(static_cast<uint32_t>(type_id));
        continue;
      }
      if (op == "ldfld") {
        uint64_t field_id = 0;
        if (inst.args.size() != 1 || !ParseUint(inst.args[0], &field_id)) {
          if (error) *error = "ldfld expects field_id";
          return false;
        }
        builder.EmitLoadField(static_cast<uint32_t>(field_id));
        continue;
      }
      if (op == "stfld") {
        uint64_t field_id = 0;
        if (inst.args.size() != 1 || !ParseUint(inst.args[0], &field_id)) {
          if (error) *error = "stfld expects field_id";
          return false;
        }
        builder.EmitStoreField(static_cast<uint32_t>(field_id));
        continue;
      }
      if (op == "typeof") {
        builder.EmitTypeOf();
        continue;
      }
      if (op == "isnull") {
        builder.EmitIsNull();
        continue;
      }
      if (op == "ref.eq") {
        builder.EmitRefEq();
        continue;
      }
      if (op == "ref.ne") {
        builder.EmitRefNe();
        continue;
      }
      if (op == "newclosure") {
        uint64_t method_id = 0;
        uint64_t upvalues = 0;
        if (inst.args.size() != 2 || !ParseUint(inst.args[0], &method_id) ||
            !ParseUint(inst.args[1], &upvalues)) {
          if (error) *error = "newclosure expects method_id upvalue_count";
          return false;
        }
        builder.EmitNewClosure(static_cast<uint32_t>(method_id),
                               static_cast<uint8_t>(upvalues));
        continue;
      }
      if (op == "newarray") {
        uint64_t type_id = 0;
        uint64_t length = 0;
        if (inst.args.size() != 2 || !ParseUint(inst.args[0], &type_id) ||
            !ParseUint(inst.args[1], &length)) {
          if (error) *error = "newarray expects type_id length";
          return false;
        }
        builder.EmitNewArray(static_cast<uint32_t>(type_id),
                             static_cast<uint32_t>(length));
        continue;
      }
      if (op == "array.len") {
        builder.EmitArrayLen();
        continue;
      }
      if (op == "array.get.i32") {
        builder.EmitArrayGetI32();
        continue;
      }
      if (op == "array.set.i32") {
        builder.EmitArraySetI32();
        continue;
      }
      if (op == "array.get.i64") {
        builder.EmitArrayGetI64();
        continue;
      }
      if (op == "array.set.i64") {
        builder.EmitArraySetI64();
        continue;
      }
      if (op == "array.get.f32") {
        builder.EmitArrayGetF32();
        continue;
      }
      if (op == "array.set.f32") {
        builder.EmitArraySetF32();
        continue;
      }
      if (op == "array.get.f64") {
        builder.EmitArrayGetF64();
        continue;
      }
      if (op == "array.set.f64") {
        builder.EmitArraySetF64();
        continue;
      }
      if (op == "array.get.ref") {
        builder.EmitArrayGetRef();
        continue;
      }
      if (op == "array.set.ref") {
        builder.EmitArraySetRef();
        continue;
      }
      if (op == "newlist") {
        uint64_t type_id = 0;
        uint64_t cap = 0;
        if (inst.args.size() != 2 || !ParseUint(inst.args[0], &type_id) ||
            !ParseUint(inst.args[1], &cap)) {
          if (error) *error = "newlist expects type_id capacity";
          return false;
        }
        builder.EmitNewList(static_cast<uint32_t>(type_id),
                            static_cast<uint32_t>(cap));
        continue;
      }
      if (op == "list.len") {
        builder.EmitListLen();
        continue;
      }
      if (op == "list.get.i32") {
        builder.EmitListGetI32();
        continue;
      }
      if (op == "list.set.i32") {
        builder.EmitListSetI32();
        continue;
      }
      if (op == "list.push.i32") {
        builder.EmitListPushI32();
        continue;
      }
      if (op == "list.pop.i32") {
        builder.EmitListPopI32();
        continue;
      }
      if (op == "list.get.i64") {
        builder.EmitListGetI64();
        continue;
      }
      if (op == "list.set.i64") {
        builder.EmitListSetI64();
        continue;
      }
      if (op == "list.push.i64") {
        builder.EmitListPushI64();
        continue;
      }
      if (op == "list.pop.i64") {
        builder.EmitListPopI64();
        continue;
      }
      if (op == "list.get.f32") {
        builder.EmitListGetF32();
        continue;
      }
      if (op == "list.set.f32") {
        builder.EmitListSetF32();
        continue;
      }
      if (op == "list.push.f32") {
        builder.EmitListPushF32();
        continue;
      }
      if (op == "list.pop.f32") {
        builder.EmitListPopF32();
        continue;
      }
      if (op == "list.get.f64") {
        builder.EmitListGetF64();
        continue;
      }
      if (op == "list.set.f64") {
        builder.EmitListSetF64();
        continue;
      }
      if (op == "list.push.f64") {
        builder.EmitListPushF64();
        continue;
      }
      if (op == "list.pop.f64") {
        builder.EmitListPopF64();
        continue;
      }
      if (op == "list.get.ref") {
        builder.EmitListGetRef();
        continue;
      }
      if (op == "list.set.ref") {
        builder.EmitListSetRef();
        continue;
      }
      if (op == "list.push.ref") {
        builder.EmitListPushRef();
        continue;
      }
      if (op == "list.pop.ref") {
        builder.EmitListPopRef();
        continue;
      }
      if (op == "list.insert.i32") {
        builder.EmitListInsertI32();
        continue;
      }
      if (op == "list.remove.i32") {
        builder.EmitListRemoveI32();
        continue;
      }
      if (op == "list.clear") {
        builder.EmitListClear();
        continue;
      }
      if (op == "string.len") {
        builder.EmitStringLen();
        continue;
      }
      if (op == "string.concat") {
        builder.EmitStringConcat();
        continue;
      }
      if (op == "string.get.char") {
        builder.EmitStringGetChar();
        continue;
      }
      if (op == "string.slice") {
        builder.EmitStringSlice();
        continue;
      }
      if (op == "ldglob" || op == "load.global") {
        uint64_t index = 0;
        if (inst.args.size() != 1 || !ParseUint(inst.args[0], &index)) {
          if (error) *error = "ldglob expects index";
          return false;
        }
        builder.EmitLoadGlobal(static_cast<uint32_t>(index));
        continue;
      }
      if (op == "stglob" || op == "store.global") {
        uint64_t index = 0;
        if (inst.args.size() != 1 || !ParseUint(inst.args[0], &index)) {
          if (error) *error = "stglob expects index";
          return false;
        }
        builder.EmitStoreGlobal(static_cast<uint32_t>(index));
        continue;
      }
      if (op == "ldupv" || op == "load.upvalue") {
        uint64_t index = 0;
        if (inst.args.size() != 1 || !ParseUint(inst.args[0], &index)) {
          if (error) *error = "ldupv expects index";
          return false;
        }
        builder.EmitLoadUpvalue(static_cast<uint32_t>(index));
        continue;
      }
      if (op == "stupv" || op == "store.upvalue") {
        uint64_t index = 0;
        if (inst.args.size() != 1 || !ParseUint(inst.args[0], &index)) {
          if (error) *error = "stupv expects index";
          return false;
        }
        builder.EmitStoreUpvalue(static_cast<uint32_t>(index));
        continue;
      }

      if (error) *error = "unknown op: " + inst.op;
      return false;
    }

    std::vector<uint8_t> code;
    if (!builder.Finish(&code, error)) return false;
    simplevm::ir::IrFunction out_fn;
    out_fn.code = std::move(code);
    out_fn.local_count = fn.locals;
    out_fn.stack_max = fn.stack_max;
    out_fn.sig_id = fn.sig_id;
    out->functions.push_back(std::move(out_fn));
  }

  return true;
}

} // namespace simplevm::irtext
