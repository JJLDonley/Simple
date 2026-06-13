#include "TAST/types.h"

namespace Simple::Lang::TAST {

bool IsPlainTypeRef(const Simple::Lang::AST::TypeRef& type) {
  return !type.is_proc && type.pointer_depth == 0 && type.dims.empty() && type.type_args.empty();
}

} // namespace Simple::Lang::TAST
