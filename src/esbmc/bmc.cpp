#include <csignal>
#include <memory>
#include <sys/types.h>
#include <algorithm>
#include <thread>
#include <chrono>

#ifndef _WIN32
#  include <unistd.h>
#  include <sched.h>
#else
#  include <windows.h>
#  include <winbase.h>
#  undef ERROR
#  undef small
#endif

#include <fmt/format.h>
#include <ac_config.h>
#include <esbmc/bmc.h>
#include <esbmc/document_subgoals.h>
#include <fstream>
#include <goto-programs/goto_loops.h>
#include <goto-symex/build_goto_trace.h>
#include <goto-symex/goto_trace.h>
#include <goto-symex/reachability_tree.h>
#include <goto-symex/slice.h>
#include <goto-symex/features.h>
#include <goto-symex/xml_goto_trace.h>
#include <langapi/language_util.h>
#include <langapi/languages.h>
#include <langapi/mode.h>
#include <sstream>
#include <util/i2string.h>
#include <irep2/irep2.h>
#include <util/location.h>

#include <util/migrate.h>
#include <util/show_symbol_table.h>
#include <util/time_stopping.h>
#include <util/cache.h>
#include <atomic>
#include <goto-symex/witnesses.h>

bmct::bmct(goto_functionst &funcs, optionst &opts, contextt &_context)
  : options(opts), context(_context), ns(context)
{
  interleaving_number = 0;
  interleaving_failed = 0;

  ltl_results_seen[ltl_res_bad] = 0;
  ltl_results_seen[ltl_res_failing] = 0;
  ltl_results_seen[ltl_res_succeeding] = 0;
  ltl_results_seen[ltl_res_good] = 0;

  // The next block will initialize the algorithms used for the analysis.
  {
    if (opts.get_bool_option("no-slice"))
      algorithms.emplace_back(std::make_unique<simple_slice>());
    else
      algorithms.emplace_back(std::make_unique<symex_slicet>(options));

    // Run cache if user has specified the option
    if (options.get_bool_option("cache-asserts"))
      // Store the set between runs
      algorithms.emplace_back(std::make_unique<assertion_cache>(
        config.ssa_caching_db, !options.get_bool_option("forward-condition")));

    if (opts.get_bool_option("ssa-features-dump"))
      algorithms.emplace_back(std::make_unique<ssa_features>());
  }

  if (options.get_bool_option("smt-during-symex"))
  {
    runtime_solver = std::unique_ptr<smt_convt>(create_solver("", ns, options));

    symex = std::make_unique<reachability_treet>(
      funcs,
      ns,
      options,
      std::make_shared<runtime_encoded_equationt>(ns, *runtime_solver),
      _context);
  }
  else
  {
    symex = std::make_unique<reachability_treet>(
      funcs,
      ns,
      options,
      std::make_shared<symex_target_equationt>(ns),
      _context);
  }
}

void bmct::successful_trace()
{
  if (options.get_bool_option("result-only"))
    return;

  std::string witness_output = options.get_option("witness-output");
  if (witness_output != "")
  {
    goto_tracet goto_trace;
    log_progress("Building successful trace");
    /* build_successful_goto_trace(eq, ns, goto_trace); */
    correctness_graphml_goto_trace(options, ns, goto_trace);
  }
}

void bmct::error_trace(smt_convt &smt_conv, const symex_target_equationt &eq)
{
  if (options.get_bool_option("result-only"))
    return;

  log_progress("Building error trace");

  bool is_compact_trace = true;
  if (
    options.get_bool_option("no-slice") &&
    !options.get_bool_option("compact-trace"))
    is_compact_trace = false;

  goto_tracet goto_trace;
  build_goto_trace(eq, smt_conv, goto_trace, is_compact_trace);

  std::string output_file = options.get_option("cex-output");
  if (output_file != "")
  {
    std::ofstream out(output_file);
    show_goto_trace(out, ns, goto_trace);
  }

  std::string witness_output = options.get_option("witness-output");
  if (witness_output != "")
    violation_graphml_goto_trace(options, ns, goto_trace);

  if (options.get_bool_option("generate-testcase"))
  {
    generate_testcase_metadata();
    generate_testcase("testcase.xml", eq, smt_conv);
  }

  std::ostringstream oss;
  log_fail("\n[Counterexample]\n");
  show_goto_trace(oss, ns, goto_trace);
  log_result("{}", oss.str());
}

