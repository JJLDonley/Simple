#include "TAST/mutability.h"

namespace Simple::Lang::TAST {

bool IsMutable(Simple::Lang::Mutability mutability) {
  return mutability == Simple::Lang::Mutability::Mutable;
}

bool CheckMutableAssignment(Simple::Lang::Mutability mutability,
                            std::string* error) {
  if (IsMutable(mutability)) return true;
  if (error) *error = "cannot assign to immutable value";
  return false;
}

} // namespace Simple::Lang::TAST
