#include <util/compiler_defs.h>
CC_DIAGNOSTIC_PUSH()
CC_DIAGNOSTIC_IGNORE_LLVM_CHECKS()
#include <clang/Frontend/ASTUnit.h>
CC_DIAGNOSTIC_POP()

#include <AST/build_ast.h>
#include <ansi-c/c_preprocess.h>
#include <boost/filesystem.hpp>
#include <c2goto/cprover_library.h>
#include <clang-c-frontend/clang_c_adjust.h>
#include <clang-c-frontend/clang_c_convert.h>
#include <clang-c-frontend/clang_c_language.h>
#include <clang-c-frontend/clang_c_main.h>
#include <util/c_expr2string.h>
#include <sstream>
#include <util/c_link.h>

#include <util/filesystem.h>

#include <ac_config.h>

languaget *new_clang_c_language()
{
  return new clang_c_languaget;
}

clang_c_languaget::clang_c_languaget() = default;

void clang_c_languaget::build_include_args(
  std::vector<std::string> &compiler_args)
{
  if (config.options.get_bool_option("nostdinc"))
  {
    compiler_args.push_back("-nostdinc");
    compiler_args.push_back("-ibuiltininc");
  }

  for (auto const &inc : config.ansi_c.include_paths)
    compiler_args.push_back("-I" + inc);

  const std::string *libc_headers = internal_libc_header_dir();
  if (libc_headers)
  {
    compiler_args.push_back("-isystem");
    compiler_args.push_back(*libc_headers);
  }

  compiler_args.push_back("-resource-dir");
  compiler_args.push_back(clang_resource_dir());

  for (const auto &dir : config.ansi_c.idirafter_paths)
  {
    compiler_args.push_back("-idirafter");
    compiler_args.push_back(dir);
  }
}

