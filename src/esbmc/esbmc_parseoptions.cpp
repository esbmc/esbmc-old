#include <ac_config.h>

#ifndef _WIN32
extern "C"
{
#  include <fcntl.h>
#  include <unistd.h>

#  ifdef HAVE_SENDFILE_ESBMC
#    include <sys/sendfile.h>
#  endif

#  include <sys/resource.h>
#  include <sys/time.h>
#  include <sys/types.h>
}
#endif

#include <esbmc/bmc.h>
#include <esbmc/esbmc_parseoptions.h>
#include <cctype>
#include <clang-c-frontend/clang_c_language.h>
#include <util/config.h>
#include <csignal>
#include <cstdlib>
#include <util/expr_util.h>
#include <iostream>
#include <goto-programs/add_race_assertions.h>
#include <goto-programs/goto_check.h>
#include <goto-programs/goto_convert_functions.h>
#include <goto-programs/goto_inline.h>
#include <goto-programs/goto_k_induction.h>
#include <goto-programs/abstract-interpretation/interval_analysis.h>
#include <goto-programs/abstract-interpretation/gcse.h>
#include <goto-programs/loop_numbers.h>
#include <goto-programs/goto_binary_reader.h>
#include <goto-programs/write_goto_binary.h>
#include <goto-programs/remove_no_op.h>
#include <goto-programs/remove_unreachable.h>
#include <goto-programs/set_claims.h>
#include <goto-programs/show_claims.h>
#include <goto-programs/loop_unroll.h>
#include <goto-programs/mark_decl_as_non_det.h>
#include <goto2c/goto2c.h>
#include <util/irep.h>
#include <langapi/languages.h>
#include <langapi/mode.h>
#include <memory>
#include <pointer-analysis/goto_program_dereference.h>
#include <pointer-analysis/show_value_sets.h>
#include <pointer-analysis/value_set_analysis.h>
#include <util/symbol.h>
#include <util/time_stopping.h>
#include <goto-programs/goto_cfg.h>

#ifndef _WIN32
#  include <sys/wait.h>
#  include <execinfo.h>
#  include <fcntl.h>
#endif

#ifdef ENABLE_OLD_FRONTEND
#  include <ansi-c/c_preprocess.h>
#endif

#ifdef ENABLE_GOTO_CONTRACTOR
#  include <goto-programs/goto_contractor.h>
#endif

#define BT_BUF_SIZE 256

extern "C" const char buildidstring_buf[];
extern "C" const unsigned int buildidstring_buf_size;

static std::string_view esbmc_version_string()
{
  return {buildidstring_buf, buildidstring_buf_size};
}

enum PROCESS_TYPE
{
  BASE_CASE,
  FORWARD_CONDITION,
  INDUCTIVE_STEP,
  PARENT
};

struct resultt
{
  PROCESS_TYPE type;
  uint64_t k;
};

#ifndef _WIN32
void timeout_handler(int)
{
  log_error("Timed out");
  // Unfortunately some highly useful pieces of code hook themselves into
  // aexit and attempt to free some memory. That doesn't really make sense to
  // occur on exit, but more importantly doesn't mix well with signal handlers,
  // and results in the allocator locking against itself. So use _exit instead
  _exit(1);
}
#endif

#ifndef _WIN32
/* This will produce output on stderr that looks somewhat like this:
 *   Signal 6, backtrace:
 *   src/esbmc/esbmc(+0xad52e)[0x556c5dcdb52e]
 *   /lib64/libc.so.6(+0x39d50)[0x7f7a8f475d50]
 *   /lib64/libc.so.6(+0x89d9c)[0x7f7a8f4c5d9c]
 *   /lib64/libc.so.6(raise+0x12)[0x7f7a8f475ca2]
 *   /lib64/libc.so.6(abort+0xd3)[0x7f7a8f45e4ed]
 *   src/esbmc/esbmc(+0x62e3e5)[0x556c5e25c3e5]
 *   src/esbmc/esbmc(+0x61f7f1)[0x556c5e24d7f1]
 *   [...]
 *
 *   Memory map:
 *   [...]
 *
 * The backtrace can be translated into proper function symbols via addr2line,
 * e.g.
 *
 *   cat bt | tr -d '[]' | tr '()' ' ' | grep esbmc | \
 *   while read f a b; do echo $a | tr -d '+'; done | \
 *   xargs addr2line -iapfCr -e src/esbmc/esbmc
 */
static void segfault_handler(int sig)
{
  ::signal(sig, SIG_DFL);
  void *buffer[BT_BUF_SIZE];
  int n = backtrace(buffer, BT_BUF_SIZE);
  dprintf(STDERR_FILENO, "\nSignal %d, backtrace:\n", sig);
  backtrace_symbols_fd(buffer, n, STDERR_FILENO);
  int fd = open("/proc/self/maps", O_RDONLY);
  if (fd != -1)
  {
    dprintf(STDERR_FILENO, "\nMemory map:\n");
    for (ssize_t rd; (rd = read(fd, buffer, sizeof(buffer))) > 0 ||
                     (rd == -1 && errno == EINTR);)
      rd = write(STDERR_FILENO, buffer, rd < 0 ? 0 : rd);
    close(fd);
  }
  ::raise(sig);
}
#endif

// This transforms a string representation of a time interval
// written in the form <number><suffix> into seconds.
// The following suffixes corresponding to time units are supported:
//
//  s - seconds,
//  m - minutes,
//  h - hours,
//  d - days.
//
// When <suffix> is empty, the default time unit is seconds.
// If <suffix> is not empty, and its final character is not in the list above,
// this method throws an error.
//
// \param str - string representation of a time interval,
// \return - number of seconds that represents the input string value.
uint64_t esbmc_parseoptionst::read_time_spec(const char *str)
{
  uint64_t mult;
  int len = strlen(str);
  if (!isdigit(str[len - 1]))
  {
    switch (str[len - 1])
    {
    case 's':
      mult = 1;
      break;
    case 'm':
      mult = 60;
      break;
    case 'h':
      mult = 3600;
      break;
    case 'd':
      mult = 86400;
      break;
    default:
      log_error("Unrecognized timeout suffix");
      abort();
    }
  }
  else
  {
    mult = 1;
  }

  uint64_t timeout = strtol(str, nullptr, 10);
  timeout *= mult;
  return timeout;
}

// This transforms a string representation of a memory limit
// written in the form <number><suffix> into megabytes.
// The following suffixes corresponding to memory size units are supported:
//
//  b - bytes,
//  k - kilobytes,
//  m - megabytes,
//  g - gigabytes.
//
// When <suffix> is empty, the default unit is megabytes.
// If <suffix> is not empty, and its final character is not in the list above,
// this method throws an error.
//
// \param str - string representation of a memory limit,
// \return - number of megabytes that represents the input string value.
uint64_t esbmc_parseoptionst::read_mem_spec(const char *str)
{
  uint64_t mult;
  int len = strlen(str);
  if (!isdigit(str[len - 1]))
  {
    switch (str[len - 1])
    {
    case 'b':
      mult = 1;
      break;
    case 'k':
      mult = 1024;
      break;
    case 'm':
      mult = 1024 * 1024;
      break;
    case 'g':
      mult = 1024 * 1024 * 1024;
      break;
    default:
      log_error("Unrecognized memlimit suffix");
      abort();
    }
  }
  else
  {
    mult = 1024 * 1024;
  }

  uint64_t size = strtol(str, nullptr, 10);
  size *= mult;
  return size;
}

static std::string format_target()
{
  const char *endian = nullptr;
  switch (config.ansi_c.endianess)
  {
  case configt::ansi_ct::IS_LITTLE_ENDIAN:
    endian = "little";
    break;
  case configt::ansi_ct::IS_BIG_ENDIAN:
    endian = "big";
    break;
  case configt::ansi_ct::NO_ENDIANESS:
    endian = "no";
    break;
  }
  assert(endian);
  const char *lib = nullptr;
  switch (config.ansi_c.lib)
  {
  case configt::ansi_ct::LIB_NONE:
    lib = "system";
    break;
  case configt::ansi_ct::LIB_FULL:
    lib = "esbmc";
    break;
  }
  assert(lib);
  std::ostringstream oss;
  oss << config.ansi_c.word_size << "-bit " << endian << "-endian "
      << config.ansi_c.target.to_string() << " with " << lib << "libc";
  return oss.str();
}

