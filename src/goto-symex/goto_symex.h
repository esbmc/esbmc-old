#ifndef CPROVER_GOTO_SYMEX_GOTO_SYMEX_H
#define CPROVER_GOTO_SYMEX_GOTO_SYMEX_H

#include <goto-programs/goto_functions.h>
#include <goto-symex/goto_symex_state.h>
#include <goto-symex/symex_target.h>
#include <map>
#include <pointer-analysis/dereference.h>
#include <stack>
#include <util/i2string.h>
#include <irep2/irep2.h>
#include <util/options.h>
#include <util/std_types.h>

class reachability_treet; // Forward dec
class execution_statet;   // Forward dec

/**
 *  Primay symbolic execution class.
 *  Contains very little state data, instead implements a large number of
 *  methods that actually perform the symbolic execution and keep data
 *  elsewhere. Interprets actual GOTO functions. Fixes up assignments and
 *  formats operations into something suitable for being fed to the symex
 *  target object. Also maintains renaming hierarchies, call stacks, pointer
 *  tracking goo.
 */

class goto_symext
{
public:
  /**
   *  Default constructor. Performs base initialization, storing namespace
   *  and the symex target we'll be assigning at.
   *  @param _ns Namespace we'll be working in.
   *  @param _new_context Context we'll be working in.
   *  @param _target Symex target that actions will be recorded into.
   *  @param opts Options we'll be running with.
   */
  goto_symext(
    const namespacet &_ns,
    contextt &_new_context,
    const goto_functionst &goto_functions,
    std::shared_ptr<symex_targett> _target,
    optionst &opts);
  goto_symext(const goto_symext &sym);
  goto_symext &operator=(const goto_symext &sym);

  // Types

public:
  /** Records for dynamically allocated blobs of memory. */
  class allocated_obj
  {
  public:
    allocated_obj(
      const expr2tc &s,
      const guardt &g,
      const bool a,
      const std::string n)
      : obj(s), alloc_guard(g), auto_deallocd(a), name(n)
    {
    }
    /** Symbol identifying the pointer that was allocated. Must have ptr type */
    expr2tc obj;
    /** Guard when allocation occured. */
    guardt alloc_guard;
    /** Record if the object is automatically desallocated (allocated with alloca). */
    bool auto_deallocd;
    /** The object name */
    std::string name;
  };

  friend class symex_dereference_statet;
  friend class bmct;
  friend class reachability_treet;

  typedef goto_symex_statet statet;

  /**
   *  Class recording the outcome of symbolic execution.
   *  Contains the things that are of interest to the BMC class: The object
   *  containing the symex equation (or otherwise, the symex target), as well
   *  as the list of claims that have been recorded (and how many are already
   *  satisfied).
   */
  class symex_resultt
  {
  public:
    symex_resultt(
      std::shared_ptr<symex_targett> t,
      unsigned int claims,
      unsigned int remain)
      : target(std::move(t)), total_claims(claims), remaining_claims(remain){};

    std::shared_ptr<symex_targett> target;
    unsigned int total_claims;
    unsigned int remaining_claims;
  };

  // Macros
  //
  /**
   *  Return identifier for goto guards.
   *  These guards are symbolic names for the truth of a guard on a GOTO jump.
   *  Assertions and other activity during the course of symbolic execution
   *  encode these execution guard in them.
   *  @return Symbol expression naming the guard
   */
  expr2tc guard_identifier()
  {
    return symbol2tc(
      get_bool_type(),
      id2string(guard_identifier_s),
      symbol2t::level1,
      0,
      0,
      cur_state->top().level1.thread_id,
      0);
  };

  // Methods

  /**
   *  Create a symex result for this run.
   */
  goto_symext::symex_resultt get_symex_result();

  /**
   *  Symbolically execute one instruction.
   *  Essentially a despatcher. Performs some renaming and dereferencing, then
   *  hands off an expression to be claimed, or assigned, or possibly for some
   *  control flow beating to occur (goto, func call, return). Threading
   *  specific operations are handled by execution_statet, which overrides
   *  this.
   *  @param art Reachability tree we're working with.
   */
  virtual void symex_step(reachability_treet &art);