void bmct::generate_smt_from_equation(
  smt_convt &smt_conv,
  symex_target_equationt &eq) const
{
  std::string logic;

  if (!options.get_bool_option("int-encoding"))
  {
    logic = "bit-vector";
    logic += (!config.ansi_c.use_fixed_for_float) ? "/floating-point " : " ";
    logic += "arithmetic";
  }
  else
    logic = "integer/real arithmetic";

  log_status("Encoding remaining VCC(s) using {}", logic);

  fine_timet encode_start = current_time();
  eq.convert(smt_conv);
  fine_timet encode_stop = current_time();
  log_status(
    "Encoding to solver time: {}s", time2string(encode_stop - encode_start));
}

smt_convt::resultt bmct::run_decision_procedure(
  smt_convt &smt_conv,
  symex_target_equationt &eq) const
{
  generate_smt_from_equation(smt_conv, eq);

  if (
    options.get_bool_option("smt-formula-too") ||
    options.get_bool_option("smt-formula-only"))
  {
    smt_conv.dump_smt();
    if (options.get_bool_option("smt-formula-only"))
      return smt_convt::P_SMTLIB;
  }

  log_progress("Solving with solver {}", smt_conv.solver_text());

  fine_timet sat_start = current_time();
  smt_convt::resultt dec_result = smt_conv.dec_solve();
  fine_timet sat_stop = current_time();

  // output runtime
  log_status(
    "Runtime decision procedure: {}s", time2string(sat_stop - sat_start));

  return dec_result;
}

void bmct::report_success()
{
  log_success("\nVERIFICATION SUCCESSFUL");
}

void bmct::report_failure()
{
  log_fail("\nVERIFICATION FAILED");
}

void bmct::show_program(const symex_target_equationt &eq)
{
  unsigned int count = 1;
  std::ostringstream oss;
  if (config.options.get_bool_option("ssa-symbol-table"))
    ::show_symbol_table_plain(ns, oss);

  languagest languages(ns, language_idt::C);

  oss << "\nProgram constraints: \n";

  bool sliced = config.options.get_bool_option("ssa-sliced");

  for (auto const &it : eq.SSA_steps)
  {
    if (!(it.is_assert() || it.is_assignment() || it.is_assume()))
      continue;

    if (it.ignore && !sliced)
      continue;

    oss << "// " << it.source.pc->location_number << " ";
    oss << it.source.pc->location.as_string();
    if (!it.comment.empty())
      oss << " (" << it.comment << ")";
    oss << "\n/* " << count << " */ ";

    std::string string_value;
    languages.from_expr(migrate_expr_back(it.cond), string_value);

    if (it.is_assignment())
    {
      oss << string_value << "\n";
    }
    else if (it.is_assert())
    {
      oss << "(assert)" << string_value << "\n";
    }
    else if (it.is_assume())
    {
      oss << "(assume)" << string_value << "\n";
    }
    else if (it.is_renumber())
    {
      oss << "renumber: " << from_expr(ns, "", it.lhs) << "\n";
    }

    if (!migrate_expr_back(it.guard).is_true())
    {
      languages.from_expr(migrate_expr_back(it.guard), string_value);
      oss << std::string(i2string(count).size() + 3, ' ');
      oss << "guard: " << string_value << "\n";
    }

    oss << '\n';
    count++;
  }
  log_status("{}", oss.str());
}

void bmct::report_trace(
  smt_convt::resultt &res,
  const symex_target_equationt &eq)
{
  bool bs = options.get_bool_option("base-case");
  bool fc = options.get_bool_option("forward-condition");
  bool is = options.get_bool_option("inductive-step");
  bool term = options.get_bool_option("termination");
  bool show_cex = options.get_bool_option("show-cex");

  switch (res)
  {
  case smt_convt::P_UNSATISFIABLE:
    if (is && term)
    {
    }
    else if (!bs)
    {
      successful_trace();
    }
    break;

  case smt_convt::P_SATISFIABLE:
    if (!bs && show_cex)
    {
      error_trace(*runtime_solver, eq);
    }
    else if (!is && !fc)
    {
      error_trace(*runtime_solver, eq);
    }
    break;

  default:
    break;
  }
}

