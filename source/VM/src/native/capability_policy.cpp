#include "native/capability_policy.h"

#include <algorithm>

namespace Simple::VM::Native {

bool AllowsCapability(const CapabilityPolicy& policy, const std::string& tag) {
  if (tag.empty() || policy.allow_all) return true;
  return std::find(policy.allowed_tags.begin(), policy.allowed_tags.end(), tag) !=
         policy.allowed_tags.end();
}

bool AllowsCapabilities(const CapabilityPolicy& policy, const std::vector<std::string>& tags,
                        std::string* denied_tag) {
  for (const std::string& tag : tags) {
    if (!AllowsCapability(policy, tag)) {
      if (denied_tag) *denied_tag = tag;
      return false;
    }
  }
  return true;
}

} // namespace Simple::VM::Native