  /**
   *  Perform accounting checks / assertions at end of a program run.
   *  This should contain anything that must happen at the end of a program run,
   *  for example assertions about dynamic memory being freed.
   */
  void add_memory_leak_checks();

protected:
  /**
   *  Perform simplification on an expression.
   *  Essentially is just a call to simplify, but is guarded by the
   *  --no-simplify option being turned off.
   *  @param expr Expression to simplify, in place.
   */
  virtual void do_simplify(expr2tc &expr);

  /**
   *  Dereference an expression.
   *  Finds dereference expressions within expr, takes the set of things that
   *  it might point at, according to value set tracking, and builds an
   *  if-then-else list of concrete references that it might point at.
   *  @param expr Expression to eliminate dereferences from.
   *  @param mode The dereference mode.
   */
  void dereference(expr2tc &expr, dereferencet::modet mode);

  // symex

  /**
   *  Perform GOTO jump using current instruction.
   *  Handle a GOTO jump between locations. This isn't just the factor of there
   *  being jumps where the guards are nondeterministic, it's that we have to
   *  handle editing the unwind bound when these things occur, and set up state
   *  merges in the future to handle each path thats taken.
   *  @param old_guard Renamed guard on this jump occuring.
   */
  virtual void symex_goto(const expr2tc &old_guard);

  /**
   *  Perform interpretation of RETURN instruction.
   *  @param code return statement.
   */
  void symex_return(const expr2tc &code);

  /**
   *  Interpret an OTHER instruction.
   *  These can take many forms; memory management functions are OTHERs for
   *  example (ideally they should be intrinsics...), but also printf and
   *  variable declarations are handled here.
   */
  void symex_other(const expr2tc code);

  /**
   *  Interpret an DECL instruction.
   *  It creates renamed symbols. These names are all the names of local
   *  variables, renamed to level 1. We also bump up the level 1 renaming
   *  number, effectively making all the local variables new instances of those
   *  variables (which is what entering a function and declaring variables
   *  does).
   */
  void symex_decl(const expr2tc code);

  /**
   *  Interpret an DEAD instruction.
   *  It calls free on alloca'd symbols and erase the symbols from the
   *  propagation map.
   */
  void symex_dead(const expr2tc code);

  /**
   *  Interpret an ASSUME instruction.
   */
  void symex_assume();

  /**
   *  Interpret an ASSERT instruction.
   */
  void symex_assert();

  /**
   *  Perform incremental SMT solving for assert and assume statements.
   *  @param expr Expression that must be checked.
   *  @param msg Textual message explaining assertion.
   *  @return Return whether verification succeeded.
   */
  bool check_incremental(const expr2tc &expr, const std::string &msg);

  /**
   *  Perform an assertion.
   *  Encodes an assertion that the expression claimed is always true. This
   *  adds the requirement that the current state guard is true as well.
   *  @param expr Expression that must always be true.
   *  @param msg Textual message explaining assertion.
   */
  virtual void claim(const expr2tc &expr, const std::string &msg);

  /**
   *  Perform an assertion.
   *  Adds to target an assertion that must be checked.
   *  @param assertion Assertion that must be checked.
   *  @param msg Textual message explaining assertion.
   */
  virtual void assertion(const expr2tc &assertion, const std::string &msg);

  /**
   *  Perform an assumption.
   *  Adds to target an assumption that must always be true.
   *  @param assumption Assumption that must always be true.
   */
  virtual void assume(const expr2tc &assumption);

  // gotos
  /**
   *  Merge converging states into current state.
   *  Jumps forwards are handled by recording a merge of the current state in
   *  the future; then when we hit that future state, a phi function is
   *  performed that joins the states converging at this point, according to
   *  the truth of their guards.
   */
  void merge_gotos();

  /**
   *  Merge pointer tracking value sets in a phi function.
   *  See merge_gotos - when we're merging states together due to previous
   *  jumps, this function implements the merging of pointer tracking data.
   *  @param goto_state Previously executed goto state to be merged in.
   *  @param dest Thread state for previous jump to be merged into.
   */
  void merge_value_sets(const statet::goto_statet &goto_state);

