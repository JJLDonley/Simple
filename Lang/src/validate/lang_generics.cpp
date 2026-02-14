bool CollectTypeParams(const std::vector<std::string>& generics,
                        std::unordered_set<std::string>* out,
                        std::string* error) {
  if (!out) return false;
  out->clear();
  for (const auto& name : generics) {
    if (!out->insert(name).second) {
      if (error) *error = "duplicate generic parameter: " + name;
      return false;
    }
  }
  return true;
}

bool CollectTypeParamsMerged(const std::vector<std::string>& a,
                             const std::vector<std::string>& b,
                             std::unordered_set<std::string>* out,
                             std::string* error) {
  if (!out) return false;
  out->clear();
  for (const auto& name : a) {
    if (!out->insert(name).second) {
      if (error) *error = "duplicate generic parameter: " + name;
      return false;
    }
  }
  for (const auto& name : b) {
    if (!out->insert(name).second) {
      if (error) *error = "duplicate generic parameter: " + name;
      return false;
    }
  }
  return true;
}