// This method creates a set of options based on the CMD arguments passed to
// ESBMC. Also, it sets some options that are used across various
// ESBMC stages but which are not available via CMD.
//
// \param options - the options object created and updated by this method.
void esbmc_parseoptionst::get_command_line_options(optionst &options)
{
  if (config.set(cmdline))
    exit(1);

  log_status("Target: {}", format_target());

  // Copy all flags that are set to non-default values in CMD into options
  options.cmdline(cmdline);
  set_verbosity_msg();

  if (cmdline.isset("git-hash"))
  {
    log_result("{}", esbmc_version_string());
    exit(0);
  }

  if (cmdline.isset("list-solvers"))
  {
    // Generated for us by autoconf,
    log_result("Available solvers: {}", ESBMC_AVAILABLE_SOLVERS);
    exit(0);
  }

  // Below we make some additional adjustments (e.g., adding some options
  // that are used by ESBMC at later stages but which are not available
  // through CMD, setting groups of options based depending on
  // particular CMD flags)
  if (cmdline.isset("bv"))
    options.set_option("int-encoding", false);

  if (cmdline.isset("ir"))
    options.set_option("int-encoding", true);

  if (cmdline.isset("fixedbv"))
    options.set_option("fixedbv", true);
  else
    options.set_option("floatbv", true);

  if (cmdline.isset("context-bound"))
    options.set_option("context-bound", cmdline.getval("context-bound"));
  else
    options.set_option("context-bound", -1);

  if (cmdline.isset("deadlock-check"))
  {
    options.set_option("deadlock-check", true);
    options.set_option("atomicity-check", false);
  }
  else
    options.set_option("deadlock-check", false);

  if (cmdline.isset("compact-trace"))
    options.set_option("no-slice", true);

  if (cmdline.isset("smt-during-symex"))
  {
    log_status("Enabling --no-slice due to presence of --smt-during-symex");
    options.set_option("no-slice", true);
  }

  if (cmdline.isset("smt-thread-guard") || cmdline.isset("smt-symex-guard"))
  {
    if (!cmdline.isset("smt-during-symex"))
    {
      log_error(
        "Please explicitly specify --smt-during-symex if you want "
        "to use features that involve encoding SMT during symex");
      abort();
    }
  }

  // check the user's parameters to run incremental verification
  if (!cmdline.isset("unlimited-k-steps"))
  {
    // Get max number of iterations
    BigInt max_k_step = strtoul(cmdline.getval("max-k-step"), nullptr, 10);

    // Get the increment
    unsigned k_step_inc = strtoul(cmdline.getval("k-step"), nullptr, 10);

    // check whether k-step is greater than max-k-step
    if (k_step_inc >= max_k_step)
    {
      log_error(
        "Please specify --k-step smaller than max-k-step if you want "
        "to use incremental verification.");
      abort();
    }
  }

  if (cmdline.isset("base-case"))
  {
    options.set_option("base-case", true);
    options.set_option("no-unwinding-assertions", true);
    options.set_option("partial-loops", false);
  }

  if (cmdline.isset("forward-condition"))
  {
    options.set_option("forward-condition", true);
    options.set_option("no-unwinding-assertions", false);
    options.set_option("partial-loops", false);
    options.set_option("no-assertions", true);
  }

  if (cmdline.isset("inductive-step"))
  {
    options.set_option("inductive-step", true);
    options.set_option("no-unwinding-assertions", true);
    options.set_option("partial-loops", false);
  }

  if (
    cmdline.isset("overflow-check") || cmdline.isset("unsigned-overflow-check"))
    options.set_option("disable-inductive-step", true);

  if (cmdline.isset("ub-shift-check"))
    options.set_option("ub-shift-check", true);

  if (cmdline.isset("timeout"))
  {
#ifdef _WIN32
    log_error("Timeout unimplemented on Windows, sorry");
    abort();
#else
    const char *time = cmdline.getval("timeout");
    uint64_t timeout = read_time_spec(time);
    signal(SIGALRM, timeout_handler);
    alarm(timeout);
#endif
  }

  if (cmdline.isset("memlimit"))
  {
#ifdef _WIN32
    log_error("Can't memlimit on Windows, sorry");
    abort();
#else
    uint64_t size = read_mem_spec(cmdline.getval("memlimit"));

    struct rlimit lim;
    lim.rlim_cur = size;
    lim.rlim_max = size;
    if (setrlimit(RLIMIT_DATA, &lim) != 0)
    {
      perror("Couldn't set memory limit");
      abort();
    }
#endif
  }

#ifndef _WIN32
  struct rlimit lim;
  if (cmdline.isset("enable-core-dump"))
  {
    lim.rlim_cur = RLIM_INFINITY;
    lim.rlim_max = RLIM_INFINITY;
    if (setrlimit(RLIMIT_CORE, &lim) != 0)
    {
      perror("Couldn't unlimit core dump size");
      abort();
    }
  }
  else
  {
    lim.rlim_cur = 0;
    lim.rlim_max = 0;
    if (setrlimit(RLIMIT_CORE, &lim) != 0)
    {
      perror("Couldn't disable core dump size");
      abort();
    }
  }
#endif

#ifndef _WIN32
  if (cmdline.isset("segfault-handler"))
  {
    signal(SIGSEGV, segfault_handler);
    signal(SIGABRT, segfault_handler);
  }
#endif

  // If multi-property is on, we should set result-only and base-case
  if (cmdline.isset("multi-property"))
  {
    options.set_option("result-only", true);
    options.set_option("base-case", true);
  }

  /* compatibility: --cvc maps to --cvc4 */
  if (cmdline.isset("cvc"))
    options.set_option("cvc4", true);

  config.options = options;
}

// This is the main entry point of ESBMC. Here ESBMC performs initialisation
// of the algorithms that will be run over the GOTO program at later stages
//
//  1) Parse CMD                            (see "get_command_line_options")
//  2) Create and preprocess a GOTO program (see "get_goto_functions")
//  3) Set user-specified claims            (see "set_claims")
//  4) Perform Bounded Model Checking
//    - Run a particular verification strategy if specified
//      in CMD (see "do_bmc_strategy"), or
//    - Perform a single run of Bounded Model Checking and rely
//      on the simplifier to determine the sufficient verification bound
//      (see "do_bmc")
int esbmc_parseoptionst::doit()
{
  // Configure msg output
  if (cmdline.isset("file-output"))
  {
    FILE *f = fopen(cmdline.getval("file-output"), "w+");
    /* TODO: handle failure */
    out = f;
    messaget::state.out = f;
  }

  // Print a banner
  log_status(
    "ESBMC version {} {}-bit {} {}",
    ESBMC_VERSION,
    sizeof(void *) * 8,
    config.this_architecture(),
    config.this_operating_system());

  if (cmdline.isset("version"))
    return 0;

  // Unwinding of transition systems
  if (cmdline.isset("module") || cmdline.isset("gen-interface"))
  {
    log_error("This version has no support for hardware modules.");
    return 1;
  }

  // Preprocess the input program.
  // (This will not have any effect if OLD_FRONTEND is not enabled.)
  if (cmdline.isset("preprocess"))
  {
    preprocessing();
    return 0;
  }

  // Initialize goto_functions algorithms
  {
    // Loop unrolling
    if (cmdline.isset("goto-unwind") && !cmdline.isset("unwind"))
    {
      size_t unroll_limit = cmdline.isset("unlimited-goto-unwind") ? -1 : 1000;
      goto_preprocess_algorithms.push_back(
        std::make_unique<bounded_loop_unroller>(unroll_limit));
    }

    // Explicitly marking all declared variables as "nondet"
    goto_preprocess_algorithms.emplace_back(
      std::make_unique<mark_decl_as_non_det>(context));
  }

  // Run this before the main flow. This method performs its own
  // parsing and preprocessing.
  // This is an old implementation of parallel k-induction algorithm.
  // Eventually we will modify it and implement parallel version for all
  // available strategies. Just run it first before everything else
  // for now.
  if (cmdline.isset("k-induction-parallel"))
    return doit_k_induction_parallel();

  // Parse ESBMC options (CMD + set internal options)
  optionst options;
  get_command_line_options(options);

  // Create and preprocess a GOTO program
  if (get_goto_program(options, goto_functions))
    return 6;

  // Output claims about this program
  // (Fedor: should be moved to the output method perhaps)
  if (cmdline.isset("show-claims"))
  {
    const namespacet ns(context);
    show_claims(ns, goto_functions);
    return 0;
  }

  // Set user-specified claims
  // (Fedor: should be moved to the preprocessing method perhaps)
  if (set_claims(goto_functions))
    return 7;

  // Leave without doing any Bounded Model Checking
  if (options.get_bool_option("skip-bmc"))
    return 0;

  // Now run one of the chosen strategies
  if (
    cmdline.isset("termination") || cmdline.isset("incremental-bmc") ||
    cmdline.isset("falsification") || cmdline.isset("k-induction"))
    return do_bmc_strategy(options, goto_functions);

  // If no strategy is chosen, just rely on the simplifier
  // and the flags set through CMD
  bmct bmc(goto_functions, options, context);
  return do_bmc(bmc);
}