  void merge_locality(const statet::goto_statet &goto_state);

  /**
   *  Join together a previous jump state into thread state.
   *  This combines together two thread states by using if-then-elses to decide
   *  the new value of a variable, according to the truth of the guards of the
   *  states being joined.
   *  @param goto_state The previous jumps state to be merged into the current
   */
  void phi_function(const statet::goto_statet &goto_state);

  /**
   *  Test whether unwinding bound has been exceeded.
   *  This looks up a look number, checks the limit on unwindings against the
   *  given number of unwinds already performed, and returns whether that limit
   *  has been exceeded.
   *  @param source Program location to check for loops against.
   *  @param unwind Number of unwinds that have already occured.
   *  @return True if we've unwound past the unwinding limit.
   */
  bool get_unwind(const symex_targett::sourcet &source, const BigInt &unwind);

  /**
   *  Encode unwinding assertions and assumption.
   *  If unwinding assertions are on, assert that the unwinding bound is not
   *  exceeded. If partial loops are off, assume that the unwinding bound was
   *  not exceeded. Otherwise, just continue execution.
   *  @param guard Current state guard.
   */
  void loop_bound_exceeded(const expr2tc &guard);

  // function calls

  /**
   *  Pop a stack frame.
   *  This frees/removes the top stack frame, and removes any relevant local
   *  variables from the l2 renaming, and value set tracking.
   */
  void pop_frame();

  /**
   *  Create assignment for return statement.
   *  Generate an assignment to the return variable from this return statement.
   *  @param assign Assignment expression. Output.
   *  @param code The return statement we're interpreting.
   *  @return True if a return assignment was generated.
   */
  bool make_return_assignment(expr2tc &assign, const expr2tc &code_return);

  /**
   *  Perform function call.
   *  Handles all kinds of function call instructions, symbols or function
   *  pointers.
   *  @param call Function call we're working on.
   */
  void symex_function_call(const expr2tc &call);

  /**
   *  End a functions interpretation.
   *  This routine pops a stack frame, and returns control to the caller;
   *  except in the case of function pointer interpretation, where we instead
   *  switch to interpreting the next pointed to function.
   */
  void symex_end_of_function();

  /**
   *  Handle an indirect function call, to a pointer.
   *  Finds all potential targets, and sets up calls to them with the
   *  appropriate guards and targets. They are then put in a list, the first
   *  one run, then at the end of each of these function calls we switch to
   *  the next in the list. Finally, when the insn after the func ptr call is
   *  run, all func ptr call states are merged in.
   *  @param call Function call to interpret.
   */
  virtual void symex_function_call_deref(const expr2tc &call);

  /**
   *  Handle function call to fixed function
   *  Like symex_function_call_code, but minus an assertion and location
   *  recording.
   *  @param code Function code to actually call
   */
  virtual void symex_function_call_code(const expr2tc &call);

  /**
   *  Discover whether recursion bound has been exceeded.
   *  @see get_unwind
   *  @param identifier Name of function to consider recursion of.
   *  @param unwind Number of times its been unwound already.
   *  @return True if unwind recursion has been exceeded.
   */
  bool get_unwind_recursion(const irep_idt &identifier, BigInt unwind);

  /**
   *  Join up function arguments.
   *  Assigns the value of arguments to a function to the actual argument
   *  variables of the function being called.
   *  @param function_type type containing argument types of func call.
   *  @param arguments The arguments to assign to function arg variables.
   *  @return the va_index for this function, if any, otherwise UINT_MAX
   */
  unsigned int argument_assignments(
    const irep_idt &function_identifier,
    const code_type2t &function_type,
    const std::vector<expr2tc> &arguments);

  /**
   *  Setup next function in a chain of func ptr calls.
   *  @see symex_function_call_deref
   *  @param first Whether this is the first func ptr invocation.
   *  @return True if a function pointer invocation was set up.
   */
  bool run_next_function_ptr_target(bool first);

