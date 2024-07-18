#ifndef CLANG_C_FRONTEND_CLANG_C_LANGUAGE_H_
#define CLANG_C_FRONTEND_CLANG_C_LANGUAGE_H_

#include <util/language.h>

#define __STDC_LIMIT_MACROS
#define __STDC_FORMAT_MACROS

// Forward dec, to avoid bringing in clang headers
namespace clang
{
class ASTUnit;
} // namespace clang

class clang_c_languaget : public languaget
{
public:
  virtual bool preprocess(const std::string &path, std::ostream &outstream);

  bool parse(const std::string &path) override;

  bool final(contextt &context) override;

  bool typecheck(contextt &context, const std::string &module) override;

  std::string id() const override
  {
    return "c";
  }

  void show_parse(std::ostream &out) override;

  // conversion from expression into string
  bool from_expr(
    const exprt &expr,
    std::string &code,
    const namespacet &ns,
    unsigned flags) override;

  // conversion from type into string
  bool from_type(
    const typet &type,
    std::string &code,
    const namespacet &ns,
    unsigned flags) override;

  unsigned default_flags(presentationt target) const override;

  languaget *new_language() const override
  {
    return new clang_c_languaget();
  }

  clang_c_languaget();

protected:
  virtual std::string internal_additions();

  static const std::string &clang_resource_dir();

  // Force the file type, .c for the C frontend and .cpp for the C++ one
  virtual void force_file_type(std::vector<std::string> &compiler_args);

  /* Include search paths, in order (any of 1, 2, 3, 4, 5, 7, 8 may be empty
   * depending on C/C++ mode and command line options such as --no-library or
   * --nostdinc):
   *
   * 1. any user-specified -I from left to right
   * 2. our C++ library headers, corresponds to:
   *    src/cpp/library/{CUDA,Qt,Qt/Core}
   * 3. our C++ standard library and system headers, corresponds to:
   *    src/cpp/library
   * 4. our C standard library and system headers, corresponds to:
   *    src/c2goto/headers
   * 5. the system's C++ standard library, e.g., /usr/include/c++/v1 for libc++ or
   *    /usr/lib/gcc/x86_64-pc-linux-gnu/13/include/g++-v13/{.,x86_64-pc-linux-gnu,backward}
   *    for the default libstdc++
   * 6. the Clang resource directory's /include, e.g.
   *    /usr/lib/llvm/16/bin/../../../../lib/clang/16/include/../include
   * 7. the default system include directories, e.g. /usr/local/include and
   *    /usr/include
   * 8. any user-specified --idirafter from left to right
   *
   * 1 and 2 are done by passing '-I'; 3 and 4 via '-isystem'; 5, 6 and 7 are
   * built into Clang where 5 can be adjusted by passing '--gcc-install-dir'
   * and '-stdlib', 6 is given via '-resource-dir', and 7 is optionally disabled
   * per '-nostdinc'; 8 is done via '-idirafter'.
   *
   * Note: built-in paths are subject to --sysroot.
   */
  virtual void build_include_args(std::vector<std::string> &compiler_args);
  virtual void build_compiler_args(std::vector<std::string> &compiler_args);

  std::vector<std::string> compiler_args(std::string tool_name)
  {
    std::vector<std::string> v{std::move(tool_name)};
    force_file_type(v);
    build_include_args(v);
    build_compiler_args(v);
    return v;
  }

  std::unique_ptr<clang::ASTUnit> AST;
};

languaget *new_clang_c_language();

#endif
