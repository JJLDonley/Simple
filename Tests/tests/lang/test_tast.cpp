#include "test_utils.h"

#include "TAST/abi.h"
#include "TAST/generics.h"

namespace Simple::VM::Tests {
namespace {

bool LangSplitTastAbiAndGenericsSmoke() {
  Simple::Lang::AST::TypeRef scalar;
  scalar.name = "i64";
  std::string error;
  if (!Simple::Lang::TAST::CheckAbiShape(scalar, false, &error)) return false;

  Simple::Lang::AST::TypeRef box;
  box.name = "Box";
  Simple::Lang::AST::TypeRef generic;
  generic.name = "T";
  box.type_args.push_back(generic);
  Simple::Lang::AST::TypeRef replacement;
  replacement.name = "string";
  Simple::Lang::TAST::GenericSubstitutionMap substitutions;
  substitutions["T"] = replacement;
  Simple::Lang::AST::TypeRef out;
  return Simple::Lang::TAST::SubstituteGenericTypes(box, substitutions, &out) &&
         out.type_args.size() == 1 && out.type_args[0].name == "string";
}

const TestCase kLangTastTests[] = {
  {"lang_split_tast_abi_and_generics_smoke", LangSplitTastAbiAndGenericsSmoke},
};

const TestSection kLangTastSections[] = {
  {"lang_tast", kLangTastTests, sizeof(kLangTastTests) / sizeof(kLangTastTests[0])},
};

} // namespace

const TestSection* GetLangTastSections(size_t* count) {
  if (count) *count = sizeof(kLangTastSections) / sizeof(kLangTastSections[0]);
  return kLangTastSections;
}

} // namespace Simple::VM::Tests