// This is the parallel version of k-induction algorithm.
// This is an old implementation and should be revisited sometime in the
// future.
int esbmc_parseoptionst::doit_k_induction_parallel()
{
#ifdef _WIN32
  log_error("Windows does not support parallel kind");
  abort();
#else
  // Pipes for communication between processes
  int forward_pipe[2], backward_pipe[2];

  // Process type
  PROCESS_TYPE process_type = PARENT;

  if (pipe(forward_pipe))
  {
    log_status("\nPipe Creation Failed, giving up.");
    _exit(1);
  }

  if (pipe(backward_pipe))
  {
    log_status("\nPipe Creation Failed, giving up.");
    _exit(1);
  }

  /* Set file descriptor non-blocking */
  fcntl(
    backward_pipe[0], F_SETFL, fcntl(backward_pipe[0], F_GETFL) | O_NONBLOCK);

  pid_t children_pid[3];
  short num_p = 0;

  // We need to fork 3 times: one for each step
  for (unsigned p = 0; p < 3; ++p)
  {
    pid_t pid = fork();

    if (pid == -1)
    {
      log_status("\nFork Failed, giving up.");
      _exit(1);
    }

    // Child process
    if (!pid)
    {
      process_type = PROCESS_TYPE(p);
      break;
    }
    // Parent process

    children_pid[p] = pid;
    ++num_p;
  }

  if (process_type == PARENT && num_p != 3)
  {
    log_error("Child processes were not created sucessfully.");
    abort();
  }

  optionst options;

  if (process_type != PARENT)
  {
    // Get full set of options
    get_command_line_options(options);

    // Generate goto functions and set claims
    if (get_goto_program(options, goto_functions))
      return 6;

    if (cmdline.isset("show-claims"))
    {
      const namespacet ns(context);
      show_claims(ns, goto_functions);
      return 0;
    }

    if (set_claims(goto_functions))
      return 7;
  }

  // Get max number of iterations
  BigInt max_k_step = cmdline.isset("unlimited-k-steps")
                        ? UINT_MAX
                        : strtoul(cmdline.getval("max-k-step"), nullptr, 10);

  // Get the increment
  unsigned k_step_inc = strtoul(cmdline.getval("k-step"), nullptr, 10);

  // All processes were created successfully
  switch (process_type)
  {
  case PARENT:
  {
    // Communication to child processes
    close(forward_pipe[1]);
    close(backward_pipe[0]);

    struct resultt a_result;
    bool bc_finished = false, fc_finished = false, is_finished = false;
    BigInt bc_solution = max_k_step, fc_solution = max_k_step,
           is_solution = max_k_step;

    // Keep reading until we find an answer
    while (!(bc_finished && fc_finished && is_finished))
    {
      // Perform read and interpret the number of bytes read
      int read_size = read(forward_pipe[0], &a_result, sizeof(resultt));
      if (read_size != sizeof(resultt))
      {
        if (read_size == 0)
        {
          // Client hung up; continue on, but don't interpret the result.
          ;
        }
        else
        {
          // Invalid size read.
          log_error("Short read communicating with kinduction children");
          log_error("Size {}, expected {}", read_size, sizeof(resultt));
          abort();
        }
      }

      // Eventually the parent process will check if the child process is alive

      // Check base case process
      if (!bc_finished)
      {
        int status;
        pid_t result = waitpid(children_pid[0], &status, WNOHANG);
        if (result == 0)
        {
          // Child still alive
        }
        else if (result == -1)
        {
          // Error
        }
        else
        {
          log_warning("base case process crashed.");
          bc_finished = fc_finished = is_finished = true;
        }
      }

      // Check forward condition process
      if (!fc_finished)
      {
        int status;
        pid_t result = waitpid(children_pid[1], &status, WNOHANG);
        if (result == 0)
        {
          // Child still alive
        }
        else if (result == -1)
        {
          // Error
        }
        else
        {
          log_warning("forward condition process crashed.");
          fc_finished = bc_finished = is_finished = true;
        }
      }

      // Check inductive step process
      if (!is_finished)
      {
        int status;
        pid_t result = waitpid(children_pid[2], &status, WNOHANG);
        if (result == 0)
        {
          // Child still alive
        }
        else if (result == -1)
        {
          // Error
        }
        else
        {
          log_warning("inductive step process crashed.");
          is_finished = bc_finished = fc_finished = true;
        }
      }

      switch (a_result.type)
      {
      case BASE_CASE:
        bc_finished = true;
        bc_solution = a_result.k;
        break;

      case FORWARD_CONDITION:
        fc_finished = true;
        fc_solution = a_result.k;
        break;

      case INDUCTIVE_STEP:
        is_finished = true;
        is_solution = a_result.k;
        break;

      default:
        log_error("Message from unrecognized k-induction child process");
        abort();
      }

      // If either the base case found a bug or the forward condition
      // finds a solution, present the result
      if (bc_finished && (bc_solution != 0) && (bc_solution != max_k_step))
        break;

      // If the either the forward condition or inductive step finds a
      // solution, first check if base case couldn't find a bug in that code,
      // if there is no bug, inductive step can present the result
      if (fc_finished && (fc_solution != 0) && (fc_solution != max_k_step))
      {
        // If base case finished, then we can present the result
        if (bc_finished)
          break;

        // Otherwise, kill the inductive step process
        kill(children_pid[2], SIGKILL);

        // And ask base case for a solution

        // Struct to keep the result
        struct resultt r = {process_type, 0};

        r.k = fc_solution.to_uint64();

        // Write result
        auto const len = write(backward_pipe[1], &r, sizeof(r));
        assert(len == sizeof(r) && "short write");
        (void)len; //ndebug
      }

      if (is_finished && (is_solution != 0) && (is_solution != max_k_step))
      {
        // If base case finished, then we can present the result
        if (bc_finished)
          break;

        // Otherwise, kill the forward condition process
        kill(children_pid[1], SIGKILL);

        // And ask base case for a solution

        // Struct to keep the result
        struct resultt r = {process_type, 0};

        r.k = is_solution.to_uint64();

        // Write result
        auto const len = write(backward_pipe[1], &r, sizeof(r));
        assert(len == sizeof(r) && "short write");
        (void)len; //ndebug
      }
    }

    for (int i : children_pid)
      kill(i, SIGKILL);

    // Check if a solution was found by the base case
    if (bc_finished && (bc_solution != 0) && (bc_solution != max_k_step))
    {
      log_result(
        "\nBug found by the base case (k = {})\nVERIFICATION FAILED",
        bc_solution);
      return true;
    }

    // Check if a solution was found by the forward condition
    if (fc_finished && (fc_solution != 0) && (fc_solution != max_k_step))
    {
      // We should only present the result if the base case finished
      // and haven't crashed (if it crashed, bc_solution will be UINT_MAX
      if (bc_finished && (bc_solution != max_k_step))
      {
        log_success(
          "\nSolution found by the forward condition; "
          "all states are reachable (k = {:d})\n"
          "VERIFICATION SUCCESSFUL",
          fc_solution);
        return false;
      }
    }

    // Check if a solution was found by the inductive step
    if (is_finished && (is_solution != 0) && (is_solution != max_k_step))
    {
      // We should only present the result if the base case finished
      // and haven't crashed (if it crashed, bc_solution will be UINT_MAX
      if (bc_finished && (bc_solution != max_k_step))
      {
        log_success(
          "\nSolution found by the inductive step "
          "(k = {:d})\n"
          "VERIFICATION SUCCESSFUL",
          is_solution);
        return false;
      }
    }

    // Couldn't find a bug or a proof for the current deepth
    log_fail("\nVERIFICATION UNKNOWN");
    return false;
  }

  case BASE_CASE:
  {
    // Set that we are running base case
    options.set_option("base-case", true);
    options.set_option("forward-condition", false);
    options.set_option("inductive-step", false);

    options.set_option("no-unwinding-assertions", true);
    options.set_option("partial-loops", false);

    // Start communication to the parent process
    close(forward_pipe[0]);
    close(backward_pipe[1]);

    // Struct to keep the result
    struct resultt r = {process_type, 0};

    // Run bmc and only send results in two occasions:
    // 1. A bug was found, we send the step where it was found
    // 2. It couldn't find a bug
    for (BigInt k_step = 1; k_step <= max_k_step; k_step += k_step_inc)
    {
      bmct bmc(goto_functions, options, context);
      bmc.options.set_option("unwind", integer2string(k_step));

      log_status("Checking base case, k = {:d}\n", k_step);

      // If an exception was thrown, we should abort the process
      int res = smt_convt::P_ERROR;
      try
      {
        res = do_bmc(bmc);
      }
      catch (...)
      {
        break;
      }

      // Send information to parent if no bug was found
      if (res == smt_convt::P_SATISFIABLE)
      {
        r.k = k_step.to_uint64();

        // Write result
        auto const len = write(forward_pipe[1], &r, sizeof(r));
        assert(len == sizeof(r) && "short write");
        (void)len; //ndebug

        log_status("Base case process finished (bug found).\n");
        return true;
      }

      // Check if the parent process is asking questions

      // Perform read and interpret the number of bytes read
      struct resultt a_result;
      int read_size = read(backward_pipe[0], &a_result, sizeof(resultt));
      if (read_size != sizeof(resultt))
      {
        if (read_size == 0)
        {
          // Client hung up; continue on, but don't interpret the result.
          continue;
        }
        if (read_size == -1 && errno == EAGAIN)
        {
          // No data available yet
          continue;
        }
        else
        {
          // Invalid size read.
          log_error("Short read communicating with kinduction parent");
          log_error("Size {}, expected {}", read_size, sizeof(resultt));

          abort();
        }
      }

      // We only receive messages from the parent
      assert(a_result.type == PARENT);

      // If the value being asked is greater or equal the current step,
      // then we can stop the base case. It can be equal, because we
      // have just checked the current value of k
      if (a_result.k < k_step)
        break;

      // Otherwise, we just need to check the base case for k = a_result.k
      max_k_step = a_result.k + k_step_inc;
    }

    // Send information to parent that a bug was not found
    r.k = 0;

    auto const len = write(forward_pipe[1], &r, sizeof(r));
    assert(len == sizeof(r) && "short write");
    (void)len; //ndebug

    log_status("Base case process finished (no bug found).\n");
    return false;
  }

  case FORWARD_CONDITION:
  {
    // Set that we are running forward condition
    options.set_option("base-case", false);
    options.set_option("forward-condition", true);
    options.set_option("inductive-step", false);

    options.set_option("no-unwinding-assertions", false);
    options.set_option("partial-loops", false);
    options.set_option("no-assertions", true);

    // Start communication to the parent process
    close(forward_pipe[0]);
    close(backward_pipe[1]);

    // Struct to keep the result
    struct resultt r = {process_type, 0};

    // Run bmc and only send results in two occasions:
    // 1. A proof was found, we send the step where it was found
    // 2. It couldn't find a proof
    for (BigInt k_step = 2; k_step <= max_k_step; k_step += k_step_inc)
    {
      bmct bmc(goto_functions, options, context);
      bmc.options.set_option("unwind", integer2string(k_step));

      log_status("Checking forward condition, k = {:d}", k_step);

      // If an exception was thrown, we should abort the process
      int res = smt_convt::P_ERROR;
      try
      {
        res = do_bmc(bmc);
      }
      catch (...)
      {
        break;
      }

      if (options.get_bool_option("disable-forward-condition"))
        break;

      // Send information to parent if no bug was found
      if (res == smt_convt::P_UNSATISFIABLE)
      {
        r.k = k_step.to_uint64();

        // Write result
        auto const len = write(forward_pipe[1], &r, sizeof(r));
        assert(len == sizeof(r) && "short write");
        (void)len; //ndebug

        log_status("Forward condition process finished (safety proven).");
        return false;
      }
    }

    // Send information to parent that it couldn't prove the code
    r.k = 0;

    auto const len = write(forward_pipe[1], &r, sizeof(r));
    assert(len == sizeof(r) && "short write");
    (void)len; //ndebug

    log_status("Forward condition process finished (safety not proven).");
    return true;
  }

  case INDUCTIVE_STEP:
  {
    // Set that we are running inductive step
    options.set_option("base-case", false);
    options.set_option("forward-condition", false);
    options.set_option("inductive-step", true);

    options.set_option("no-unwinding-assertions", true);
    options.set_option("partial-loops", true);

    // Start communication to the parent process
    close(forward_pipe[0]);
    close(backward_pipe[1]);

    // Struct to keep the result
    struct resultt r = {process_type, 0};

    // Run bmc and only send results in two occasions:
    // 1. A proof was found, we send the step where it was found
    // 2. It couldn't find a proof
    for (BigInt k_step = 2; k_step <= max_k_step; k_step += k_step_inc)
    {
      bmct bmc(goto_functions, options, context);

      bmc.options.set_option("unwind", integer2string(k_step));

      log_status("Checking inductive step, k = {:d}", k_step);

      // If an exception was thrown, we should abort the process
      int res = smt_convt::P_ERROR;
      try
      {
        res = do_bmc(bmc);
      }
      catch (...)
      {
        break;
      }

      if (options.get_bool_option("disable-inductive-step"))
        break;

      // Send information to parent if no bug was found
      if (res == smt_convt::P_UNSATISFIABLE)
      {
        r.k = k_step.to_uint64();

        // Write result
        auto const len = write(forward_pipe[1], &r, sizeof(r));
        assert(len == sizeof(r) && "short write");
        (void)len; //ndebug

        log_status("Inductive process finished (safety proven).");
        return false;
      }
    }

    // Send information to parent that it couldn't prove the code
    r.k = 0;

    auto const len = write(forward_pipe[1], &r, sizeof(r));
    assert(len == sizeof(r) && "short write");
    (void)len; //ndebug

    log_status("Inductive process finished (safety not proven).");
    return true;
  }

  default:
    assert(0 && "Unknown process type.");
  }

#endif

  return 0;
}

