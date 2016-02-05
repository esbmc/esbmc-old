/*******************************************************************\

Module: C++ Language Module

Author: Daniel Kroening, kroening@cs.cmu.edu

\*******************************************************************/

#include <sstream>
#include <fstream>

#include <llvm_language.h>
#include <llvm_convert.h>
#include <llvm_adjust.h>
#include <llvm_main.h>

#include <ansi-c/cprover_library.h>
#include <ansi-c/c_preprocess.h>
#include <ansi-c/c_link.h>
#include <ansi-c/gcc_builtin_headers.h>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>

#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>

languaget *new_llvm_language()
{
  return new llvm_languaget;
}

llvm_languaget::llvm_languaget()
{
  add_clang_headers();
  internal_additions();
}

llvm_languaget::~llvm_languaget()
{
}

void llvm_languaget::build_compiler_string(
  std::vector<std::string> &compiler_string)
{
  compiler_string.push_back("-I.");

  // Append mode arg
  switch(config.ansi_c.word_size)
  {
    case 16:
      compiler_string.push_back("-m16");
      break;

    case 32:
      compiler_string.push_back("-m32");
      break;

    case 64:
      compiler_string.push_back("-m64");
      break;

    default:
      std::cerr << "Unknown word size: " << config.ansi_c.word_size
                << std::endl;
      abort();
  }

  if(config.ansi_c.char_is_unsigned)
    compiler_string.push_back("-funsigned-char");

  if(config.options.get_bool_option("deadlock-check"))
  {
    compiler_string.push_back("-Dpthread_join=pthread_join_switch");
    compiler_string.push_back("-Dpthread_mutex_lock=pthread_mutex_lock_check");
    compiler_string.push_back("-Dpthread_mutex_unlock=pthread_mutex_unlock_check");
    compiler_string.push_back("-Dpthread_cond_wait=pthread_cond_wait_check");
  }
  else if (config.options.get_bool_option("lock-order-check"))
  {
    compiler_string.push_back("-Dpthread_join=pthread_join_noswitch");
    compiler_string.push_back("-Dpthread_mutex_lock=pthread_mutex_lock_nocheck");
    compiler_string.push_back("-Dpthread_mutex_unlock=pthread_mutex_unlock_nocheck");
    compiler_string.push_back("-Dpthread_cond_wait=pthread_cond_wait_nocheck");
  }
  else
  {
    compiler_string.push_back("-Dpthread_join=pthread_join_noswitch");
    compiler_string.push_back("-Dpthread_mutex_lock=pthread_mutex_lock_noassert");
    compiler_string.push_back("-Dpthread_mutex_unlock=pthread_mutex_unlock_noassert");
    compiler_string.push_back("-Dpthread_cond_wait=pthread_cond_wait_nocheck");
  }

  for(auto def : config.ansi_c.defines)
    compiler_string.push_back("-D" + def);

  for(auto inc : config.ansi_c.include_paths)
    compiler_string.push_back("-I" + inc);

  // Ignore ctype defined by the system
  compiler_string.push_back("-D__NO_CTYPE");

  // Realloc should call __ESBMC_realloc
  compiler_string.push_back("-Drealloc=__ESBMC_realloc");

  // Force llvm see all files as .c
  // This forces the preprocessor to be called even in preprocessed files
  // which allow us to perform transformations using -D
  compiler_string.push_back("-x");
  compiler_string.push_back("c");
}

bool llvm_languaget::parse(
  const std::string &path,
  message_handlert &message_handler)
{
  // preprocessing

  std::ostringstream o_preprocessed;
  if(preprocess(path, o_preprocessed, message_handler))
    return true;

  return parse(path);
}

bool llvm_languaget::parse(const std::string& path)
{
  // Finish the compiler string
  std::vector<std::string> compiler_string;
  build_compiler_string(compiler_string);

  // TODO: Change to JSONCompilationDatabase
  clang::tooling::FixedCompilationDatabase Compilations("./", compiler_string);

  std::vector<std::string> sources;
  sources.push_back("/esbmc_intrinsics.h");
  sources.push_back(path);

  clang::tooling::ClangTool Tool(Compilations, sources);
  Tool.mapVirtualFile("/esbmc_intrinsics.h", intrinsics);

  for(auto it = clang_headers_name.begin(), it1 = clang_headers_content.begin();
      (it != clang_headers_name.end()) && (it1 != clang_headers_content.end());
      ++it, ++it1)
    Tool.mapVirtualFile(*it, *it1);

  Tool.buildASTs(ASTs);

  // Use diagnostics to find errors, rather than the return code.
  for (const auto &astunit : ASTs) {
    if (astunit->getDiagnostics().hasErrorOccurred()) {
      std::cerr << std::endl;
      return true;
    }
  }

  return false;
}

bool llvm_languaget::typecheck(
  contextt& context,
  const std::string& module,
  message_handlert& message_handler)
{
  return convert(context, module, message_handler);
}

void llvm_languaget::show_parse(std::ostream& out __attribute__((unused)))
{
  for (auto &translation_unit : ASTs)
  {
    for (clang::ASTUnit::top_level_iterator
      it = translation_unit->top_level_begin();
      it != translation_unit->top_level_end();
      it++)
    {
      (*it)->dump();
      std::cout << std::endl;
    }
  }
}

bool llvm_languaget::convert(
  contextt &context,
  const std::string &module,
  message_handlert &message_handler)
{
  contextt new_context;

  llvm_convertert converter(new_context, ASTs);
  if(converter.convert())
    return true;

  llvm_adjust adjuster(new_context);
  if(adjuster.adjust())
    return true;

  return c_link(context, new_context, message_handler, module);
}

