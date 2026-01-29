#ifndef SIMPLE_VM_H
#define SIMPLE_VM_H

#include <cstdint>
#include <string>
#include <vector>

#include "sbc_types.h"

namespace simplevm {

enum class ExecStatus {
  Ok,
  Halted,
  Trapped,
  BadModule,
};

struct ExecResult {
  ExecStatus status = ExecStatus::Ok;
  std::string error;
  int32_t exit_code = 0;
};

ExecResult ExecuteModule(const SbcModule& module);
ExecResult ExecuteModule(const SbcModule& module, bool verify);

} // namespace simplevm

#endif // SIMPLE_VM_H
