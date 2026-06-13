#include "test_utils.h"

#include <vector>

#include "interpreter/frames.h"
#include "interpreter/stack.h"

namespace Simple::VM::Tests {
namespace {

bool VmSplitInterpreterStackAndFrames() {
  std::vector<Simple::VM::Interpreter::Slot> stack;
  Simple::VM::Interpreter::Push(stack, 11);
  Simple::VM::Interpreter::Push(stack, 22);
  if (Simple::VM::Interpreter::Peek(stack) != 22) return false;
  if (Simple::VM::Interpreter::Pop(stack) != 22) return false;
  if (Simple::VM::Interpreter::Pop(stack) != 11) return false;

  const auto frame = Simple::VM::Interpreter::MakeFrame(3, 9, 4, 17);
  return frame.func_index == 3 && frame.return_pc == 9 &&
         frame.stack_base == 4 && frame.closure_ref == 17;
}

const TestCase kVmInterpreterTests[] = {
  {"vm_split_interpreter_stack_and_frames", VmSplitInterpreterStackAndFrames},
};

const TestSection kVmInterpreterSections[] = {
  {"vm_interpreter", kVmInterpreterTests, sizeof(kVmInterpreterTests) / sizeof(kVmInterpreterTests[0])},
};

} // namespace

const TestSection* GetVmInterpreterSections(size_t* count) {
  if (count) *count = sizeof(kVmInterpreterSections) / sizeof(kVmInterpreterSections[0]);
  return kVmInterpreterSections;
}

} // namespace Simple::VM::Tests