  /**
   *  Run an intrinsic, something prefixed with __ESBMC.
   *  This looks through a set of intrinsic functions that are implemented in
   *  ESBMC, and calls the appropriate one. Examples include starting a thread,
   *  ending a thread, switching to another thread.
   *  @param call Function call being performed.
   *  @param art Reachability tree we're operating on.
   *  @param symname Name of intrinsic we're calling.
   */
  void run_intrinsic(
    const code_function_call2t &call,
    reachability_treet &art,
    const std::string &symname);

  /**
   *  Run a builtin, something prefixed with __builtin.
   *  This looks through a set of builtin functions that are implemented in
   *  ESBMC, and calls the appropriate one.
   *  @param call Function call being performed.
   *  @param symname Name of builtin we're calling.
   *  @return true if we handled the builtin
   */
  bool
  run_builtin(const code_function_call2t &call, const std::string &symname);

  /** Perform yield; forces a context switch point. */
  void intrinsic_yield(reachability_treet &arg);
  /** Perform switch_to; switches control to explicit thread ID. */
  void
  intrinsic_switch_to(const code_function_call2t &c, reachability_treet &art);
  /** Yield, always switching away from this thread */
  void intrinsic_switch_from(reachability_treet &arg);
  /** Perform get_thread_id; return the current thread identifier. */
  void intrinsic_get_thread_id(
    const code_function_call2t &call,
    reachability_treet &art);
  /** Perform set_thread_state; store thread startup information. */
  void intrinsic_set_thread_data(
    const code_function_call2t &call,
    reachability_treet &art);
  /** Perform get_thread_data; get thread startup information. */
  void intrinsic_get_thread_data(
    const code_function_call2t &call,
    reachability_treet &art);
  /** Perform spawn_thread; Generates a new thread at a named function. */
  void intrinsic_spawn_thread(
    const code_function_call2t &call,
    reachability_treet &art);
  /** Perform terminate_thread; Record thread as terminated. */
  void intrinsic_terminate_thread(reachability_treet &art);
  /** Perform get_thead_state... defunct. */
  void intrinsic_get_thread_state(
    const code_function_call2t &call,
    reachability_treet &art);
  /** Really atomic start/end - atomic blocks that just disable ileaves. */
  void intrinsic_really_atomic_begin(reachability_treet &art);
  /** Really atomic start/end - atomic blocks that just disable ileaves. */
  void intrinsic_really_atomic_end(reachability_treet &art);
  /** Context switch to the monitor thread. */
  void intrinsic_switch_to_monitor(reachability_treet &art);
  /** Context switch from the monitor thread. */
  void intrinsic_switch_from_monitor(reachability_treet &art);
  /** Register which thread is the monitor thread. */
  void intrinsic_register_monitor(
    const code_function_call2t &call,
    reachability_treet &art);
  /** Terminate the monitor thread */
  void intrinsic_kill_monitor(reachability_treet &art);
  /**
   * @brief Intrinsic call for C memset function call
   * 
   * This will either invoke our operational model (at string.c)
   * or try to compute the resulting value directly
   * 
   * @param art 
   * @param func_call memset function call
   */
  void intrinsic_memset(
    reachability_treet &art,
    const code_function_call2t &func_call);

  // Function to call a symname function, in case where were not able to optimize it
  void
  bump_call(const code_function_call2t &func_call, const std::string &symname);

  /** Returns the size of the object
   *
   * If the object is invalid, then this function will return 0
   */
  void intrinsic_get_object_size(
    const code_function_call2t &func_call,
    reachability_treet &art);

  /* Handles dereferencing between threads and is used only in data race checks. **/
  void replace_races_check(expr2tc &expr);

  /** Walk back up stack frame looking for exception handler. */
  bool symex_throw();

  /** Register exception handler on stack. */
  void symex_catch();

  /** Register throw handler on stack. */
  void symex_throw_decl();

  /** Update throw target. */
  void update_throw_target(
    goto_symex_statet::exceptiont *except [[maybe_unused]],
    goto_programt::const_targett target,
    const expr2tc &code,
    bool is_ellipsis = false);