// This method iteratively applies one of the verification strategies
// for different unwinding bounds up to the specified maximum depth.
//
// ESBMC features 4 verification strategies:
//
//  1) Incremental
//  2) Termination
//  3) Falsification
//  4) k-induction
//
// Applying a strategy in this context means solving a paticular sequence
// of decision problems from the list below for the given unwinding bound k:
//
//  - Base case             (see "is_base_case_violated")
//  - Forward condition     (see "does_forward_condition_hold")
//  - Inductive step        (see "is_inductive_step_violated")
//
// \param options - options for setting the verification strategy
// and conrolling symbolic execution
// \param goto_functions - GOTO program under verification
int esbmc_parseoptionst::do_bmc_strategy(
  optionst &options,
  goto_functionst &goto_functions)
{
  // Get max number of iterations
  BigInt max_k_step = cmdline.isset("unlimited-k-steps")
                        ? UINT_MAX
                        : strtoul(cmdline.getval("max-k-step"), nullptr, 10);

  // Get the increment
  unsigned k_step_inc = strtoul(cmdline.getval("k-step"), nullptr, 10);

  // Trying all bounds from 1 to "max_k_step" in "k_step_inc"
  for (BigInt k_step = 1; k_step <= max_k_step; k_step += k_step_inc)
  {
    // k-induction
    if (options.get_bool_option("k-induction"))
    {
      if (
        is_base_case_violated(options, goto_functions, k_step).is_true() &&
        !cmdline.isset("multi-property"))
        return 1;

      if (does_forward_condition_hold(options, goto_functions, k_step)
            .is_false())
        return 0;

      // Don't run inductive step for k_step == 1
      if (k_step > 1)
      {
        if (is_inductive_step_violated(options, goto_functions, k_step)
              .is_false())
          return 0;
      }
    }
    // termination
    if (options.get_bool_option("termination"))
    {
      if (does_forward_condition_hold(options, goto_functions, k_step)
            .is_false())
        return 0;

      /* Disable this for now as it is causing more than 100 errors on SV-COMP
      if(!is_inductive_step_violated(options, goto_functions, k_step))
        return false;
      */
    }
    // incremental-bmc
    if (options.get_bool_option("incremental-bmc"))
    {
      if (
        is_base_case_violated(options, goto_functions, k_step).is_true() &&
        !cmdline.isset("multi-property"))
        return 1;

      if (does_forward_condition_hold(options, goto_functions, k_step)
            .is_false())
        return 0;
    }
    // falsification
    if (options.get_bool_option("falsification"))
    {
      if (is_base_case_violated(options, goto_functions, k_step).is_true())
        return 1;
    }
  }

  log_status("Unable to prove or falsify the program, giving up.");
  log_fail("VERIFICATION UNKNOWN");
  return 0;
}