bool llvm_languaget::preprocess(
  const std::string &path __attribute__((unused)),
  std::ostream &outstream __attribute__((unused)),
  message_handlert &message_handler __attribute__((unused)))
{
  // TODO: Check the preprocess situation.
#if 0
  return c_preprocess(path, outstream, false, message_handler);
#endif
  return false;
}

bool llvm_languaget::final(contextt& context, message_handlert& message_handler)
{
  add_cprover_library(context, message_handler);
  return llvm_main(context, "c::", "c::main", message_handler);
}

void llvm_languaget::internal_additions()
{
  intrinsics +=
    "__attribute__((used))\n"
    "void __ESBMC_assume(_Bool assumption);\n"
    "__attribute__((used))\n"
    "void assert(_Bool assertion);\n"
    "__attribute__((used))\n"
    "void __ESBMC_assert(_Bool assertion, const char *description);\n"
    "__attribute__((used))\n"
    "_Bool __ESBMC_same_object(const void *, const void *);\n"

    // pointers
    "__attribute__((used))\n"
    "unsigned __ESBMC_POINTER_OBJECT(const void *p);\n"
    "__attribute__((used))\n"
    "signed __ESBMC_POINTER_OFFSET(const void *p);\n"

    // malloc
    // This will be set to infinity size array at llvm_adjust
    // TODO: We definitely need a better solution for this
    "__attribute__((used))\n"
    "_Bool __ESBMC_alloc[1];\n"
    "__attribute__((used))\n"
    "_Bool __ESBMC_deallocated[1];\n"
    "__attribute__((used))\n"
    "_Bool __ESBMC_is_dynamic[1];\n"
    "__attribute__((used))\n"
    "unsigned long __ESBMC_alloc_size[1];\n"

    "__attribute__((used))\n"
    "void *__ESBMC_realloc(void *ptr, long unsigned int size);\n"

    // float stuff
    "__attribute__((used))\n"
    "_Bool __ESBMC_isnan(double f);\n"
    "__attribute__((used))\n"
    "_Bool __ESBMC_isfinite(double f);\n"
    "__attribute__((used))\n"
    "_Bool __ESBMC_isinf(double f);\n"
    "__attribute__((used))\n"
    "_Bool __ESBMC_isnormal(double f);\n"

    // absolute value
    "__attribute__((used))\n"
    "int __ESBMC_abs(int x);\n"
    "__attribute__((used))\n"
    "long int __ESBMC_labs(long int x);\n"
    "__attribute__((used))\n"
    "double __ESBMC_fabs(double x);\n"
    "__attribute__((used))\n"
    "long double __ESBMC_fabsl(long double x);\n"
    "__attribute__((used))\n"
    "float __ESBMC_fabsf(float x);\n"

    // Digital controllers code
    "__attribute__((used))\n"
    "void __ESBMC_generate_cascade_controllers(float * cden, int csize, float * cout, int coutsize, _Bool isDenominator);\n"
    "__attribute__((used))\n"
    "void __ESBMC_generate_delta_coefficients(float a[], double out[], float delta);\n"
    "__attribute__((used))\n"
    "_Bool __ESBMC_check_delta_stability(double dc[], double sample_time, int iwidth, int precision);\n"

    // Forward decs for pthread main thread begin/end hooks. Because they're
    // pulled in from the C library, they need to be declared prior to pulling
    // them in, for type checking.
    "__attribute__((used))\n"
    "void pthread_start_main_hook(void);\n"
    "__attribute__((used))\n"
    "void pthread_end_main_hook(void);\n"

    // Forward declarations for nondeterministic types.
    "int nondet_int();\n"
    "unsigned int nondet_uint();\n"
    "long nondet_long();\n"
    "unsigned long nondet_ulong();\n"
    "short nondet_short();\n"
    "unsigned short nondet_ushort();\n"
    "char nondet_char();\n"
    "unsigned char nondet_uchar();\n"
    "signed char nondet_schar();\n"
    "_Bool nondet_bool();\n"
    "float nondet_float();\n"
    "double nondet_double();"

    // And again, for TACAS VERIFIER versions,
    "int __VERIFIER_nondet_int();\n"
    "unsigned int __VERIFIER_nondet_uint();\n"
    "long __VERIFIER_nondet_long();\n"
    "unsigned long __VERIFIER_nondet_ulong();\n"
    "short __VERIFIER_nondet_short();\n"
    "unsigned short __VERIFIER_nondet_ushort();\n"
    "char __VERIFIER_nondet_char();\n"
    "unsigned char __VERIFIER_nondet_uchar();\n"
    "signed char __VERIFIER_nondet_schar();\n"
    "_Bool __VERIFIER_nondet_bool();\n"
    "float __VERIFIER_nondet_float();\n"
    "double __VERIFIER_nondet_double();"

    "\n";
}

bool llvm_languaget::from_expr(
  const exprt &expr __attribute__((unused)),
  std::string &code __attribute__((unused)),
  const namespacet &ns __attribute__((unused)))
{
  std::cout << "Method " << __PRETTY_FUNCTION__ << " not implemented yet" << std::endl;
  abort();
  return true;
}

bool llvm_languaget::from_type(
  const typet &type __attribute__((unused)),
  std::string &code __attribute__((unused)),
  const namespacet &ns __attribute__((unused)))
{
  std::cout << "Method " << __PRETTY_FUNCTION__ << " not implemented yet" << std::endl;
  abort();
  return true;
}

bool llvm_languaget::to_expr(
  const std::string &code __attribute__((unused)),
  const std::string &module __attribute__((unused)),
  exprt &expr __attribute__((unused)),
  message_handlert &message_handler __attribute__((unused)),
  const namespacet &ns __attribute__((unused)))
{
  std::cout << "Method " << __PRETTY_FUNCTION__ << " not implemented yet" << std::endl;
  abort();
  return true;
}