  /** Check if we can rethrow an exception:
   *  if we can then update the target.
   *  if we can't then gives a error.
   */
  bool handle_rethrow(
    const expr2tc &operand,
    const goto_programt::instructiont &instruction);

  /** Check if we can throw an exception:
   *  if we can't then gives a error.
   */
  int handle_throw_decl(
    goto_symex_statet::exceptiont *frame,
    const irep_idt &id);

  /**
   * Call terminate function handler when needed.
   */
  bool terminate_handler();

  /**
   * Call unexpected function handler when needed.
   */
  bool unexpected_handler();

  /**
   *  Replace ireps regarding dynamic allocations with code.
   *  Things like "invalid-object" and suchlike are replaced here with
   *  references to array members, or more elaborate expressions, representing
   *  how that information is actually stored in the resulting SMT. In the past
   *  this has been done in the solver backend, but that seems slightly
   *  the wrong place.
   *  @param expr Expression we're replacing the contents of.
   */
  void replace_dynamic_allocation(expr2tc &expr);
  void default_replace_dynamic_allocation(expr2tc &expr);

  /**
   *  Decide if symbol is valid or not.
   *  i.e., whether it's live or not.
   *  @return True if symbol is valid.
   */
  bool is_valid_object(const symbolt &symbol);

  /**
   *  Make symbolic assignment.
   *  Renames things; records assignment in symex target, and all the relevant
   *  renaming and value set tracking objects. The primary task of this routine
   *  is to rewrite assignments to arrays, structs, and byte_selects into the
   *  equivalent uses of WITH, or byte_update, and so forth. The end result is
   *  a single new value to be bound to a new symbol.
   *  @param code Code to assign; with lhs and rhs.
   *  @param type Assignment type, visible by default
   *  @param kind The step kind, by default is plain BMC
   *  @param guard A guard for the assignment, true by default
   */
  virtual void symex_assign(
    const expr2tc &code,
    const bool hidden = false,
    const guardt &guard = guardt());

  /** Recursively perform symex assign. @see symex_assign */
  void symex_assign_rec(
    const expr2tc &lhs,
    const expr2tc &full_lhs,
    expr2tc &rhs,
    expr2tc full_rhs,
    guardt &guard,
    const bool hidden);

  /**
   *  Perform assignment to a symbol.
   *  Renames further, performs goto_symex_statet::assignment and symex target
   *  assignments.
   *  @param lhs Symbol to assign to
   *  @param full_lhs The original assignment symbol
   *  @param rhs Value to assign to symbol
   *  @param guard Guard; intent unknown
   */
  void symex_assign_symbol(
    const expr2tc &lhs,
    const expr2tc &full_lhs,
    expr2tc &rhs,
    expr2tc &full_rhs,
    guardt &guard,
    const bool hidden);

  /**
   *  Perform assignment to a structure.
   *  Performed when a constant structure appears on the left hand side.
   *  These kinds of assignments are permitted by C99, and some C++ fudge.
   *  Decomposes structure into each particular field, and encodes an assignment
   *  for each pair of fields.
   *
   *  (It's not intuitive that one may assign to a /constant/ structure, however
   *  a number of pieces of code need to be able to create structures out of
   *  thin air, or more often an array of bytes. In lieu of better distinction
   *  between a struct literal and a group of values arranged as a structure,
   *  the constant_struct irep is used).
   *
   *  @param lhs Symbol to assign to
   *  @param full_lhs The original assignment symbol
   *  @param rhs Value to assign to symbol
   *  @param guard Guard; intent unknown
   */
  void symex_assign_structure(
    const expr2tc &lhs,
    const expr2tc &full_lhs,
    expr2tc &rhs,
    expr2tc &full_rhs,
    guardt &guard,
    const bool hidden);

