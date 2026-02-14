#include <string>

std::string ResolveImportModule(const std::string& module) {
  if (module == "core_os") return "core.os";
  if (module == "core_io") return "core.io";
  if (module == "core_fs") return "core.fs";
  if (module == "core_log") return "core.log";
  if (module == "core_dl") return "core.dl";
  return module;
}