void bmct::report_multi_property_trace(
  smt_convt::resultt &res,
  const std::string &msg)
{
  assert(options.get_bool_option("base-case"));

  switch (res)
  {
  case smt_convt::P_UNSATISFIABLE:
    // UNSAT means that the property was correct up to K
    log_success("Claim '{}' holds up to the current K", msg);
    break;

  case smt_convt::P_SATISFIABLE:
    // SAT means that we found a error!
    log_fail("Claim '{}' fails", msg);
    break;

  default:
    log_fail("Claim '{}' could not be solved", msg);
    break;
  }
}

void bmct::report_result(smt_convt::resultt &res)
{
  // k-induction prints its own messages
  if (options.get_bool_option("k-induction-parallel"))
    return;

  bool bs = options.get_bool_option("base-case");
  bool fc = options.get_bool_option("forward-condition");
  bool is = options.get_bool_option("inductive-step");
  bool term = options.get_bool_option("termination");
  bool mul = options.get_bool_option("multi-property");

  switch (res)
  {
  case smt_convt::P_UNSATISFIABLE:
    if (is && term)
    {
      report_failure();
    }
    else if (!bs || mul)
    {
      report_success();
    }
    else
    {
      log_status("No bug has been found in the base case");
    }
    break;

  case smt_convt::P_SATISFIABLE:
    if (!is && !fc)
    {
      report_failure();
    }
    else if (fc)
    {
      log_status("The forward condition is unable to prove the property");
    }
    else if (is)
    {
      log_status("The inductive step is unable to prove the property");
    }
    break;

  // Return failure if we didn't actually check anything, we just emitted the
  // test information to an SMTLIB formatted file. Causes esbmc to quit
  // immediately (with no error reported)
  case smt_convt::P_SMTLIB:
    return;

  default:
    log_error("SMT solver failed");
    break;
  }

  if ((interleaving_number > 0) && options.get_bool_option("all-runs"))
  {
    log_status("Number of generated interleavings: {}", interleaving_number);
    log_status("Number of failed interleavings: {}", interleaving_failed);
  }
}

smt_convt::resultt bmct::start_bmc()
{
  std::shared_ptr<symex_target_equationt> eq;
  smt_convt::resultt res = run(eq);
  report_trace(res, *eq);
  report_result(res);
  return res;
}

smt_convt::resultt bmct::run(std::shared_ptr<symex_target_equationt> &eq)
{
  symex->options.set_option("unwind", options.get_option("unwind"));
  symex->setup_for_new_explore();

  if (options.get_bool_option("schedule"))
    return run_thread(eq);

  smt_convt::resultt res;
  do
  {
    if (++interleaving_number > 1)
      log_status("Thread interleavings {}", interleaving_number);

    fine_timet bmc_start = current_time();
    res = run_thread(eq);

    if (res == smt_convt::P_SATISFIABLE)
    {
      if (config.options.get_bool_option("smt-model"))
        runtime_solver->print_model();

      if (config.options.get_bool_option("bidirectional"))
        bidirectional_search(*runtime_solver, *eq);
    }

    if (res)
    {
      if (res == smt_convt::P_SATISFIABLE)
        ++interleaving_failed;

      if (!options.get_bool_option("all-runs"))
        return res;
    }
    fine_timet bmc_stop = current_time();

    log_status("BMC program time: {}s", time2string(bmc_stop - bmc_start));

    // Only run for one run
    if (options.get_bool_option("interactive-ileaves"))
      return res;

  } while (symex->setup_next_formula());

  if (options.get_bool_option("ltl"))
  {
    // So, what was the lowest value ltl outcome that we saw?
    if (ltl_results_seen[ltl_res_bad])
      log_result("Final lowest outcome: LTL_BAD");
    else if (ltl_results_seen[ltl_res_failing])
      log_result("Final lowest outcome: LTL_FAILING");
    else if (ltl_results_seen[ltl_res_succeeding])
      log_result("Final lowest outcome: LTL_SUCCEEDING");
    else if (ltl_results_seen[ltl_res_good])
      log_result("Final lowest outcome: LTL_GOOD");
    else
      log_warning("No LTL traces seen, apparently");
  }

  return interleaving_failed > 0 ? smt_convt::P_SATISFIABLE : res;
}