  /**
   *  Perform assignment to an extract irep.
   *
   *  Currently these extract assignments can crop up when we're assigning into
   *  a bitfield. We can't rewrite the assignment in the front end, because
   *  there isn't enough context there to know whether an expression is on the
   *  lhs or rhs of an expression. Thus we have to take the operand of the
   *  extract irep, and read/modify/write it.
   *
   *  @param lhs Extract irep to assign into
   *  @param rhs Value to assign to bitfield
   *  @param guard Guard of the current assignment
   */
  void symex_assign_extract(
    const expr2tc &lhs,
    const expr2tc &full_lhs,
    expr2tc &rhs,
    expr2tc &full_rhs,
    guardt &guard,
    const bool hidden);

  /**
   *  Perform assignment to a typecast irep.
   *  This just ends up moving the typecast from the lhs to the rhs.
   *  @param lhs Typecast to assign to
   *  @param full_lhs The original assignment symbol
   *  @param rhs Value to assign to lhs
   *  @param guard Guard; intent unknown
   */
  void symex_assign_typecast(
    const expr2tc &lhs,
    const expr2tc &full_lhs,
    expr2tc &rhs,
    expr2tc &full_rhs,
    guardt &guard,
    const bool hidden);

  /**
   *  Perform assignment to an array.
   *  lhs transformed to the container of the array, or the symbol for its
   *  destination. rhs converted to a WITH statement, updating the contents of
   *  the original array with the value of the original rhs.
   *  @param lhs Array to assign to
   *  @param full_lhs The original assignment symbol
   *  @param rhs Value to assign to symbol
   *  @param guard Guard; intent unknown
   */
  void symex_assign_array(
    const expr2tc &lhs,
    const expr2tc &full_lhs,
    expr2tc &rhs,
    expr2tc &full_rhs,
    guardt &guard,
    const bool hidden);

  /**
   *  Perform assignment to a struct.
   *  Exactly like with arrays, but with structs and members.
   *  @see symex_assign_array
   *  @param lhs Struct to assign to
   *  @param full_lhs The original assignment symbol
   *  @param rhs Value to assign to lhs
   *  @param guard Guard; intent unknown
   */
  void symex_assign_member(
    const expr2tc &lhs,
    const expr2tc &full_lhs,
    expr2tc &rhs,
    expr2tc &full_rhs,
    guardt &guard,
    const bool hidden);

  /**
   *  Perform assignment to an "if".
   *  This ends up being two assignments, one to one branch of the if, the
   *  other to the other. The appropriate guard is executed in either case.
   *  @param lhs "If" to assign to
   *  @param full_lhs The original assignment symbol
   *  @param rhs Value to assign to lhs
   *  @param guard Guard; intent unknown
   */
  void symex_assign_if(
    const expr2tc &lhs,
    const expr2tc &full_lhs,
    expr2tc &rhs,
    expr2tc &full_rhs,
    guardt &guard,
    const bool hidden);

  /**
   *  Perform assignment to a byte extract.
   *  Results in a byte update of the relevant part of the lhs with the
   *  right hand side at the appropriate position. Currently a problem , as
   *  assignments of something that's bigger than a byte fails.
   *  @param lhs Byte extract to assign to
   *  @param full_lhs The original assignment symbol
   *  @param rhs Value to assign to lhs
   *  @param guard Guard; intent unknown
   */
  void symex_assign_byte_extract(
    const expr2tc &lhs,
    const expr2tc &full_lhs,
    expr2tc &rhs,
    expr2tc &full_rhs,
    guardt &guard,
    const bool hidden);

  /**
   *  Assign through a 'concat' operation. These are generated when we fail to
   *  dereference something correctly, and generate a series of byte operations
   *  that we then stitch back together. When that's on the left hand side of an
   *  expression, this means that we have to decompose the right hand side into
   *  a series of byte assignments.
   *  @param lhs Concat to assign to
   *  @param full_lhs The original assignment symbol
   *  @param rhs Value to assign to lhs
   *  @param guard Assignment guard.
   */
  void symex_assign_concat(
    const expr2tc &lhs,
    const expr2tc &full_lhs,
    expr2tc &rhs,
    expr2tc &full_rhs,
    guardt &guard,
    const bool hidden);

