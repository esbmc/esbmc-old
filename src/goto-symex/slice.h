#ifndef CPROVER_GOTO_SYMEX_SLICE_H
#define CPROVER_GOTO_SYMEX_SLICE_H

#include <goto-symex/symex_target_equation.h>
#include <util/time_stopping.h>
#include <util/algorithms.h>
#include <util/options.h>
#include <boost/range/adaptor/reversed.hpp>
#include <langapi/language_util.h>

/* Base interface */
class slicer : public ssa_step_algorithm
{
public:
  slicer() : ssa_step_algorithm(true)
  {
  }
  BigInt ignored() const override
  {
    return sliced;
  }

protected:
  /// tracks how many steps were sliced
  BigInt sliced = 0;
};

/**
 * Naive slicer: slice every step after the last assertion
 * @param eq symex formula to be sliced
 * @return number of steps that were ignored
 */
class simple_slice : public slicer
{
public:
  simple_slice() = default;
  bool run(symex_target_equationt::SSA_stepst &) override;
};

/**
 * Claim slicer: remove every other claim of the formula
 * with the exception of the one to keep.
 */
class claim_slicer : public slicer
{
public:
  explicit claim_slicer(
    const size_t claim_to_keep,
    bool show_slice_info,
    bool is_goto_cov,
    namespacet &ns)
    : claim_to_keep(claim_to_keep),
      show_slice_info(show_slice_info),
      is_goto_cov(is_goto_cov),
      ns(ns)
  {
    if (!claim_to_keep)
    {
      log_error("All the claims start from 1 (use --show-claims)");
      abort();
    }
  };
  bool run(symex_target_equationt::SSA_stepst &) override;
  size_t claim_to_keep;
  std::string claim_msg;
  std::string claim_loc;
  bool show_slice_info;
  bool is_goto_cov;
  namespacet ns;
};

/**
 * @brief Class for the symex-slicer, this slicer is to be executed
 * on SSA formula in order to remove every symbol that does not depends
 * on it
 *
 * It works by constructing a symbol dependency list by transversing
 * the SSA formula in reverse order. If any assume, assignment, or renumber
 * step does not belong into this dependency, then it will be ignored.
 */
class symex_slicet : public slicer
{
public:
  explicit symex_slicet(const optionst &options)
    : slice_assumes(options.get_bool_option("slice-assumes")),
      slice_nondet(!options.get_bool_option("generate-testcase"))
  {
  }

  /**
   * Iterate over all steps of the \eq in REVERSE order,
   * getting symbol dependencies. If an
   * assignment, renumber or assume does not contain one
   * of the dependency symbols, then it will be ignored.
   *
   * @param eq symex formula to be sliced
   */
  bool run(symex_target_equationt::SSA_stepst &eq) override
  {
    sliced = 0;
    fine_timet algorithm_start = current_time();
    for (auto &step : boost::adaptors::reverse(eq))
    {
      if (step.ignore)
        continue;
      run_on_step(step);
    }
    fine_timet algorithm_stop = current_time();
    log_status(
      "Slicing time: {}s (removed {} assignments)",
      time2string(algorithm_stop - algorithm_start),
      sliced);
    return true;
  }

  /**
   * Holds the symbols the current equation depends on.
   */
  std::unordered_set<std::string> depends;

  static expr2tc get_nondet_symbol(const expr2tc &expr);

  /**
   * Marks SSA_steps to be ignored which have no effects on the target equation,
   * according to the options set in the `config`.
   *
   * Notably, this function depends on the global `config`:
   *  - "no-slice" in `options` -> perform only simple slicing: ignore everything
   *    after the final assertion
   *  - "slice-assumes" in `options` -> also perform slicing of assumption steps
   *  - `config.no_slice_names` and `config.no_slice_ids` -> suppress slicing of
   *    particular symbols in non-simple slicing mode.
   *
   *    * Note 1: ASSERTS are not sliced, only their symbols are added
   * into the #depends
   *
   *    * Note 2: Similar to ASSERTS, if 'slice-assumes' option is
   * is not enabled. Then only its symbols are added into the
   * #depends

   *
   * @param eq The target equation containing the SSA steps to perform program
   *           slicing on.
   * @return Whether any step was sliced away
   */

protected:
  /// whether assumes should be sliced
  const bool slice_assumes;
  /// Whether we should slice nondet symbols
  const bool slice_nondet;

  /**
   * Recursively explores the operands of an expression \expr
   * If a symbol is found, then it is added into the #depends
   * member if `Add` is true, otherwise returns true.
   *
   * @param expr expression to extract every symbol
   * @return true if at least one symbol was found
   */
  template <bool Add>
  bool get_symbols(const expr2tc &expr);

  /**
   * Remove unneeded assumes from the formula
   *
   * Check if the Assume cond symbol is in the #depends, if
   * it is not then mark the \SSA_Step as ignored.
   *
   * If the assume cond is in the #depends, then add its guards
   * and cond into the #depends
   *
   * Note 1: All the conditions operands are going to be added
   * into the #depends. This makes that the condition itself as
   * a "reverse taint"
   *
   * TODO: What happens if the ASSUME would result in false?
   *
   * @param SSA_step an assume step
   */
  void run_on_assert(symex_target_equationt::SSA_stept &SSA_step) override;

  /**
   * Remove unneeded assumes from the formula
   *
   * Check if the Assume cond symbol is in the #depends, if
   * it is not then mark the \SSA_Step as ignored.
   *
   * If the assume cond is in the #depends, then add its guards
   * and cond into the #depends
   *
   * Note 1: All the conditions operands are going to be added
   * into the #depends. This makes that the condition itself as
   * a "reverse taint"
   *
   * TODO: What happens if the ASSUME would result in false?
   *
   * @param SSA_step an assume step
   */
  void run_on_assume(symex_target_equationt::SSA_stept &SSA_step) override;

  /**
   * Remove unneeded assignments from the formula
   *
   * Check if the LHS symbol is in the #depends, if
   * it is not then mark the \SSA_Step as ignored.
   *
   * If the assume cond is in the #depends, then add its guards
   * and cond into the #depends
   *
   * @param SSA_step an assignment step
   */
  void run_on_assignment(symex_target_equationt::SSA_stept &SSA_step) override;

  /**
   * Remove unneeded renumbers from the formula
   *
   * Check if the LHS symbol is in the #depends, if
   * it is not then mark the \SSA_Step as ignored.
   *
   * If the assume cond is in the #depends, then add its guards
   * and cond into the #depends
   *
   * @param SSA_step an renumber step
   */
  void run_on_renumber(symex_target_equationt::SSA_stept &SSA_step) override;
};

#endif
