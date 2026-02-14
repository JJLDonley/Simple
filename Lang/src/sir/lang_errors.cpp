#include <string>
#include <vector>

bool CountFormatPlaceholders(const std::string& fmt,
                             size_t* out_count,
                             std::vector<std::string>* out_segments,
                             std::string* error) {
  if (!out_count) return false;
  *out_count = 0;
  if (out_segments) out_segments->clear();
  size_t segment_start = 0;
  for (size_t i = 0; i < fmt.size(); ++i) {
    if (fmt[i] == '{') {
      if (i + 1 >= fmt.size() || fmt[i + 1] != '}') {
        if (error) *error = "invalid format string: expected '{}' placeholder";
        return false;
      }
      if (out_segments) out_segments->push_back(fmt.substr(segment_start, i - segment_start));
      ++(*out_count);
      ++i;
      segment_start = i + 1;
      continue;
    }
    if (fmt[i] == '}') {
      if (error) *error = "invalid format string: unmatched '}'";
      return false;
    }
  }
  if (out_segments) out_segments->push_back(fmt.substr(segment_start));
  return true;
}