// This checks whether "there is a set of inputs that reaches and violates
// an assertion when all the loops in the verified program are unwound up to
// the given bound k".
//
// \param options - options for controlling the symbolic execution
// \param goto_function - GOTO program under investigation
// \param k_step - depth to which all loops in the program are unrolled
// \return
//    TV_TRUE if such assertion violation (i.e., a bug) is found,
//    TV_FALSE if all reachable assertions hold for all input values
// in "goto_functions" with all its loops unrolled up to "k_step",
//    TV_UNKNOWN - otherwise.
tvt esbmc_parseoptionst::is_base_case_violated(
  optionst &options,
  goto_functionst &goto_functions,
  const BigInt &k_step)
{
  options.set_option("base-case", true);
  options.set_option("forward-condition", false);
  options.set_option("inductive-step", false);
  options.set_option("no-unwinding-assertions", true);
  options.set_option("partial-loops", false);
  options.set_option("unwind", integer2string(k_step));

  bmct bmc(goto_functions, options, context, to_remove_claims);

  log_status("Checking base case, k = {:d}", k_step);
  switch (do_bmc(bmc))
  {
  case smt_convt::P_UNSATISFIABLE:
    return tvt(tvt::TV_FALSE);

  case smt_convt::P_SMTLIB:
  case smt_convt::P_ERROR:
    break;

  case smt_convt::P_SATISFIABLE:
    log_result("\nBug found (k = {:d})", k_step);
    return tvt(tvt::TV_TRUE);

  default:
    log_result("Unknown BMC result");
    abort();
  }

  return tvt(tvt::TV_UNKNOWN);
}

// This checks whether "there is a set of inputs for which one of the loop
// conditions is still satisfied after it has been executed
// (i.e., unrolled) at least k times".
//
// \param options - options for controlling the symbolic execution
// \param goto_function - GOTO program under investigation
// \param k_step - depth to which all loops in the program are unrolled
// \return
//    TV_TRUE if there is a set of input values for which at least
// one of the loops in the program can be executed more than "k_step" times.
//    TV_FALSE if all reachable loops have at most "k_step" iterations
// for all input values in "goto_functions".
//    TV_UNKNOWN - otherwise.
tvt esbmc_parseoptionst::does_forward_condition_hold(
  optionst &options,
  goto_functionst &goto_functions,
  const BigInt &k_step)
{
  if (options.get_bool_option("disable-forward-condition"))
    return tvt(tvt::TV_UNKNOWN);

  options.set_option("base-case", false);
  options.set_option("forward-condition", true);
  options.set_option("inductive-step", false);
  options.set_option("no-unwinding-assertions", false);
  options.set_option("partial-loops", false);

  // We have to disable assertions in the forward condition but
  // restore the previous value after it
  bool no_assertions = options.get_bool_option("no-assertions");

  // Turn assertions off
  options.set_option("no-assertions", true);
  options.set_option("unwind", integer2string(k_step));

  bmct bmc(goto_functions, options, context);

  log_progress("Checking forward condition, k = {:d}", k_step);
  auto res = do_bmc(bmc);

  // Restore the no assertion flag, before checking the other steps
  options.set_option("no-assertions", no_assertions);

  switch (res)
  {
  case smt_convt::P_SATISFIABLE:
    return tvt(tvt::TV_TRUE);

  case smt_convt::P_SMTLIB:
  case smt_convt::P_ERROR:
    break;

  case smt_convt::P_UNSATISFIABLE:
    log_result(
      "\nSolution found by the forward condition; "
      "all states are reachable (k = {:d})",
      k_step);
    return tvt(tvt::TV_FALSE);

  default:
    log_fail("Unknown BMC result");
    abort();
  }

  return tvt(tvt::TV_UNKNOWN);
}

// This tries to prove the inductive step: "assuming nondeterministic
// inputs for every loop, and assuming that all assertions hold for
// the first k iterations of every loop, all assertions will also hold
// when all loops in the program are unrolled to k+1."
// ("Loop inputs" are the variables whose values change inside the loop.)
//
// \param options - options for controlling the symbolic execution
// \param goto_function - GOTO program under investigation
// \param k_step - depth to which all loops in the program are unrolled
// \return -
//    TV_TRUE if there is a set of values for which all assertions in
// all loops hold for the first "k" iterations but not one of the assertions in
// one of the loops is violated during the "k+1" iterations.
//    TV_FALSE if the the inductive step holds.
//    TV_UNKNOWN - otherwise.
tvt esbmc_parseoptionst::is_inductive_step_violated(
  optionst &options,
  goto_functionst &goto_functions,
  const BigInt &k_step)
{
  if (options.get_bool_option("disable-inductive-step"))
    return tvt(tvt::TV_UNKNOWN);

  if (
    strtoul(cmdline.getval("max-inductive-step"), nullptr, 10) <
    k_step.to_uint64())
    return tvt(tvt::TV_UNKNOWN);

  options.set_option("base-case", false);
  options.set_option("forward-condition", false);
  options.set_option("inductive-step", true);
  options.set_option("no-unwinding-assertions", true);
  options.set_option("partial-loops", true);
  options.set_option("unwind", integer2string(k_step));

  bmct bmc(goto_functions, options, context);

  log_progress("Checking inductive step, k = {:d}", k_step);
  switch (do_bmc(bmc))
  {
  case smt_convt::P_SATISFIABLE:
    return tvt(tvt::TV_TRUE);

  case smt_convt::P_SMTLIB:
  case smt_convt::P_ERROR:
    break;

  case smt_convt::P_UNSATISFIABLE:
    log_result(
      "\nSolution found by the inductive step "
      "(k = {:d})",
      k_step);
    return tvt(tvt::TV_FALSE);

  default:
    log_fail("Unknown BMC result\n");
    abort();
  }

  return tvt(tvt::TV_UNKNOWN);
}

// This is a wrapper method that does a single round of
// symbolic execution of the given GOTO program and creates
// a decision problem specified by the verification options.
// In brief, they are used to control what assertions and
// assumptions are injected into the verified bounded trace
// during symbolic execution.
//
// \param bmc - the bmc object that contains all the necessary
// information (see below) to perform a single run of Bounded Model Checking:
//
//  1) GOTO program,
//  2) verification options.
//  3) program context,
int esbmc_parseoptionst::do_bmc(bmct &bmc)
{
  log_progress("Starting Bounded Model Checking");

  smt_convt::resultt res = bmc.start_bmc();
  if (res == smt_convt::P_ERROR)
    abort();

#ifdef HAVE_SENDFILE_ESBMC
  if (bmc.options.get_bool_option("memstats"))
  {
    int fd = open("/proc/self/status", O_RDONLY);
    sendfile(2, fd, nullptr, 100000);
    close(fd);
  }
#endif

  return res;
}