void bmct::bidirectional_search(
  smt_convt &smt_conv,
  const symex_target_equationt &eq)
{
  // We should only analyse the inductive step's cex and we're running
  // in k-induction mode
  if (!(options.get_bool_option("inductive-step") &&
        options.get_bool_option("k-induction")))
    return;

  // We'll walk list of SSA steps and look for inductive assignments
  std::vector<stack_framet> frames;
  unsigned assert_loop_number = 0;
  for (const auto &ssait : eq.SSA_steps)
  {
    if (ssait.is_assert() && smt_conv.l_get(ssait.cond_ast).is_false())
    {
      if (!ssait.loop_number)
        return;

      // Save the location of the failed assertion
      frames = ssait.stack_trace;
      assert_loop_number = ssait.loop_number;

      // We are not interested in instructions before the failed assertion yet
      break;
    }
  }

  for (auto f : frames)
  {
    // Look for the function
    goto_functionst::function_mapt::iterator fit =
      symex->goto_functions.function_map.find(f.function);
    assert(fit != symex->goto_functions.function_map.end());

    // Find function loops
    goto_loopst loops(f.function, symex->goto_functions, fit->second);

    if (!loops.get_loops().size())
      continue;

    auto lit = loops.get_loops().begin(), lie = loops.get_loops().end();
    while (lit != lie)
    {
      auto loop_head = lit->get_original_loop_head();

      // Skip constraints from other loops
      if (loop_head->loop_number == assert_loop_number)
        break;

      ++lit;
    }

    if (lit == lie)
      continue;

    // Get the loop vars
    auto all_loop_vars = lit->get_modified_loop_vars();
    all_loop_vars.insert(
      lit->get_unmodified_loop_vars().begin(),
      lit->get_unmodified_loop_vars().end());

    // Now, walk the SSA and get the last value of each variable before the loop
    std::unordered_map<irep_idt, std::pair<expr2tc, expr2tc>, irep_id_hash>
      var_ssa_list;

    for (const auto &ssait : eq.SSA_steps)
    {
      if (ssait.loop_number == lit->get_original_loop_head()->loop_number)
        break;

      if (ssait.ignore)
        continue;

      if (!ssait.is_assignment())
        continue;

      expr2tc new_lhs = ssait.original_lhs;
      renaming::renaming_levelt::get_original_name(new_lhs, symbol2t::level0);

      if (all_loop_vars.find(new_lhs) == all_loop_vars.end())
        continue;

      var_ssa_list[to_symbol2t(new_lhs).thename] = {
        ssait.original_lhs, ssait.rhs};
    }

    if (!var_ssa_list.size())
      return;

    // Query the solver for the value of each variable
    std::vector<expr2tc> equalities;
    for (auto it : var_ssa_list)
    {
      // We don't support arrays or pointers
      if (is_array_type(it.second.first) || is_pointer_type(it.second.first))
        return;

      auto lhs = build_lhs(smt_conv, it.second.first);
      auto value = build_rhs(smt_conv, it.second.second);

      // Add lhs and rhs to the list of new constraints
      equalities.push_back(equality2tc(lhs, value));
    }

    // Build new assertion
    expr2tc constraints = equalities[0];
    for (std::size_t i = 1; i < equalities.size(); ++i)
      constraints = and2tc(constraints, equalities[i]);

    // and add it to the goto program
    goto_programt::targett loop_exit = lit->get_original_loop_exit();

    goto_programt::instructiont i;
    i.make_assertion(not2tc(constraints));
    i.location = loop_exit->location;
    i.location.user_provided(true);
    i.loop_number = loop_exit->loop_number;
    i.inductive_assertion = true;

    fit->second.body.insert_swap(loop_exit, i);

    // recalculate numbers, etc.
    symex->goto_functions.update();
    return;
  }
}