  /**
   *  This method is used when we need to assign a value
   *  to a struct bitfield which is obtained via
   *  applying a bit-mask and bit-shifting. Such "masking"
   *  is typically applied to 'index', 'byte_extract' or 'concat'.
   *  @param lhs Bitfield to assign to
   *  @param full_lhs The original assignment symbol
   *  @param rhs Value to assign to lhs
   *  @param guard Assignment guard.
   */
  void symex_assign_bitfield(
    const expr2tc &lhs,
    const expr2tc &full_lhs,
    expr2tc &rhs,
    expr2tc &full_rhs,
    guardt &guard,
    const bool hidden);

  /** Symbolic implementation of malloc. */
  expr2tc symex_malloc(const expr2tc &lhs, const sideeffect2t &code);
  /** Implementation of realloc. */
  void symex_realloc(const expr2tc &lhs, const sideeffect2t &code);
  /** Symbolic implementation of alloca. */
  expr2tc symex_alloca(const expr2tc &lhs, const sideeffect2t &code);
  /** Wrapper around for alloca and malloc. */
  expr2tc
  symex_mem(const bool is_malloc, const expr2tc &lhs, const sideeffect2t &code);
  /** Pointer modelling update function */
  void track_new_pointer(
    const expr2tc &ptr_obj,
    const type2tc &new_type,
    const expr2tc &size = expr2tc());
  /** Symbolic implementation of free */
  void symex_free(const expr2tc &expr);
  /** Symbolic implementation of c++'s delete. */
  void symex_cpp_delete(const expr2tc &code);
  /** Symbolic implementation of c++'s new. */
  void symex_cpp_new(const expr2tc &lhs, const sideeffect2t &code);
  /** Symbolic implementation of printf */
  void symex_printf(const expr2tc &lhs, expr2tc &code);
  /** Symbolic implementation of scanf and fscanf */
  void symex_input(const code_function_call2t &expr);
  /** Symbolic implementation of va_arg */
  void symex_va_arg(const expr2tc &lhs, const sideeffect2t &code);

  /**
   *  Replace nondet func calls with nondeterminism.
   *  Creates a new nondeterministic symbol, with a globally unique counter
   *  encoded into its name. Is left as a free variable.
   *  @param expr Expr to search for nondet symbols.
   */
  void replace_nondet(expr2tc &expr);

  /**
   *  Fetch reference to global dynamic object counter.
   *  @return Reference to global dynamic object counter.
   */
  virtual unsigned int &get_dynamic_counter() = 0;
  /**
   *  Fetch reference to global nondet object counter.
   *  @return Reference to global nondet object counter.
   */
  virtual unsigned int &get_nondet_counter() = 0;
  /**
   *  Incrementally check the assumption.
   */
  bool is_assume_false(const expr2tc &assumption);

  // Members

  /** Options we're working with */
  optionst &options;
  /**
   *  Symbol prefix for guards.
   *  These guards are the symbolic names for the truth of whether a particular
   *  branch has been taken during symbolic execution.
   *  @see guard_identifier
   */
  irep_idt guard_identifier_s;
  /** Loop numbers. */
  unsigned first_loop;
  /** Number of assertions executed. */
  unsigned total_claims;
  /** Number of assertions remaining to be discharged. */
  unsigned remaining_claims;
  /** Reachability tree we're working with. */
  reachability_treet *art1;
  /** Unwind bounds, loop number -> max unwinds. */
  std::map<unsigned, BigInt> unwind_set;
  /** Global maximum number of unwinds. */
  BigInt max_unwind;
  /** Whether constant propagation is to be enabled. */
  bool constant_propagation;
  /** Namespace we're working in. */
  const namespacet &ns;
  /** Context we're working with */
  contextt &new_context;
  /** GOTO functions that we're operating over. */
  const goto_functionst &goto_functions;
  /** Target listening to the execution trace */
  std::shared_ptr<symex_targett> target;
  /** Target thread we're currently operating upon */
  goto_symex_statet *cur_state;
  /** Symbol names for modelling arrays.
   *  These irep_idts contain the names of the arrays being used to store data
   *  modelling what pointers are active, which are freed, and so forth. They
   *  can change between C and C++, unfortunately. */
  irep_idt valid_ptr_arr_name, alloc_size_arr_name, dyn_info_arr_name;
  /** List of all allocated objects.
   *  Used to track what we should level memory-leak-assertions against when the
   *  program execution has finished */
  std::list<allocated_obj> dynamic_memory;