void clang_c_languaget::build_compiler_args(
  std::vector<std::string> &compiler_args)
{
  // Append mode arg
  switch (config.ansi_c.word_size)
  {
  case 16:
  case 32:
  case 64:
    compiler_args.emplace_back("-m" + std::to_string(config.ansi_c.word_size));
    break;

  default:
    log_error("Unknown word size: {}\n", config.ansi_c.word_size);
    abort();
  }

  if (config.options.get_bool_option("deadlock-check"))
  {
    compiler_args.emplace_back("-Dpthread_join=pthread_join_switch");
    compiler_args.emplace_back("-Dpthread_mutex_lock=pthread_mutex_lock_check");
    compiler_args.emplace_back(
      "-Dpthread_mutex_unlock=pthread_mutex_unlock_check");
    compiler_args.emplace_back("-Dpthread_cond_wait=pthread_cond_wait_check");
    compiler_args.emplace_back("-Dsem_wait=sem_wait_check");
  }
  else if (config.options.get_bool_option("lock-order-check"))
  {
    compiler_args.emplace_back("-Dpthread_join=pthread_join_noswitch");
    compiler_args.emplace_back(
      "-Dpthread_mutex_lock=pthread_mutex_lock_nocheck");
    compiler_args.emplace_back(
      "-Dpthread_mutex_unlock=pthread_mutex_unlock_nocheck");
    compiler_args.emplace_back("-Dpthread_cond_wait=pthread_cond_wait_nocheck");
    compiler_args.emplace_back(
      "-Dpthread_mutex_destroy=pthread_mutex_destroy_check");
    compiler_args.emplace_back("-Dsem_wait=sem_wait_nocheck");
  }
  else
  {
    compiler_args.emplace_back("-Dpthread_join=pthread_join_noswitch");
    compiler_args.emplace_back(
      "-Dpthread_mutex_lock=pthread_mutex_lock_noassert");
    compiler_args.emplace_back(
      "-Dpthread_mutex_unlock=pthread_mutex_unlock_noassert");
    compiler_args.emplace_back("-Dpthread_cond_wait=pthread_cond_wait_nocheck");
    compiler_args.emplace_back("-Dsem_wait=sem_wait_nocheck");
  }

  for (auto const &def : config.ansi_c.defines)
    compiler_args.push_back("-D" + def);

  if (messaget::state.target("clang", VerbosityLevel::Debug))
    compiler_args.emplace_back("-v");

  compiler_args.emplace_back("-target");
  compiler_args.emplace_back(config.ansi_c.target.to_string());

  std::string sysroot;

  if (config.ansi_c.cheri)
  {
    bool is_purecap = config.ansi_c.cheri == configt::ansi_ct::CHERI_PURECAP;
    compiler_args.emplace_back(
      "-cheri=" + std::to_string(config.ansi_c.capability_width()));

    if (config.ansi_c.target
          .is_riscv()) /* unused as of yet: arch is mips64el */
    {
      compiler_args.emplace_back("-march=rv64imafdcxcheri");
      compiler_args.emplace_back(
        std::string("-mabi=") + (is_purecap ? "l64pc128d" : "lp64d"));
    }
    else if (config.ansi_c.target.arch == "aarch64c")
    {
      /* for morello-llvm 11.0.0 from
       * https://git.morello-project.org/morello/llvm-project.git
       * 94e1dbacf1d854b48386ec2c07a35e0694d626e2
       */
      std::string march = "-march=morello";
      if (is_purecap)
      {
        march += "+c64";
        compiler_args.emplace_back("-mabi=purecap");
      }
      compiler_args.emplace_back(std::move(march));
      compiler_args.emplace_back("-D__ESBMC_CHERI_MORELLO__");
    }
    else if (is_purecap)
      compiler_args.emplace_back("-mabi=purecap");

    compiler_args.emplace_back(
      "-D__ESBMC_CHERI__=" + std::to_string(config.ansi_c.capability_width()));
    compiler_args.emplace_back(
      "-D__builtin_cheri_length_get(p)=__esbmc_cheri_length_get(p)");
    compiler_args.emplace_back(
      "-D__builtin_cheri_bounds_set(p,n)=__esbmc_cheri_bounds_set(p,n)");

    /* DEMO */
    compiler_args.emplace_back(
      "-D__builtin_cheri_base_get(p)=__esbmc_cheri_base_get(p)");
    compiler_args.emplace_back(
      "-D__builtin_cheri_top_get(p)=__esbmc_cheri_top_get(p)");
    compiler_args.emplace_back(
      "-D__builtin_cheri_perms_get(p)=__esbmc_cheri_perms_get(p)");
    compiler_args.emplace_back(
      "-D__builtin_cheri_type_get(p)=__esbmc_cheri_type_get(p)");
    compiler_args.emplace_back(
      "-D__builtin_cheri_flags_get(p)=__esbmc_cheri_flags_get(p)");
    compiler_args.emplace_back(
      "-D__builtin_cheri_sealed_get(p)=__esbmc_cheri_sealed_get(p)");

    /* TODO: DEMO */
    compiler_args.emplace_back("-D__builtin_cheri_tag_get(p)=1");
    compiler_args.emplace_back("-D__builtin_clzll(n)=__esbmc_clzll(n)");

    switch (config.ansi_c.cheri)
    {
    case configt::ansi_ct::CHERI_OFF:
      break;
    case configt::ansi_ct::CHERI_HYBRID:
#ifdef ESBMC_CHERI_HYBRID_SYSROOT
      sysroot = ESBMC_CHERI_HYBRID_SYSROOT;
#endif
      break;
    case configt::ansi_ct::CHERI_PURECAP:
#ifdef ESBMC_CHERI_PURECAP_SYSROOT
      sysroot = ESBMC_CHERI_PURECAP_SYSROOT;
#endif
      break;
    }
  }

  config.options.get_option("sysroot", sysroot);
  if (!sysroot.empty())
    compiler_args.push_back("--sysroot=" + sysroot);
  else
    compiler_args.emplace_back("--sysroot=" ESBMC_C2GOTO_SYSROOT);

  for (const auto &inc : config.ansi_c.include_files)
  {
    compiler_args.push_back("-include");
    compiler_args.push_back(inc);
  }

  for (auto const &inc : config.ansi_c.forces)
    compiler_args.push_back("-f" + inc);

  for (auto const &inc : config.ansi_c.warnings)
    compiler_args.push_back("-W" + inc);

  compiler_args.emplace_back("-D__builtin_memcpy=memcpy");

  compiler_args.emplace_back("-D__ESBMC_alloca=__builtin_alloca");

  // Ignore ctype defined by the system
  compiler_args.emplace_back("-D__NO_CTYPE");

  if (config.ansi_c.target.is_macos())
  {
    compiler_args.push_back("-D_EXTERNALIZE_CTYPE_INLINES_");
    compiler_args.push_back("-D_DONT_USE_CTYPE_INLINE_");
    compiler_args.push_back("-D_SECURE__STRING_H_");
    compiler_args.push_back("-U__BLOCKS__");
    compiler_args.push_back("-Wno-nullability-completeness");
    compiler_args.push_back("-Wno-deprecated-register");
    /*
     * The float ABI on macOS is 'softfp', but for AArch64 clang defaults
     * to armv4t in 32-bit mode and the default for that is the incompatible
     * 'soft': the system's <fenv.h> won't work.
     */
    if (config.ansi_c.target.is_arm() && config.ansi_c.word_size == 32)
    {
      compiler_args.emplace_back("-arch");
      compiler_args.emplace_back("armv6");
      compiler_args.push_back("-mfloat-abi=softfp");
    }
  }

  if (config.ansi_c.target.is_windows_abi())
  {
    compiler_args.push_back("-D_INC_TIME_INL");
    compiler_args.push_back("-D__CRT__NO_INLINE");
    compiler_args.push_back("-D_USE_MATH_DEFINES");
  }

#if ESBMC_SVCOMP
  compiler_args.push_back("-D__ESBMC_SVCOMP");
#endif

  // Increase maximum bracket depth
  compiler_args.push_back("-fbracket-depth=1024");

  // Add -Wunknown-attributes, preprocessed files with GCC generate a bunch
  // of __leaf__ attributes that we don't care about
  compiler_args.emplace_back("-Wno-unknown-attributes");

  /* put custom options at the end of the cmdline such that they can override
   * whatever defaults we put in before. */
  compiler_args.insert(
    compiler_args.end(),
    config.ansi_c.frontend_opts.begin(),
    config.ansi_c.frontend_opts.end());

  // Option to avoid creating a linking command
  compiler_args.emplace_back("-fsyntax-only");
}

