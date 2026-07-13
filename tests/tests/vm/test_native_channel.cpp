#include "test_utils.h"

#include "native/channel.h"

namespace Simple::VM::Tests {
namespace {

bool VmSplitNativeChannelSendReceivePending() {
  const int64_t handle = Simple::VM::Native::Channel::New(Simple::VM::Native::Channel::g_i32);
  if (!Simple::VM::Native::Channel::Send(Simple::VM::Native::Channel::g_i32, handle, 42)) return false;
  if (Simple::VM::Native::Channel::Pending(Simple::VM::Native::Channel::g_i32, handle) != 1) return false;
  int32_t value = 0;
  if (!Simple::VM::Native::Channel::Receive(Simple::VM::Native::Channel::g_i32, handle, false, &value)) {
    return false;
  }
  Simple::VM::Native::Channel::CloseAll(handle);
  return value == 42 && Simple::VM::Native::Channel::Pending(Simple::VM::Native::Channel::g_i32, handle) == 0;
}

const TestCase kVmNativeChannelTests[] = {
  {"vm_split_native_channel_send_receive_pending", VmSplitNativeChannelSendReceivePending},
};

const TestSection kVmNativeChannelSections[] = {
  {"vm_native_channel", kVmNativeChannelTests, sizeof(kVmNativeChannelTests) / sizeof(kVmNativeChannelTests[0])},
};

} // namespace

const TestSection* GetVmNativeChannelSections(size_t* count) {
  if (count) *count = sizeof(kVmNativeChannelSections) / sizeof(kVmNativeChannelSections[0]);
  return kVmNativeChannelSections;
}

} // namespace Simple::VM::Tests
