#include "RAST/reserved_resolution.h"

#include "RAST/import_graph.h"
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
  else if (canonical_module == "Random") *out = "System.random";
  else if (canonical_module == "Env") *out = "System.env";
  else if (canonical_module == "Path") *out = "System.path";
  else if (canonical_module == "FS") *out = "System.fs";
  else if (canonical_module == "Json") *out = "System.json";
  else if (canonical_module == "Buffer") *out = "System.buffer";
  else if (canonical_module == "Log") *out = "System.log";
  else return false;
  return true;
}

bool IsIoPrintName(const std::string& name) {
  return name == "print" || name == "println";
}

bool IsReservedModuleFunction(const std::string& canonical_module, const std::string& member) {
  std::string native_module;
  if (NativeModuleNameForReserved(canonical_module, &native_module) &&
      ReservedNativeRegistry().Find(native_module, member)) {
    return true;
  }
  if (canonical_module == "IO") {
    return member == "print" || member == "println" || member == "buffer_new" ||
           member == "buffer_len" || member == "buffer_fill" || member == "buffer_copy";
  }
  if (canonical_module == "Math") {
    return member == "abs" || member == "min" || member == "max" || member == "sqrt";
  }
  if (canonical_module == "Time") return member == "mono_ns" || member == "wall_ns" || member == "formatWallNs";
  if (canonical_module == "DL") {
    return member == "open" || member == "sym" || member == "close" ||
           member == "last_error" || member == "call_i32" || member == "call_i64" ||
           member == "call_f32" || member == "call_f64" || member == "call_str0";
  }
  if (canonical_module == "OS") {
    return member == "args_count" || member == "args_get" || member == "env_get" ||
           member == "cwd_get" || member == "time_mono_ns" || member == "time_wall_ns" ||
           member == "formatWallNs" ||
           member == "sleep_ms";
  }
  if (canonical_module == "Thread") {
    return member == "sleep" || member == "yield" || member == "hardwareConcurrency";
  }
  if (canonical_module == "Random") {
    return member == "seed" || member == "i32" || member == "range" || member == "f64";
  }
  if (canonical_module == "Env") {
    return member == "argsCount" || member == "arg" || member == "get" || member == "set" ||
           member == "platform" || member == "arch" || member == "exePath";
  }
  if (canonical_module == "Path") {
    return member == "join" || member == "dirname" || member == "basename" || member == "ext" ||
           member == "normalize" || member == "exists" || member == "isFile" || member == "isDir";
  }
  if (canonical_module == "FS") {
    return member == "readText" || member == "writeText" || member == "readBytes" || member == "writeBytes" ||
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
  if (canonical_module == "Json") return member == "parse" || member == "stringify" || member == "free";
  if (canonical_module == "Buffer") {
    return member == "new" || member == "len" || member == "readU16LE" || member == "readU32LE" ||
           member == "writeU16LE" || member == "writeU32LE" || member == "slice" || member == "copy";
  }
  if (canonical_module == "Log") return member == "log" || member == "info" || member == "warn" || member == "error" || member == "setLevel" || member == "setFile";
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
    const auto& root = base.children[0];
    if (root.kind == Simple::Lang::AST::ExprKind::Identifier && root.text == "System") {
      *out = root.text + "." + base.text;
      return true;
    }
  }
  return false;
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