bool esbmc_parseoptionst::set_claims(goto_functionst &goto_functions)
{
  try
  {
    if (cmdline.isset("claim"))
      ::set_claims(goto_functions, cmdline.get_values("claim"));
  }

  catch (const char *e)
  {
    log_error("{}", e);
    return true;
  }

  catch (const std::string &e)
  {
    log_error("{}", e);
    return true;
  }

  catch (int)
  {
    return true;
  }

  return false;
}

// This method performs a wide range of actions that can be broadly divided
// into 3 main steps:
//
//  1) creating a GOTO program,
//  2) processing the GOTO program, and
//  3) outputting the GOTO program.
//
// This method is typically used as the second stage
// (right after parsing the command line options) by the verification methods
// (i.e., BMC, k-induction, etc).
//
// \param options - various options used during the above steps,
// \param goto_functions - the "created and processed" GOTO program.
bool esbmc_parseoptionst::get_goto_program(
  optionst &options,
  goto_functionst &goto_functions)
{
  try
  {
    fine_timet create_start = current_time();
    if (create_goto_program(options, goto_functions))
      return true;
    fine_timet create_stop = current_time();
    log_status(
      "GOTO program creation time: {}s",
      time2string(create_stop - create_start));

    fine_timet process_start = current_time();
    if (process_goto_program(options, goto_functions))
      return true;
    fine_timet process_stop = current_time();
    log_status(
      "GOTO program processing time: {}s",
      time2string(process_stop - process_start));
    if (output_goto_program(options, goto_functions))
      return true;
  }

  catch (const char *e)
  {
    log_error("{}", e);
    return true;
  }

  catch (const std::string &e)
  {
    log_error("{}", e);
    return true;
  }

  catch (std::bad_alloc &)
  {
    log_error("Out of memory");
    return true;
  }

  return false;
}

// This method creates a GOTO program from the source specified by the
// command line options. A GOTO program can be created:
//
//  1) from a GOTO binary file,
//  2) by parsing the input program files.
//
// \param options - options to be passed through,
// \param goto_functions - this is where the created GOTO program is stored.
bool esbmc_parseoptionst::create_goto_program(
  optionst &options,
  goto_functionst &goto_functions)
{
  try
  {
    if (cmdline.args.size() == 0)
    {
      log_error("Please provide a program to verify");
      return true;
    }

    // If the user is providing the GOTO functions, we don't need to parse
    if (cmdline.isset("binary"))
    {
      if (read_goto_binary(goto_functions))
        return true;
    }
    else
    {
      if (parse_goto_program(options, goto_functions))
        return true;
    }
  }

  catch (const char *e)
  {
    log_error("{}", e);
    return true;
  }

  catch (const std::string &e)
  {
    log_error("{}", e);
    return true;
  }

  catch (std::bad_alloc &)
  {
    log_error("Out of memory");
    return true;
  }

  return false;
}

// This method creates a GOTO program from the given GOTO binary.
//
// \param goto_functions - this is where the created GOTO program is stored.
bool esbmc_parseoptionst::read_goto_binary(goto_functionst &goto_functions)
{
  log_progress("Reading GOTO program from file");
  goto_binary_reader goto_reader;
  for (const auto &arg : cmdline.args)
  {
    if (goto_reader.read_goto_binary(arg, context, goto_functions))
    {
      log_error("Failed to open `{}'", arg);
      return true;
    }
  }

  return false;
}

// This method creates a GOTO program by parsing the input program files.
//
// \param options - options to be passed to the program parser,
// \param goto_functions - this is where the created GOTO program is stored.
bool esbmc_parseoptionst::parse_goto_program(
  optionst &options,
  goto_functionst &goto_functions)
{
  try
  {
    if (parse(cmdline))
      return true;

    if (cmdline.isset("parse-tree-too") || cmdline.isset("parse-tree-only"))
    {
      std::ostringstream oss;
      for (auto &it : langmap)
        it.second->show_parse(oss);
      log_status("{}", oss.str());
      if (cmdline.isset("parse-tree-only"))
        return true;
    }

    // Typecheking (old frontend) or adjust (clang frontend)
    if (typecheck())
      return true;
    if (final())
      return true;

    // we no longer need any parse trees or language files
    clear_parse();

    if (cmdline.isset("symbol-table-too") || cmdline.isset("symbol-table-only"))
    {
      std::ostringstream oss;
      show_symbol_table_plain(oss);
      log_status("{}", oss.str());
      if (cmdline.isset("symbol-table-only"))
        return true;
    }

    log_progress("Generating GOTO Program");
    goto_convert(context, options, goto_functions);
  }

  catch (const char *e)
  {
    log_error("{}", e);
    return true;
  }

  catch (const std::string &e)
  {
    log_error("{}", e);
    return true;
  }

  catch (std::bad_alloc &)
  {
    log_error("Out of memory");
    return true;
  }

  return false;
}