smt_convt::resultt bmct::run_thread(std::shared_ptr<symex_target_equationt> &eq)
{
  fine_timet symex_start = current_time();
  try
  {
    goto_symext::symex_resultt result = options.get_bool_option("schedule")
                                          ? symex->generate_schedule_formula()
                                          : symex->get_next_formula();

    fine_timet symex_stop = current_time();

    eq = std::dynamic_pointer_cast<symex_target_equationt>(result.target);

    log_status(
      "Symex completed in: {}s ({} assignments)",
      time2string(symex_stop - symex_start),
      eq->SSA_steps.size());

    if (options.get_bool_option("double-assign-check"))
      eq->check_for_duplicate_assigns();

    BigInt ignored;
    for (auto &a : algorithms)
    {
      a->run(eq->SSA_steps);
      ignored += a->ignored();
    }

    if (
      options.get_bool_option("program-only") ||
      options.get_bool_option("program-too"))
      show_program(*eq);

    if (options.get_bool_option("program-only"))
      return smt_convt::P_SMTLIB;

    log_status(
      "Generated {} VCC(s), {} remaining after simplification ({} assignments)",
      result.total_claims,
      result.remaining_claims,
      BigInt(eq->SSA_steps.size()) - ignored);

    if (options.get_bool_option("document-subgoals"))
    {
      std::ostringstream oss;
      document_subgoals(*eq, oss);
      log_status("{}", oss.str());
      return smt_convt::P_SMTLIB;
    }

    if (options.get_bool_option("show-vcc"))
    {
      show_vcc(*eq);
      return smt_convt::P_SMTLIB;
    }

    if (result.remaining_claims == 0)
    {
      if (options.get_bool_option("smt-formula-only"))
      {
        log_status(
          "No VCC remaining, no SMT formula will be generated for"
          " this program\n");
        return smt_convt::P_SMTLIB;
      }

      return smt_convt::P_UNSATISFIABLE;
    }

    if (options.get_bool_option("ltl"))
    {
      int res = ltl_run_thread(*eq);
      if (res == -1)
        return smt_convt::P_SMTLIB;
      if (res < 0)
        return smt_convt::P_ERROR;
      // Record that we've seen this outcome; later decide what the least
      // outcome was.
      ltl_results_seen[res]++;
      return smt_convt::P_UNSATISFIABLE;
    }

    if (!options.get_bool_option("smt-during-symex"))
    {
      runtime_solver =
        std::unique_ptr<smt_convt>(create_solver("", ns, options));
    }

    if (
      options.get_bool_option("multi-property") &&
      options.get_bool_option("base-case"))
      return multi_property_check(*eq, result.remaining_claims);

    return run_decision_procedure(*runtime_solver, *eq);
  }

  catch (std::string &error_str)
  {
    log_error("{}", error_str);
    return smt_convt::P_ERROR;
  }

  catch (const char *error_str)
  {
    log_error("{}", error_str);
    return smt_convt::P_ERROR;
  }

  catch (std::bad_alloc &)
  {
    log_error("Out of memory\n");
    return smt_convt::P_ERROR;
  }
}

int bmct::ltl_run_thread(symex_target_equationt &equation) const
{
  /* LTL checking - first check for whether we have a negative prefix, then
   * the indeterminate ones. */
  using Type = std::pair<std::string_view, ltl_res>;
  static constexpr std::array seq = {
    Type{"LTL_BAD", ltl_res_bad},
    Type{"LTL_FAILING", ltl_res_failing},
    Type{"LTL_SUCCEEDING", ltl_res_succeeding},
  };

  for (const auto &[which, check] : seq)
  {
    size_t num_asserts = 0;

    /* Start by turning all assertions that aren't the sought prefix assertion
     * into skips. */
    for (auto &SSA_step : equation.SSA_steps)
      if (SSA_step.is_assert())
      {
        if (SSA_step.comment != which)
          SSA_step.type = goto_trace_stept::SKIP;
        else
          num_asserts++;
      }

    smt_convt::resultt result = smt_convt::P_UNSATISFIABLE;
    log_status("Checking for {}", which);
    if (num_asserts != 0)
    {
      std::unique_ptr<smt_convt> smt_conv(create_solver("", ns, options));
      result = run_decision_procedure(*smt_conv, equation);
      if (result == smt_convt::P_SATISFIABLE)
        log_status("Found trace satisfying {}", which);
    }
    else
      log_warning("Couldn't find {} assertion", which);

    /* Turn skip steps back into assertions. */
    for (auto &SSA_step : equation.SSA_steps)
      if (SSA_step.is_skip())
        for (const auto &[which2, _] : seq)
          if (SSA_step.comment == which2)
          {
            SSA_step.type = goto_trace_stept::ASSERT;
            break;
          }

    switch (result)
    {
    case smt_convt::P_SATISFIABLE:
      return check;
    case smt_convt::P_ERROR:
      return -2;
    case smt_convt::P_SMTLIB:
      return -1;
    case smt_convt::P_UNSATISFIABLE:
      continue;
    }
  }

  /* Otherwise, we just got a good prefix. */
  return ltl_res_good;
}