void clang_c_languaget::force_file_type(std::vector<std::string> &compiler_args)
{
  // Force clang see all files as .c
  // This forces the preprocessor to be called even in preprocessed files
  // which allow us to perform transformations using -D
  compiler_args.push_back("-x");
  compiler_args.push_back("c");

  // C language standard
  assert(config.language.lid == language_idt::C);
  const std::string &cstd = config.language.std;
  if (!cstd.empty())
    compiler_args.emplace_back("-std=" + cstd);
}

bool clang_c_languaget::parse(const std::string &path)
{
  // preprocessing

  std::ostringstream o_preprocessed;
  if (preprocess(path, o_preprocessed))
    return true;

  // Get compiler arguments and add the file path
  std::vector<std::string> new_compiler_args = compiler_args("clang-tool");
  new_compiler_args.push_back(path);

  if (FILE *f = messaget::state.target("clang", VerbosityLevel::Debug))
  {
    fprintf(f, "clang invocation:");
    for (const std::string &s : new_compiler_args)
      fprintf(f, " '%s'", s.c_str());
    fprintf(f, "\n");
  }

  // Get intrinsics
  std::string intrinsics = internal_additions();

  // Generate ASTUnit and add to our vector
  auto newAST = buildASTs(intrinsics, new_compiler_args);

  // Use diagnostics to find errors, rather than the return code.
  if (newAST->getDiagnostics().hasErrorOccurred())
    return true;

  if (!AST)
    AST = move(newAST);
  else
    mergeASTs(newAST, AST);

  return false;
}

bool clang_c_languaget::typecheck(contextt &context, const std::string &)
{
  clang_c_convertert converter(context, AST, "C");
  if (converter.convert())
    return true;

  clang_c_adjust adjuster(context);
  if (adjuster.adjust())
    return true;

  return false;
}

void clang_c_languaget::show_parse(std::ostream &)
{
  AST->getASTContext().getTranslationUnitDecl()->dump();
}

bool clang_c_languaget::preprocess(const std::string &, std::ostream &)
{
// TODO: Check the preprocess situation.
#if 0
  return c_preprocess(path, outstream, false);
#endif
  return false;
}

bool clang_c_languaget::final(contextt &context)
{
  add_cprover_library(context, this);
  clang_c_maint c_main(context);
  return c_main.clang_main();
}