// This method performs various analyses and transformations
// on the given GOTO program. They involve all the techniques that we class
// as "static analyses" - performed on the given GOTO program before it is
// symbolically executed. Examples of such techniques include:
//
//  - interval analysis,
//  - removal of unreachable code,
//  - preprocessing the program for k-induction,
//  - applying GOTO contractors,
//  - ...
//
// \param options - various options used by the processing methods,
// \param goto_functions - reference to the GOTO program to be processed.
bool esbmc_parseoptionst::process_goto_program(
  optionst &options,
  goto_functionst &goto_functions)
{
  try
  {
    namespacet ns(context);

    bool is_no_remove = cmdline.isset("multi-property") ||
                        cmdline.isset("assertion-coverage") ||
                        cmdline.isset("assertion-coverage-claims") ||
                        cmdline.isset("condition-coverage") ||
                        cmdline.isset("condition-coverage-claims");

    // Start by removing all no-op instructions and unreachable code
    if (!(cmdline.isset("no-remove-no-op")))
      remove_no_op(goto_functions);

    // We should skip this 'remove-unreachable' removal in goto-cov and multi-property
    // - multi-property wants to find all the bugs in the src code
    // - assertion-coverage wants to find out unreached codes (asserts)
    // - however, the optimisation below will remove codes during the Goto stage
    if (!(cmdline.isset("no-remove-unreachable") || is_no_remove))
      remove_unreachable(goto_functions);

    // Apply all the initialized algorithms
    for (auto &algorithm : goto_preprocess_algorithms)
      algorithm->run(goto_functions);

    // do partial inlining
    if (!cmdline.isset("no-inlining"))
    {
      if (cmdline.isset("full-inlining"))
        goto_inline(goto_functions, options, ns);
      else
        goto_partial_inline(goto_functions, options, ns);
    }

    if (cmdline.isset("gcse"))
    {
      std::shared_ptr<value_set_analysist> vsa =
        std::make_shared<value_set_analysist>(ns);
      try
      {
        log_status("Computing Value-Set Analysis (VSA)");
        (*vsa)(goto_functions);
      }
      catch (vsa_not_implemented_exception &)
      {
        log_warning(
          "Unable to compute VSA due to incomplete implementation. Some GOTO "
          "optimizations will be disabled");
        vsa = nullptr;
      }
      catch (type2t::symbolic_type_excp &)
      {
        log_warning(
          "[GOTO] Unable to compute VSA due to symbolic type. Some GOTO "
          "optimizations will be disabled");
        vsa = nullptr;
      }

      if (cmdline.isset("no-library"))
        log_warning("Using CSE with --no-library might cause huge slowdowns!");

      if (!vsa)
        log_warning("Could not apply GCSE optimization due to VSA limitation!");
      else
      {
        goto_cse cse(context, vsa);
        cse.run(goto_functions);
      }
    }

    if (cmdline.isset("interval-analysis") || cmdline.isset("goto-contractor"))
    {
      interval_analysis(goto_functions, ns, options);
    }

    if (
      cmdline.isset("inductive-step") || cmdline.isset("k-induction") ||
      cmdline.isset("k-induction-parallel"))
    {
      // Always remove skips before doing k-induction.
      // It seems to fix some issues for now
      remove_no_op(goto_functions);
      goto_k_induction(goto_functions);
    }

    if (
      cmdline.isset("goto-contractor") ||
      cmdline.isset("goto-contractor-condition"))
    {
#ifdef ENABLE_GOTO_CONTRACTOR
      goto_contractor(goto_functions, ns, options);
#else
      log_error(
        "Current build does not support contractors. If ibex is installed, add "
        "to your build process "
        "-DENABLE_GOTO_CONTRACTOR=ON -DIBEX_DIR=path-to-ibex");
      abort();
#endif
    }

    if (cmdline.isset("termination"))
      goto_termination(goto_functions);

    goto_check(ns, options, goto_functions);

    // add re-evaluations of monitored properties
    add_property_monitors(goto_functions, ns);

    // Once again, remove all unreachable and no-op code that could have been
    // introduced by the above algorithms
    if (!(cmdline.isset("no-remove-no-op")))
      remove_no_op(goto_functions);

    if (!(cmdline.isset("no-remove-unreachable") || is_no_remove))
      remove_unreachable(goto_functions);

    goto_functions.update();

    if (cmdline.isset("data-races-check"))
    {
      log_status("Adding Data Race Checks");

      value_set_analysist value_set_analysis(ns);
      value_set_analysis(goto_functions);

      add_race_assertions(value_set_analysis, context, goto_functions);

      value_set_analysis.update(goto_functions);
    }

    //! goto-cov will also mutate the asserts added by esbmc (e.g. goto-check)
    if (
      cmdline.isset("assertion-coverage") ||
      cmdline.isset("assertion-coverage-claims"))
    {
      // for multi-property
      options.set_option("result-only", true);
      options.set_option("base-case", true);
      options.set_option("multi-property", true);
      options.set_option("keep-verified-claims", false);

      goto_coveraget tmp(ns, goto_functions);
      tmp.replace_all_asserts_to_guard(gen_false_expr(), true);
    }

    if (
      cmdline.isset("condition-coverage") ||
      cmdline.isset("condition-coverage-claims") ||
      cmdline.isset("condition-coverage-rm") ||
      cmdline.isset("condition-coverage-claims-rm"))
    {
      // for multi-property
      options.set_option("result-only", true);
      options.set_option("base-case", true);
      options.set_option("multi-property", true);
      options.set_option("keep-verified-claims", false);
      // unreachable conditions should be also considered as short-circuited

      //?:
      // if we do not want expressions like 'if(2 || 3)' get simplified to 'if(1||1)'
      // we need to enable the options below:
      //    options.set_option("no-simplify", true);
      //    options.set_option("no-propagation", true);
      // however, this will affect the performance, thus they are not enabled by default

      std::string filename = cmdline.args[0];
      goto_coveraget tmp(ns, goto_functions, filename);
      tmp.replace_all_asserts_to_guard(gen_true_expr());
      tmp.gen_cond_cov();
    }

    if (options.get_bool_option("make-assert-false"))
    {
      goto_coveraget tmp(ns, goto_functions);
      tmp.replace_all_asserts_to_guard(gen_false_expr());
    }

    if (cmdline.isset("add-false-assert"))
    {
      goto_coveraget tmp(ns, goto_functions);
      tmp.add_false_asserts();
    }
  }

  catch (const char *e)
  {
    log_error("{}", e);
    return true;
  }

  catch (const std::string &e)
  {
    log_error("{}", e);
    return true;
  }

  catch (std::bad_alloc &)
  {
    log_error("Out of memory");
    return true;
  }

  return false;
}

// This method provides different output methods for the given GOTO program.
// Depending on the provided options this method can:
//
//  - output the given GOTO program as text,
//  - translate the provided GOTO program into C,
//  - create a GOTO binary from this GOTO program,
//  - methods outputting some additional information of the GOTO program.
//
// \param options - various options setting the output methods,
// \param goto_functions - the GOTO program to be output.
bool esbmc_parseoptionst::output_goto_program(
  optionst &options,
  goto_functionst &goto_functions)
{
  try
  {
    namespacet ns(context);

    // show it?
    if (cmdline.isset("show-loops"))
    {
      show_loop_numbers(goto_functions);
      return true;
    }

    // show it?
    if (cmdline.isset("show-goto-value-sets"))
    {
      value_set_analysist value_set_analysis(ns);
      value_set_analysis(goto_functions);
      std::ostringstream oss;
      show_value_sets(goto_functions, value_set_analysis, oss);
      log_result("{}", oss.str());
      return true;
    }

    // Write the GOTO program into a binary
    if (cmdline.isset("output-goto"))
    {
      log_status("Writing GOTO program to file");
      std::ofstream oss(
        cmdline.getval("output-goto"), std::ios::out | std::ios::binary);
      if (write_goto_binary(oss, context, goto_functions))
      {
        log_error("Failed to generate goto binary file"); // TODO: explain why
        abort();
      };
      return true;
    }

    if (cmdline.isset("show-ileave-points"))
    {
      print_ileave_points(ns, goto_functions);
      return true;
    }

    // Output the GOTO program to the log (and terminate or continue) in
    // a human-readable format
    if (
      cmdline.isset("goto-functions-too") ||
      cmdline.isset("goto-functions-only"))
    {
      std::ostringstream oss;
      goto_functions.output(ns, oss);
      log_status("{}", oss.str());
      if (cmdline.isset("goto-functions-only"))
        return true;
    }

    if (cmdline.isset("dump-goto-cfg"))
    {
      goto_cfg cfg(goto_functions);
      cfg.dump_graph();
      return true;
    }

    // Translate the GOTO program to C and output it into the log or
    // a specified output file
    if (cmdline.isset("goto2c"))
    {
      // Creating a translator here
      goto2ct goto2c(ns, goto_functions);
      goto2c.preprocess();
      goto2c.check();
      std::string res = goto2c.translate();

      const std::string &filename = options.get_option("output");
      if (!filename.empty())
      {
        // Outputting the translated program into the output file
        std::ofstream out(filename);
        assert(out);
        out << res;
      }
      else
        std::cout << res;
      return true;
    }
  }

  catch (const char *e)
  {
    log_error("{}", e);
    return true;
  }

  catch (const std::string &e)
  {
    log_error("{}", e);
    return true;
  }

  return false;
}

// This performs the preprocessing of the input program
// when the old C/C++ frontend (i.e., from "ansi-c/" or "cpp/") is used.
void esbmc_parseoptionst::preprocessing()
{
  try
  {
    if (cmdline.args.size() != 1)
    {
      log_error("Please provide one program to preprocess");
      return;
    }

    std::string filename = cmdline.args[0];

    // To test that the file exists,
    std::ifstream infile(filename.c_str());
    if (!infile)
    {
      log_error("failed to open input file");
      return;
    }
#ifdef ENABLE_OLD_FRONTEND
    std::ostringstream oss;
    if (c_preprocess(filename, oss, false))
      log_error("PREPROCESSING ERROR");
    log_status("{}", oss.str());
#endif
  }
  catch (const char *e)
  {
    log_error("{}", e);
  }

  catch (const std::string &e)
  {
    log_error("{}", e);
  }

  catch (std::bad_alloc &)
  {
    log_error("Out of memory");
  }
}