smt_convt::resultt bmct::multi_property_check(
  const symex_target_equationt &eq,
  size_t remaining_claims)
{
  // As of now, it only makes sense to do this for the base-case
  assert(
    options.get_bool_option("base-case") &&
    "Multi-property only supports base-case");

  // Initial values
  smt_convt::resultt final_result = smt_convt::P_UNSATISFIABLE;
  std::atomic_size_t ce_counter = 0;
  std::unordered_set<size_t> jobs;
  std::mutex result_mutex;
  std::unordered_set<std::string> reached_claims;
  // For coverage info
  std::unordered_multiset<std::string> reached_mul_claims;
  bool is_assert_cov = options.get_bool_option("assertion-coverage") ||
                       options.get_bool_option("assertion-coverage-claims");
  bool is_cond_cov = options.get_bool_option("condition-coverage") ||
                     options.get_bool_option("condition-coverage-claims") ||
                     options.get_bool_option("condition-coverage-rm") ||
                     options.get_bool_option("condition-coverage-claims-rm");
  bool is_keep_verified = options.get_bool_option("keep-verified-claims");

  // For multi-kind/ multi-incr
  bool is_clear_verified = (options.get_bool_option("k-induction") ||
                            options.get_bool_option("incremental-bmc") ||
                            options.get_bool_option("k-induction-parallel")) &&
                           !is_keep_verified;
  std::set<std::pair<std::string, std::string>> to_remove_claims = {{}};
  // For multi-fail-fast
  const std::string fail_fast = options.get_option("multi-fail-fast");
  const bool is_fail_fast = !fail_fast.empty() ? true : false;
  const int fail_fast_limit = is_fail_fast ? stoi(fail_fast) : 0;
  int fail_fast_cnt = 0;
  if (is_fail_fast && fail_fast_limit < 0)
  {
    log_error("the value of multi-fail-fast should be positive!");
    abort();
  }

  // TODO: This is the place to check a cache
  for (size_t i = 1; i <= remaining_claims; i++)
    jobs.emplace(i);

  /* This is a JOB that will:
   * 1. Generate a solver instance for a specific claim (@parameter i)
   * 2. Solve the instance
   * 3. Generate a Counter-Example (or Witness)
   *
   * This job also affects the environment by using:
   * - &ce_counter: for generating the Counter Example file name
   * - &final_result: if the current instance is SAT, then we known that the current k contains a bug
   *
   * Finally, this function is affected by the "multi-fail-fast" option, which makes this instance stop
   * if final_result is set to SAT
   */
  auto job_function = [this,
                       &eq,
                       &ce_counter,
                       &final_result,
                       &result_mutex,
                       &reached_claims,
                       &reached_mul_claims,
                       &is_assert_cov,
                       &is_cond_cov,
                       &is_keep_verified,
                       &is_clear_verified,
                       &is_fail_fast,
                       &fail_fast_limit,
                       &fail_fast_cnt,
                       &to_remove_claims](const size_t &i) {
    //"multi-fail-fast n": stop after first n SATs found.
    if (is_fail_fast && fail_fast_cnt >= fail_fast_limit)
      return;

    // Since this is just a copy, we probably don't need a lock
    symex_target_equationt local_eq = eq;

    // Set up the current claim and disable slice info output
    bool is_goto_cov = is_assert_cov || is_cond_cov;
    claim_slicer claim(i, false, is_goto_cov, ns);
    claim.run(local_eq.SSA_steps);

    // Drop claims that verified to be failed
    // we use the "comment + location" to distinguish each claim
    // to avoid double verifying the claims that are already verified
    bool is_verified = false;
    std::string cmt_loc;
    cmt_loc = claim.claim_msg + "\t" + claim.claim_loc;
    if (is_assert_cov)
      // C++20 reached_mul_claims.contains
      is_verified = reached_mul_claims.count(cmt_loc) ? true : false;
    else
      is_verified = reached_claims.count(cmt_loc) ? true : false;
    if (is_assert_cov && is_verified)
      // insert to the multiset before skipping the verification process
      reached_mul_claims.emplace(cmt_loc);
    if (is_verified && !is_keep_verified)
      return;

    // Slice
    symex_slicet slicer(options);
    slicer.run(local_eq.SSA_steps);

    if (options.get_bool_option("ssa-features-dump"))
    {
      ssa_features features;
      features.run(local_eq.SSA_steps);
    }

    // Initialize a solver
    std::unique_ptr<smt_convt> runtime_solver(create_solver("", ns, options));

    log_status(
      "Solving claim '{}' with solver {}",
      claim.claim_msg,
      runtime_solver->solver_text());

    // Save current instance
    smt_convt::resultt result =
      run_decision_procedure(*runtime_solver, local_eq);

    // If an assertion instance is verified to be violated
    if (result == smt_convt::P_SATISFIABLE)
    {
      bool is_compact_trace = true;
      if (
        options.get_bool_option("no-slice") &&
        !options.get_bool_option("compact-trace"))
        is_compact_trace = false;

      goto_tracet goto_trace;
      build_goto_trace(local_eq, *runtime_solver, goto_trace, is_compact_trace);

      // Store cmt_loc
      if (is_assert_cov)
        reached_mul_claims.emplace(cmt_loc);
      else
        reached_claims.emplace(cmt_loc);

      // Generate Output
      std::string output_file = options.get_option("cex-output");
      if (output_file != "")
      {
        std::ofstream out(fmt::format("{}-{}", ce_counter++, output_file));
        show_goto_trace(out, ns, goto_trace);
      }
      std::ostringstream oss;
      log_fail("\n[Counterexample]\n");
      show_goto_trace(oss, ns, goto_trace);
      log_result("{}", oss.str());
      final_result = result;

      // Update fail-fast-counter
      fail_fast_cnt++;

      // for kind && incr: remove verified claims
      if (is_clear_verified)
        to_remove_claims.insert(
          std::make_pair(claim.claim_msg, claim.claim_loc));
    }
  };

  std::for_each(std::begin(jobs), std::end(jobs), job_function);

  // remove claims
  if (is_clear_verified && !to_remove_claims.empty())
  {
    Forall_goto_functions (f_it, symex->goto_functions)
    {
      if (f_it->second.body_available)
      {
        goto_programt &goto_program = f_it->second.body;
        std::string claim_msg, claim_loc;
        std::pair<std::string, std::string> claim_pair;
        Forall_goto_program_instructions (it, goto_program)
        {
          if (it->is_assert())
          {
            claim_msg = from_expr(ns, "", it->guard);
            claim_loc = it->location.as_string();
            claim_pair = std::make_pair(claim_msg, claim_loc);
            if (to_remove_claims.count(claim_pair))
            {
              it->make_skip();
              to_remove_claims.erase(claim_pair);
            }
          }
        }
      }
    }
  }

  // For coverage
  // Assertion Coverage:
  if (is_assert_cov)
  {
    goto_coveraget tmp(ns, symex->goto_functions);
    const int total = tmp.get_total_instrument();
    const int tracked_instance = reached_mul_claims.size();
    const int total_instance = tmp.get_total_assert_instance();

    if (total)
    {
      log_success("\n[Coverage]\n");
      // The total assertion instances include the assert inside the source file, the unwinding asserts, the claims inserted during the goto-check and so on.
      log_result("Total Asserts: {}", total);
      log_result("Total Assertion Instances: {}", total_instance);
      log_result("Reached Assertion Instances: {}", tracked_instance);
    }

    // show claims
    if (options.get_bool_option("assertion-coverage-claims"))
    {
      // reached claims:
      for (const auto &claim : reached_mul_claims)
      {
        log_status("  {}", claim);
      }
    }

    if (total_instance != 0)
      log_result(
        "Assertion Instances Coverage: {}%",
        tracked_instance * 100.0 / total_instance);
    else
      log_result("Assertion Instances Coverage: 0%");
  }

  // Condition Coverage:
  else if (is_cond_cov)
  {
    log_success("\n[Coverage]\n");

    // not all the claims are cond-cov instrumentations
    // thus we need to skip the irrelevant claims
    // when comparing 'total_cond_assert' and 'reached_claims'
    goto_coveraget tmp(ns, symex->goto_functions);
    const std::set<std::pair<std::string, std::string>> &total_cond_assert =
      tmp.get_total_cond_assert();
    size_t total_instance = total_cond_assert.size();
    size_t reached_instance = 0;
    size_t short_circuit_instance = 0;
    size_t sat_instance = 0;
    size_t unsat_instance = 0;

    // show claims
    bool cond_show_claims =
      options.get_bool_option("condition-coverage-claims") ||
      options.get_bool_option("condition-coverage-claims-rm");

    // reached claims:
    auto total_cond_assert_cpy = total_cond_assert;
    for (const auto &claim_pair : total_cond_assert)
    {
      std::string claim_msg = claim_pair.first;
      std::string claim_loc = claim_pair.second;
      std::string claim = claim_msg + "\t" + claim_loc;
      if (reached_claims.count(claim))
      {
        // show sat claims
        if (cond_show_claims)
          log_status("  {} : SATISFIED", claim);

        // update counter +=2
        // as we handle ass and !ass at the same time
        reached_instance += 2;

        // update sat counter
        ++sat_instance;

        // prevent double count
        reached_claims.erase(claim);
        total_cond_assert_cpy.erase(claim_pair);

        // reversal: obtain !ass
        if (
          claim_msg[0] == '!' && claim_msg[1] == '(' && claim_msg.back() == ')')
          // e.g. !(a==1)
          claim_msg = claim_msg.substr(2, claim_msg.length() - 3);
        else
          claim_msg = "!(" + claim_msg + ")";
        std::string r_claim = claim_msg + "\t" + claim_loc;

        if (reached_claims.count(r_claim))
        {
          ++sat_instance;
          if (cond_show_claims)
            log_result("  {} : SATISFIED", r_claim);
        }
        else
        {
          ++unsat_instance;
          if (cond_show_claims)
            log_result("  {} : UNSATISFIED", r_claim);
        }

        // prevent double count
        // e.g if( a ==0 && a == 0)
        // we only count a==0 and !(a==0) once
        reached_claims.erase(r_claim);
        std::pair<std::string, std::string> _pair =
          std::make_pair(claim_msg, claim_loc);
        total_cond_assert_cpy.erase(_pair);
      }
    }

    // the remain unreached instrumentaion are regarded as short-circuited
    //! the reached_claims might not be empty (due to unwinding assertions)
    short_circuit_instance = total_cond_assert_cpy.size();

    // show short-circuited:
    if (cond_show_claims && short_circuit_instance > 0)
    {
      log_success("[Short Circuited Conditions]\n");
      for (const auto &claim_pair : total_cond_assert_cpy)
      {
        std::string claim_msg = claim_pair.first;
        std::string claim_loc = claim_pair.second;
        std::string claim = claim_msg + "\t" + claim_loc;
        log_result("  {}", claim);
      }
    }

    // show the number
    log_result("Reached Conditions:  {}", reached_instance);
    log_result("Short Circuited Conditions:  {}", short_circuit_instance);
    log_result(
      "Total Conditions:  {}\n", reached_instance + short_circuit_instance);

    log_result("Condition Properties - SATISFIED:  {}", sat_instance);
    log_result("Condition Properties - UNSATISFIED:  {}\n", unsat_instance);

    if (total_instance != 0)
      log_result(
        "Condition Coverage: {}%", sat_instance * 100.0 / total_instance);
    else
      log_result("Condition Coverage: 0%");
  }
  return final_result;
}
