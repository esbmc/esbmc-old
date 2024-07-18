#ifndef CLANG_C_FRONTEND_AST_BUILD_AST_H_
#define CLANG_C_FRONTEND_AST_BUILD_AST_H_

#include <memory>
#include <unordered_map>
#include <vector>

#define __STDC_LIMIT_MACROS
#define __STDC_FORMAT_MACROS

// Forward dec, to avoid bringing in clang headers
namespace clang
{
class ASTUnit;
} // namespace clang

std::unique_ptr<clang::ASTUnit> buildASTs(
  const std::string &intrinsics,
  const std::vector<std::string> &compiler_args);

void mergeASTs(
  const std::unique_ptr<clang::ASTUnit> &FromUnit,
  std::unique_ptr<clang::ASTUnit> &ToUnit);

#endif /* CLANG_C_FRONTEND_AST_BUILD_AST_H_ */