std::string clang_c_languaget::internal_additions()
{
  std::string intrinsics =
    R"(
# 1 "esbmc_intrinsics.h" 1
void __ESBMC_assume(_Bool);
void __ESBMC_assert(_Bool, const char *);
_Bool __ESBMC_same_object(const void *, const void *);
void __ESBMC_yield();
void __ESBMC_atomic_begin();
void __ESBMC_atomic_end();

// Explicitly initialize a new object
void __ESBMC_init_object(void*);

// pointers
__UINTPTR_TYPE__ __ESBMC_POINTER_OBJECT(const void *);
__PTRDIFF_TYPE__ __ESBMC_POINTER_OFFSET(const void *);

// malloc
__attribute__((annotate("__ESBMC_inf_size")))
_Bool __ESBMC_alloc[1];

__attribute__((annotate("__ESBMC_inf_size")))
_Bool __ESBMC_is_dynamic[1];

__attribute__((annotate("__ESBMC_inf_size")))
__SIZE_TYPE__ __ESBMC_alloc_size[1];

// Get object size
__SIZE_TYPE__ __ESBMC_get_object_size(const void *);

_Bool __ESBMC_is_little_endian();

int __ESBMC_rounding_mode = 0;

void *__ESBMC_memset(void *, int, unsigned int);

/* same semantics as memcpy(tgt, src, size) where size matches the size of the
 * types tgt and src point to. */
void __ESBMC_bitcast(void * /* tgt */, void * /* src */);

// Calls goto_symext::add_memory_leak_checks() which adds memory leak checks
// if it's enabled
void __ESBMC_memory_leak_checks();

// Forward decls for pthread main thread begin/end hooks. Because they're
// pulled in from the C library, they need to be declared prior to pulling
// them in, for type checking.
void __ESBMC_pthread_start_main_hook(void);
void __ESBMC_pthread_end_main_hook(void);

// Forward decl of the intrinsic function that calls atexit registered functions.
// We need this here or it won't be pulled from the C library
void __ESBMC_atexit_handler(void);

// Forward declarations for nondeterministic types.
int nondet_int();
unsigned int nondet_uint();
long nondet_long();
unsigned long nondet_ulong();
short nondet_short();
unsigned short nondet_ushort();
char nondet_char();
unsigned char nondet_uchar();
signed char nondet_schar();
_Bool nondet_bool();
float nondet_float();
double nondet_double();

// TACAS definitions,
int __VERIFIER_nondet_int();
unsigned int __VERIFIER_nondet_uint();
long __VERIFIER_nondet_long();
unsigned long __VERIFIER_nondet_ulong();
short __VERIFIER_nondet_short();
unsigned short __VERIFIER_nondet_ushort();
char __VERIFIER_nondet_char();
unsigned char __VERIFIER_nondet_uchar();
signed char __VERIFIER_nondet_schar();
_Bool __VERIFIER_nondet_bool();
float __VERIFIER_nondet_float();
double __VERIFIER_nondet_double();

void __VERIFIER_error();
void __VERIFIER_assume(int);
void __VERIFIER_atomic_begin();
void __VERIFIER_atomic_end();

/* Causes a verification error when its call is reachable; internal use in math
 * models */
void __ESBMC_unreachable();
    )";

  if (config.ansi_c.cheri)
  {
    intrinsics += R"(
__SIZE_TYPE__ __esbmc_cheri_length_get(void *__capability);
void *__capability __esbmc_cheri_bounds_set(void *__capability, __SIZE_TYPE__);
__SIZE_TYPE__ __esbmc_cheri_base_get(void *__capability);
#if __ESBMC_CHERI__ == 128
__UINT64_TYPE__ __esbmc_cheri_top_get(void *__capability);
__SIZE_TYPE__ __esbmc_cheri_perms_get(void *__capability);
__UINT16_TYPE__ __esbmc_cheri_flags_get(void *__capability);
__UINT32_TYPE__ __esbmc_cheri_type_get(void *__capability);
_Bool __esbmc_cheri_sealed_get(void *__capability);
#endif
__UINT64_TYPE__ __esbmc_clzll(__UINT64_TYPE__);
    )";
  }

  return intrinsics;
}

bool clang_c_languaget::from_expr(
  const exprt &expr,
  std::string &code,
  const namespacet &ns,
  unsigned flags)
{
  code = c_expr2string(expr, ns, flags);
  return false;
}

bool clang_c_languaget::from_type(
  const typet &type,
  std::string &code,
  const namespacet &ns,
  unsigned flags)
{
  code = c_type2string(type, ns, flags);
  return false;
}

unsigned clang_c_languaget::default_flags(presentationt target) const
{
  unsigned f = 0;
  switch (target)
  {
  case presentationt::HUMAN:
    f |= c_expr2stringt::SHORT_ZERO_COMPOUNDS;
    break;
  case presentationt::WITNESS:
    f |= c_expr2stringt::UNIQUE_FLOAT_REPR;
    break;
  }
  return f;
}