  /* Exception Handling.
   * This will stack the try-catch blocks, so we always know which catch
   * we should jump.
   */
  typedef std::stack<goto_symex_statet::exceptiont> stack_catcht;

  /** Stack of try-catch blocks. */
  stack_catcht stack_catch;

  /** Pointer to last thrown exception. */
  goto_programt::instructiont *last_throw;

  /** Map of currently active exception targets, i.e. instructions where an
   *  exception is going to be merged in in the future. Keys are iterators to
   *  the instruction catching the object; values are the symbols that the
   *  thrown piece of data has been assigned to. */
  std::map<goto_programt::const_targett, expr2tc> thrown_obj_map;

  /** Flag to indicate if we are go into the unexpected flow. */
  bool inside_unexpected;

  /** Disable return value optimization */
  bool no_return_value_opt;
  /** Limit size for stack */
  unsigned long stack_limit;
  /** Depth limit, as given by the --depth option */
  unsigned long depth_limit;
  /** Instruction number we are to break at -- that is, trap, to the debugger.
   *  Zero means no trap; there is a zero instruction, but there are better
   *  ways of trapping at the start of symbolic execution to get at that. */
  unsigned long break_insn;
  /** Flag as to whether we're performing memory leak checks. Corresponds to
   *  the option --memory-leak-check */
  bool memory_leak_check;
  /** Flag as to whether we're pruning the objects from the memory leak check
   *  that are still reachable via global pointers. Corresponds to the option
   *  --no-reachable-memory-leak */
  bool no_reachable_memleak;
  /** Flag as to whether we're checking user assertions. Corresponds to
   *  the option --no-assertions */
  bool no_assertions;
  /** Flag as to whether we're not simplifying exprs. Corresponds to
   *  the option --no-simplify */
  bool no_simplify;
  /** Flag as to whether we're inserting unwinding assertions. Corresponds to
   *  the option --no-unwinding-assertions */
  bool no_unwinding_assertions;
  /** Flag as to whether we're not enabling partial loops. Corresponds to
   *  the option --partial-loops */
  bool partial_loops;
  /** Flag as to whether we're doing a k-induction. Corresponds to
   *  the options --k-induction and --k-induction-parallel */
  bool k_induction;
  /** Flag as to whether we're doing a k-induction base case. Corresponds to
   *  the option --base-case */
  bool base_case;
  /** Flag as to whether we're doing a k-induction forward condition.
   *  Corresponds to the option --forward-condition */
  bool forward_condition;
  /** Flag as to whether we're doing a k-induction inductive step.
   *  Corresponds to the option --inductive-step */
  bool inductive_step;
  /** Set of dereference state records; this field is used as a mailbox between
   *  the dereference code and the caller, who will inspect the contents after
   *  a call to dereference (in INTERNAL mode) completes. */
  std::list<dereference_callbackt::internal_item> internal_deref_items;

  friend void build_goto_symex_classes();
};

class symex_dereference_statet : public dereference_callbackt
{
public:
  symex_dereference_statet(
    goto_symext &_goto_symex,
    goto_symext::statet &_state)
    : goto_symex(_goto_symex), state(_state)
  {
  }

protected:
  goto_symext &goto_symex;
  goto_symext::statet &state;

  void dereference_failure(
    const std::string &property,
    const std::string &msg,
    const guardt &guard) override;

  void dereference_assume(const guardt &guard) override;

  void
  get_value_set(const expr2tc &expr, value_setst::valuest &value_set) override;

  bool has_failed_symbol(const expr2tc &expr, const symbolt *&symbol) override;

  void rename(expr2tc &expr) override;

  expr2tc constant_propagation(expr2tc &expr);

  void
  dump_internal_state(const std::list<struct internal_item> &data) override;
  bool is_live_variable(const expr2tc &sym) override;
};

#endif
