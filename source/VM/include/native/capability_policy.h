#pragma once

#include <string>
#include <vector>

namespace Simple::VM::Native {

struct CapabilityPolicy {
  bool allow_all = true;
  std::vector<std::string> allowed_tags;
};

bool AllowsCapability(const CapabilityPolicy& policy, const std::string& tag);
bool AllowsCapabilities(const CapabilityPolicy& policy, const std::vector<std::string>& tags,
                        std::string* denied_tag);

} // namespace Simple::VM::Native