void esbmc_parseoptionst::add_property_monitors(
  goto_functionst &goto_functions,
  namespacet &ns [[maybe_unused]])
{
  std::map<std::string, std::pair<std::set<std::string>, expr2tc>> monitors;

  context.foreach_operand([this, &monitors](const symbolt &s) {
    if (
      !has_prefix(s.name, "__ESBMC_property_") ||
      s.name.as_string().find("$type") != std::string::npos)
      return;

    // strip prefix "__ESBMC_property_"
    std::string prop_name = s.name.as_string().substr(17);
    std::set<std::string> used_syms;
    expr2tc main_expr = calculate_a_property_monitor(prop_name, used_syms);
    monitors[prop_name] = std::pair{used_syms, main_expr};
  });

  if (monitors.size() == 0)
    return;

  Forall_goto_functions (f_it, goto_functions)
  {
    /* do not instrument global entry function */
    if (f_it->first == "__ESBMC_main")
      continue;

    /* do also not instrument functions computing the propositions themselves */
    if (has_prefix(f_it->first, "c:@F@") && has_suffix(f_it->first, "_status"))
    {
      const std::string &name = f_it->first.as_string();
      std::string prop_name = name.substr(5, name.length() - 5 - 7);
      if (monitors.find(prop_name) != monitors.end())
        continue;
    }

    log_debug("ltl", "adding monitor exprs in function {}", f_it->first);
    goto_functiont &func = f_it->second;
    goto_programt &prog = func.body;
    Forall_goto_program_instructions (p_it, prog)
      add_monitor_exprs(p_it, prog.instructions, monitors);
  }

  // Find main function; find first function call; insert updates to each
  // property expression. This makes sure that there isn't inconsistent
  // initialization of each monitor boolean.
  goto_functionst::function_mapt::iterator f_it =
    goto_functions.function_map.find("__ESBMC_main");
  assert(f_it != goto_functions.function_map.end());
  std::string main_suffix = "@" + (config.main.empty() ? "main" : config.main);
  const symbol2t *entry_sym = nullptr;
  Forall_goto_program_instructions (p_it, f_it->second.body)
  {
    /* Find the call to the entry point, usually 'main'. At that point
     * everything like pthreads, etc., is already set up. */
    if (p_it->type != FUNCTION_CALL)
      continue;
    const code_function_call2t &func_call = to_code_function_call2t(p_it->code);
    if (!is_symbol2t(func_call.function))
      continue;
    const symbol2t &func_sym = to_symbol2t(func_call.function);
    if (!has_suffix(func_sym.thename, main_suffix))
      continue;

    /* found it */
    entry_sym = &func_sym;
    break;
  }
  assert(entry_sym);

  f_it = goto_functions.function_map.find(entry_sym->thename.as_string());
  assert(f_it != goto_functions.function_map.end());

  goto_programt &body = f_it->second.body;
  goto_programt::instructionst &insn_list = body.instructions;

  /* insert a call to start the monitor thread and after it also to kill it */
  goto_programt::instructiont new_insn;
  new_insn.function = entry_sym->thename;

  expr2tc func_sym = symbol2tc(get_empty_type(), "c:@F@ltl2ba_start_monitor");
  std::vector<expr2tc> args;
  new_insn.make_function_call(code_function_call2tc(expr2tc(), func_sym, args));
  insn_list.insert(insn_list.begin(), new_insn);

  func_sym = symbol2tc(get_empty_type(), "c:@F@ltl2ba_finish_monitor");
  new_insn.make_function_call(code_function_call2tc(expr2tc(), func_sym, args));
  // add this call before each 'return' instruction
  for (auto it = insn_list.begin(); it != insn_list.end(); ++it)
  {
    if (it->type != RETURN)
      continue;
    insn_list.insert(it, new_insn);
  }
}

static void collect_symbol_names(
  const expr2tc &e,
  const std::string &prefix,
  std::set<std::string> &used_syms)
{
  if (is_symbol2t(e))
  {
    const symbol2t &thesym = to_symbol2t(e);
    assert(thesym.rlevel == 0);
    std::string sym = thesym.get_symbol_name();

    used_syms.insert(sym);
  }
  else
  {
    e->foreach_operand([&prefix, &used_syms](const expr2tc &e) {
      if (!is_nil_expr(e))
        collect_symbol_names(e, prefix, used_syms);
    });
  }
}

expr2tc esbmc_parseoptionst::calculate_a_property_monitor(
  const std::string &name,
  std::set<std::string> &used_syms) const
{
  const symbolt *fn = context.find_symbol("c:@F@" + name + "_status");
  assert(fn);

  const codet &fn_code = to_code(fn->value);
  assert(fn_code.get_statement() == "block");
  assert(fn_code.operands().size() == 1);

  const codet &fn_ret = to_code(fn_code.op0());
  assert(fn_ret.get_statement() == "return");
  assert(fn_ret.operands().size() == 1);

  expr2tc new_main_expr;
  migrate_expr(fn_ret.op0(), new_main_expr);

  collect_symbol_names(new_main_expr, name, used_syms);

  return new_main_expr;
}

void esbmc_parseoptionst::add_monitor_exprs(
  goto_programt::targett insn,
  goto_programt::instructionst &insn_list,
  const std::map<std::string, std::pair<std::set<std::string>, expr2tc>>
    &monitors)
{
  // We've been handed an instruction, look for assignments to the
  // symbol we're looking for. When we find one, append a goto instruction that
  // re-evaluates a proposition expression. Because there can be more than one,
  // we put re-evaluations in atomic blocks.

  if (!insn->is_assign())
    return;

  code_assign2t &assign = to_code_assign2t(insn->code);

  // Don't allow propositions about things like the contents of an array and
  // suchlike.
  if (!is_symbol2t(assign.target))
    return;

  symbol2t &sym = to_symbol2t(assign.target);

  // Is this actually an assignment that we're interested in?
  std::string sym_name = sym.get_symbol_name();
  std::set<std::pair<std::string, expr2tc>> triggered;
  for (const auto &[prop, pair] : monitors)
    if (pair.first.find(sym_name) != pair.first.end())
      triggered.emplace(prop, pair.second);

  if (triggered.empty())
    return;

  goto_programt::instructiont new_insn;

  new_insn.type = ATOMIC_BEGIN;
  new_insn.function = insn->function;
  insn_list.insert(insn, new_insn);

  insn++;

#if 0
  new_insn.type = FUNCTION_CALL;
  expr2tc func_sym =
    symbol2tc(get_empty_type(), "c:@F@__ESBMC_switch_to_monitor");
  std::vector<expr2tc> args;
  new_insn.code = code_function_call2tc(expr2tc(), func_sym, args);
  new_insn.function = insn->function;
  insn_list.insert(insn, new_insn);
#endif

  new_insn.type = ATOMIC_END;
  new_insn.function = insn->function;
  insn_list.insert(insn, new_insn);
}

static unsigned int calc_globals_used(const namespacet &ns, const expr2tc &expr)
{
  if (is_nil_expr(expr))
    return 0;

  if (!is_symbol2t(expr))
  {
    unsigned int globals = 0;

    expr->foreach_operand([&globals, &ns](const expr2tc &e) {
      globals += calc_globals_used(ns, e);
    });

    return globals;
  }

  std::string identifier = to_symbol2t(expr).get_symbol_name();

  if (
    identifier == "NULL" || identifier == "__ESBMC_alloc" ||
    identifier == "__ESBMC_alloc_size")
    return 0;

  const symbolt *sym = ns.lookup(identifier);
  assert(sym);
  if (sym->static_lifetime || sym->type.is_dynamic_set())
    return 1;

  return 0;
}

void esbmc_parseoptionst::print_ileave_points(
  namespacet &ns,
  goto_functionst &goto_functions)
{
  forall_goto_functions (fit, goto_functions)
    forall_goto_program_instructions (pit, fit->second.body)
    {
      bool print_insn = false;

      switch (pit->type)
      {
      case GOTO:
      case ASSUME:
      case ASSERT:
      case ASSIGN:
        if (calc_globals_used(ns, pit->guard) > 0)
          print_insn = true;
        break;
      case FUNCTION_CALL:
      {
        const code_function_call2t &deref_code =
          to_code_function_call2t(pit->code);
        if (
          is_symbol2t(deref_code.function) &&
          to_symbol2t(deref_code.function).get_symbol_name() ==
            "c:@F@__ESBMC_yield")
          print_insn = true;
        break;
      }
      case NO_INSTRUCTION_TYPE:
      case OTHER:
      case SKIP:
      case LOCATION:
      case END_FUNCTION:
      case ATOMIC_BEGIN:
      case ATOMIC_END:
      case RETURN:
      case DECL:
      case DEAD:
      case THROW:
      case CATCH:
      case THROW_DECL:
      case THROW_DECL_END:
        break;
      }

      if (print_insn)
        pit->output_instruction(ns, pit->function, std::cout);
    }
}

// This prints the ESBMC version and a list of CMD options
// available in ESBMC.
void esbmc_parseoptionst::help()
{
  log_status("\n* * *           ESBMC {}          * * *", ESBMC_VERSION);
  std::ostringstream oss;
  oss << cmdline.cmdline_options;
  log_status("{}", oss.str());
}
