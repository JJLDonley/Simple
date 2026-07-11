#include "RAST/reserved_resolution.h"

#include <algorithm>

#include "RAST/import_graph.h"
#include "lang_reserved.h"
#include "native/registry.h"

namespace Simple::Lang::RAST {

namespace {

const Simple::VM::Native::NativeRegistry& ReservedNativeRegistry() {
  static const Simple::VM::Native::NativeRegistry registry = Simple::VM::Native::BuildDefaultRegistry();
  return registry;
}

} // namespace

bool NativeModuleNameForReserved(const std::string& canonical_module, std::string* out) {
  if (!out) return false;
  if (canonical_module == "IO") *out = "System.io";
  else if (canonical_module == "DL") *out = "System.dl";
  else if (canonical_module == "OS") *out = "System.os";
  else if (canonical_module == "Thread") *out = "System.thread";
  else if (canonical_module == "SystemRandom" || canonical_module == "StandardRandom") *out = "System.random";
  else if (canonical_module == "Env") *out = "System.env";
  else if (canonical_module == "Path") *out = "System.path";
  else if (canonical_module == "FS") *out = "System.fs";
  else if (canonical_module == "SystemJson") *out = "System.json";
  else if (IsSystemBufferLikeCanonical(canonical_module) ||
           canonical_module == ToCanonicalName(StandardModule::Bytes)) *out = std::string(ToNativeModule(SystemModule::Buffer));
  else if (canonical_module == "SystemLog" || canonical_module == "StandardLog") *out = "System.log";
  else return false;
  return true;
}

namespace {

void AddNativeReservedMembers(const std::string& canonical_module, std::vector<std::string>* out) {
  if (!out) return;
  std::string native_module;
  if (!NativeModuleNameForReserved(canonical_module, &native_module)) return;
  for (const auto& spec : ReservedNativeRegistry().Functions()) {
    if (spec.module_name != native_module) continue;
    if (std::find(out->begin(), out->end(), spec.symbol_name) == out->end()) {
      out->push_back(spec.symbol_name);
    }
  }
}

} // namespace

std::vector<std::string> ReservedModuleMembers(const std::string& canonical_module) {
  std::vector<std::string> out;
  if (canonical_module == "StandardIO") return {"print", "println"};
  if (canonical_module == "IO") {
    out = {"buffer_new", "buffer_len", "buffer_fill", "buffer_copy"};
    AddNativeReservedMembers(canonical_module, &out);
    return out;
  }
  if (canonical_module == "Math") return {"abs", "min", "max", "sqrt", "PI"};
  if (canonical_module == "Time") return {"mono_ns", "wall_ns"};
  if (canonical_module == "StandardTime") return {"mono_ns", "wall_ns", "formatWallNs"};
  if (canonical_module == "DL") {
    out = {"open", "sym", "close", "last_error", "call_i32", "call_i64", "call_f32", "call_f64",
           "call_str0", "supported"};
    AddNativeReservedMembers(canonical_module, &out);
    return out;
  }
  if (canonical_module == "OS") {
    return {"platform", "arch", "isLinux", "isMacos", "isWindows", "pid", "cpuCount",
            "pageSize", "exit", "sleepMs"};
  }
  if (canonical_module == "Thread") {
    out = {"sleep", "yield", "hardwareConcurrency"};
    AddNativeReservedMembers(canonical_module, &out);
    return out;
  }
  if (canonical_module == "SystemRandom") return {"seed", "i32", "i64", "f64", "fillBytes"};
  if (canonical_module == "StandardRandom") return {"seed", "i32", "i64", "range", "f64"};
  if (canonical_module == "Env") return {"argsCount", "arg", "get", "set", "unset", "exePath"};
  if (canonical_module == "StandardPath") return {"join", "dirname", "basename", "ext", "stem", "normalize"};
  if (canonical_module == "Path") {
    return {"separator", "delimiter", "isAbsolute", "join", "dirname", "basename", "ext", "stem", "normalize"};
  }
  if (canonical_module == "StandardFS") {
    return {"readText", "writeText", "readBytes", "writeBytes", "exists", "isFile", "isDir",
            "copy", "remove", "mkdir", "mkdirAll", "listDir", "cwd", "setCwd"};
  }
  if (canonical_module == "FS") {
    out = {"readText", "writeText", "readBytes", "writeBytes", "exists", "isFile", "isDir",
           "copy", "remove", "mkdir", "mkdirAll", "listDir", "cwd", "setCwd"};
    AddNativeReservedMembers(canonical_module, &out);
    return out;
  }
  if (canonical_module == "Channel") {
    out = {"newI32", "sendI32", "trySendI32", "recvI32", "tryRecvI32", "pendingI32",
           "newI64", "sendI64", "trySendI64", "recvI64", "tryRecvI64", "pendingI64",
           "newF32", "sendF32", "trySendF32", "recvF32", "tryRecvF32", "pendingF32",
           "newF64", "sendF64", "trySendF64", "recvF64", "tryRecvF64", "pendingF64",
           "newBool", "sendBool", "trySendBool", "recvBool", "tryRecvBool", "pendingBool",
           "newString", "sendString", "trySendString", "recvString", "tryRecvString", "pendingString",
           "newBytes", "sendBytes", "trySendBytes", "recvBytes", "tryRecvBytes", "pendingBytes", "close"};
    AddNativeReservedMembers(canonical_module, &out);
    return out;
  }
  if (canonical_module == "File") return {"open", "close", "read", "write"};
  if (canonical_module == "SystemJson") return {"parse", "stringify", "free"};
  if (IsSystemBufferLikeCanonical(canonical_module)) {
    const auto names = SystemBufferMemberNames();
    for (std::string_view name : names) out.emplace_back(name);
    return out;
  }
  if (canonical_module == ToCanonicalName(StandardModule::Buffer)) return {};
  if (canonical_module == ToCanonicalName(StandardModule::Bytes)) return {"new", "slice"};
  if (canonical_module == "SystemLog") return {"log", "setLevel", "setFile", "flush"};
  if (canonical_module == "StandardLog") return {"info", "warn", "error", "setLevel", "setFile"};
  return out;
}

bool IsIoPrintName(const std::string& name) {
  return name == "print" || name == "println";
}

bool GetReservedModuleVarType(const std::string& canonical_module,
                              const std::string& member,
                              Simple::Lang::AST::TypeRef* out) {
  auto set_simple = [out](const std::string& name) {
    if (out) {
      *out = Simple::Lang::AST::TypeRef{};
      out->name = name;
    }
    return true;
  };
  if (canonical_module == "Math" && member == "PI") return set_simple("f64");
  if (canonical_module == "DL" && member == "supported") return set_simple("bool");
  return false;
}

bool IsReservedModuleEnabled(const std::unordered_set<std::string>& reserved_imports,
                             const std::unordered_map<std::string, std::string>& reserved_import_aliases,
                             const std::string& name) {
  if (reserved_import_aliases.find(name) != reserved_import_aliases.end()) return true;
  if (reserved_imports.find(name) != reserved_imports.end()) return true;
  std::string canonical;
  if (!CanonicalizeReservedImportPath(name, &canonical)) return false;
  return reserved_imports.find(canonical) != reserved_imports.end();
}

bool ResolveReservedModuleName(const std::unordered_set<std::string>& reserved_imports,
                               const std::unordered_map<std::string, std::string>& reserved_import_aliases,
                               const std::string& name,
                               std::string* out) {
  if (!out) return false;
  std::string canonical;
  if (CanonicalizeReservedImportPath(name, &canonical) &&
      reserved_imports.find(canonical) != reserved_imports.end()) {
    *out = canonical;
    return true;
  }
  auto it = reserved_import_aliases.find(name);
  if (it != reserved_import_aliases.end()) {
    *out = it->second;
    return true;
  }
  return false;
}

bool IsReservedModuleFunction(const std::string& canonical_module, const std::string& member) {
  if (canonical_module == "SystemRandom") return member == "seed" || member == "i32" || member == "i64" || member == "f64" || member == "fillBytes";
  if (canonical_module == "StandardRandom") return member == "seed" || member == "i32" || member == "i64" || member == "range" || member == "f64";
  if (canonical_module == "SystemLog") return member == "log" || member == "setLevel" || member == "setFile" || member == "flush";
  if (canonical_module == "StandardLog") return member == "info" || member == "warn" || member == "error" || member == "setLevel" || member == "setFile";
  std::string native_module;
  if (NativeModuleNameForReserved(canonical_module, &native_module) &&
      ReservedNativeRegistry().Find(native_module, member)) {
    return true;
  }
  if (canonical_module == "StandardIO") return member == "print" || member == "println";
  if (canonical_module == "IO") {
    return member == "buffer_new" || member == "buffer_len" || member == "buffer_fill" || member == "buffer_copy";
  }
  if (canonical_module == "Math") {
    return member == "abs" || member == "min" || member == "max" || member == "sqrt";
  }
  if (canonical_module == "Time") return member == "mono_ns" || member == "wall_ns";
  if (canonical_module == "StandardTime") return member == "mono_ns" || member == "wall_ns" || member == "formatWallNs";
  if (canonical_module == "DL") {
    return member == "open" || member == "sym" || member == "close" ||
           member == "last_error" || member == "call_i32" || member == "call_i64" ||
           member == "call_f32" || member == "call_f64" || member == "call_str0";
  }
  if (canonical_module == "OS") {
    return member == "platform" || member == "arch" || member == "isLinux" || member == "isMacos" ||
           member == "isWindows" || member == "pid" || member == "cpuCount" || member == "pageSize" ||
           member == "exit" || member == "sleepMs";
  }
  if (canonical_module == "Thread") {
    return member == "sleep" || member == "yield" || member == "hardwareConcurrency";
  }
  if (canonical_module == "Env") {
    return member == "argsCount" || member == "arg" || member == "get" || member == "set" ||
           member == "unset" || member == "exePath";
  }
  if (canonical_module == "StandardPath") {
    return member == "join" || member == "dirname" || member == "basename" || member == "ext" ||
           member == "stem" || member == "normalize";
  }
  if (canonical_module == "Path") {
    return member == "separator" || member == "delimiter" || member == "isAbsolute" ||
           member == "join" || member == "dirname" || member == "basename" || member == "ext" ||
           member == "stem" || member == "normalize";
  }
  if (canonical_module == "StandardFS" || canonical_module == "FS") {
    return member == "readText" || member == "writeText" || member == "readBytes" || member == "writeBytes" ||
           member == "exists" || member == "isFile" || member == "isDir" ||
           member == "copy" || member == "remove" || member == "mkdir" || member == "mkdirAll" ||
           member == "listDir" || member == "cwd" || member == "setCwd";
  }
  if (canonical_module == "Channel") {
    return member == "newI32" || member == "sendI32" || member == "trySendI32" || member == "recvI32" || member == "tryRecvI32" || member == "pendingI32" ||
           member == "newI64" || member == "sendI64" || member == "trySendI64" || member == "recvI64" || member == "tryRecvI64" || member == "pendingI64" ||
           member == "newF32" || member == "sendF32" || member == "trySendF32" || member == "recvF32" || member == "tryRecvF32" || member == "pendingF32" ||
           member == "newF64" || member == "sendF64" || member == "trySendF64" || member == "recvF64" || member == "tryRecvF64" || member == "pendingF64" ||
           member == "newBool" || member == "sendBool" || member == "trySendBool" || member == "recvBool" || member == "tryRecvBool" || member == "pendingBool" ||
           member == "newString" || member == "sendString" || member == "trySendString" || member == "recvString" || member == "tryRecvString" || member == "pendingString" ||
           member == "newBytes" || member == "sendBytes" || member == "trySendBytes" || member == "recvBytes" || member == "tryRecvBytes" || member == "pendingBytes" ||
           member == "close";
  }
  if (canonical_module == "File") return member == "open" || member == "close" || member == "read" || member == "write";
  if (canonical_module == "SystemJson") return member == "parse" || member == "stringify" || member == "free";
  if (IsSystemBufferLikeCanonical(canonical_module)) return IsSystemBufferMember(member);
  if (canonical_module == ToCanonicalName(StandardModule::Buffer)) return false;
  if (canonical_module == ToCanonicalName(StandardModule::Bytes)) return member == "new" || member == "slice";
  return false;
}

std::string NormalizeDlMemberName(const std::string& name) {
  if (name == "Open") return "open";
  if (name == "Sym") return "sym";
  if (name == "Close") return "close";
  if (name == "LastError") return "last_error";
  if (name == "CallI32") return "call_i32";
  if (name == "CallI64") return "call_i64";
  if (name == "CallF32") return "call_f32";
  if (name == "CallF64") return "call_f64";
  if (name == "CallStr0") return "call_str0";
  return name;
}

bool GetModuleNameFromExpr(const Simple::Lang::AST::Expr& base, std::string* out) {
  if (!out) return false;
  if (base.kind == Simple::Lang::AST::ExprKind::Identifier) {
    *out = base.text;
    return true;
  }
  if (base.kind == Simple::Lang::AST::ExprKind::Member && base.op == "." && !base.children.empty()) {
    std::string prefix;
    if (!GetModuleNameFromExpr(base.children[0], &prefix)) return false;
    *out = prefix + "." + base.text;
    return true;
  }
  return false;
}

bool IsIoPrintCallExpr(const Simple::Lang::AST::Expr& callee,
                       const std::unordered_set<std::string>& reserved_imports,
                       const std::unordered_map<std::string, std::string>& reserved_import_aliases) {
  if (callee.kind != Simple::Lang::AST::ExprKind::Member || callee.op != "." || callee.children.empty()) {
    return false;
  }
  if (!IsIoPrintName(callee.text)) return false;
  std::string module_name;
  if (!GetModuleNameFromExpr(callee.children[0], &module_name)) return false;
  std::string resolved;
  return ResolveReservedModuleName(reserved_imports, reserved_import_aliases, module_name, &resolved) &&
         resolved == "StandardIO";
}

bool IsCoreDlOpenCallExpr(const Simple::Lang::AST::Expr& expr,
                          const std::unordered_set<std::string>& reserved_imports,
                          const std::unordered_map<std::string, std::string>& reserved_import_aliases) {
  if (expr.kind != Simple::Lang::AST::ExprKind::Call || expr.children.empty()) return false;
  const auto& callee = expr.children[0];
  if (callee.kind != Simple::Lang::AST::ExprKind::Member || callee.op != "." || callee.children.empty()) {
    return false;
  }
  std::string module_name;
  if (!GetModuleNameFromExpr(callee.children[0], &module_name)) return false;
  if (!IsReservedModuleEnabled(reserved_imports, reserved_import_aliases, module_name)) return false;
  std::string resolved;
  if (!ResolveReservedModuleName(reserved_imports, reserved_import_aliases, module_name, &resolved)) return false;
  return resolved == "DL" && NormalizeDlMemberName(callee.text) == "open";
}

bool GetDlOpenManifestModule(
    const Simple::Lang::AST::Expr& expr,
    const std::unordered_set<std::string>& reserved_imports,
    const std::unordered_map<std::string, std::string>& reserved_import_aliases,
    const std::unordered_map<std::string, std::unordered_map<std::string, const Simple::Lang::AST::ExternDecl*>>& externs_by_module,
    std::string* out_module) {
  if (!out_module) return false;
  if (!IsCoreDlOpenCallExpr(expr, reserved_imports, reserved_import_aliases)) return false;
  if (expr.args.size() != 2) return false;
  if (expr.args[1].kind != Simple::Lang::AST::ExprKind::Identifier) return false;
  const std::string& module = expr.args[1].text;
  auto mod_it = externs_by_module.find(module);
  if (mod_it == externs_by_module.end() || mod_it->second.empty()) return false;
  *out_module = module;
  return true;
}

bool GetDlOpenManifestModule(const ResolvedProgram* program,
                             const Simple::Lang::AST::Expr& expr,
                             std::string* out_module) {
  if (!program || !out_module || expr.kind != Simple::Lang::AST::ExprKind::Call || expr.children.empty()) return false;
  const auto& callee = expr.children[0];
  if (callee.kind != Simple::Lang::AST::ExprKind::Member || callee.op != "." || callee.children.empty()) return false;
  if (NormalizeDlMemberName(callee.text) != "open") return false;
  std::string module_alias;
  if (!GetModuleNameFromExpr(callee.children[0], &module_alias)) return false;
  std::string canonical;
  if (!ResolveReservedImportAlias(program->program, module_alias, &canonical) || canonical != "DL") return false;
  if (expr.args.size() != 2 || expr.args[1].kind != Simple::Lang::AST::ExprKind::Identifier) return false;
  const std::string& manifest_module = expr.args[1].text;
  const std::string prefix = manifest_module + ".";
  for (const auto& entry : program->by_qualified_name) {
    if (entry.first.rfind(prefix, 0) == 0 && program->symbols[entry.second].kind == SymbolKind::Extern) {
      *out_module = manifest_module;
      return true;
    }
  }
  return false;
}

} // namespace Simple::Lang::RAST
