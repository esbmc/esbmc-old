/// \file
/// Interval Analysis

#include "interval_analysis.h"
#include <goto-programs/abstract-interpretation/interval_analysis.h>
#include <goto-programs/abstract-interpretation/interval_domain.h>
#include <unordered_set>
#include <util/prefix.h>
#include <goto-programs/goto_loops.h>
#include <util/time_stopping.h>

template <class Interval>
inline void optimize_expr_interval(expr2tc &expr, const interval_domaint &state)
{
  // Only integers for now (more implementation is needed for floats)
  if (!(is_signedbv_type(expr->type) || is_unsignedbv_type(expr->type) ||
        is_bool_type(expr->type)))
    return;

  // Forward Analysis
  auto interval = state.get_interval<Interval>(expr);

  // Singleton Propagation
  if (interval.singleton() && is_bv_type(expr))
  {
    // Right now we can only do that for bitvectors
    expr = state.make_expression_value<Interval>(interval, expr->type, true);
    return;
  }

  // Boolean intervals
  if (is_bool_type(expr))
  {
    // Expression is always true
    if (!interval.contains(0))
    {
      expr = gen_true_expr();
      return;
    }

    // interval is [0,0] which is always false
    if (interval.singleton())
    {
      expr = gen_false_expr();
      return;
    }
  }
}

static void optimize_expression(expr2tc &expr, const interval_domaint &state)
{
  // Preconditions
  if (is_nil_expr(expr))
    return;

  // We can't simplify addr-of sub-expr.
  // int x = 3; int *ptr = &x; would become int x = 3; int *ptr = &3;
  if (is_address_of2t(expr))
    return;

  // We can't replace the target of an assignment.
  // int x = 3; x = 4; would become int x = 3; 3 = 4;
  if (is_code_assign2t(expr))
  {
    optimize_expression(to_code_assign2t(expr).source, state);
    return;
  }

  // Function calls might have an implicit assignment
  if (is_code_function_call2t(expr))
  {
    for (auto &x : to_code_function_call2t(expr).operands)
      optimize_expression(x, state);
    return;
  }

  if (interval_domaint::enable_wrapped_intervals)
    optimize_expr_interval<wrapped_interval>(expr, state);
  else
    optimize_expr_interval<interval_domaint::integer_intervalt>(expr, state);

  // Try sub-expressions
  expr->Foreach_operand(
    [&state](expr2tc &e) -> void { optimize_expression(e, state); });
  simplify(expr);
}

void optimize_function(
  const ait<interval_domaint> &interval_analysis,
  goto_functiont &goto_function)
{
  // Inline optimizations
  Forall_goto_program_instructions (i_it, goto_function.body)
  {
    const interval_domaint &d = interval_analysis[i_it];

    // Singleton Propagation
    optimize_expression(i_it->code, d);
    optimize_expression(i_it->guard, d);
  }
}

/*
 * Instrument an assume containing all the restriction for the set of symbols
 */
inline void instrument_symbol_constraints(
  const ait<interval_domaint> &interval_analysis,
  std::unordered_set<expr2tc, irep2_hash> symbols,
  goto_programt::instructionst::iterator &it,
  goto_functiont &goto_function)
{
  std::vector<expr2tc> symbol_constraints;
  auto state_iterator = interval_analysis.state_map.find(it);
  // We may be trying to instrument an unreachable state
  if (state_iterator == interval_analysis.state_map.end())
    return;
  const interval_domaint &d = state_iterator->second;
  for (const auto &symbol_expr : symbols)
  {
    expr2tc tmp = d.make_expression(symbol_expr);
    if (!is_true(tmp))
      symbol_constraints.push_back(tmp);
  }

  if (!symbol_constraints.empty())
  {
    goto_programt::instructiont instruction;
    instruction.make_assumption(conjunction(symbol_constraints));
    instruction.inductive_step_instruction = config.options.is_kind();
    instruction.location = it->location;
    instruction.function = it->function;
    goto_function.body.insert_swap(it++, instruction);
  }
}

/**
 * Instrument loops with all variables that are affected by it (not only the guards)
 *
 * Before:
 * before-loop
 * 1 : IF !(COND == 0) GOTO 2
 * X = Y // X changes and Y is read-only
 * ...
 * GOTO 1
 * 2: after-loop
 *
 * After:
 * before-loop
 * ASSUME (COND >= some_value && ... && Y <= some_value2)
 * 1: IF !(COND == 0) GOTO 2
 * X = Y // X changes and Y is read-only
 * ...
 * ASSUME (COND >= some_value && ... && Y <= some_value2)
 * GOTO 1
 * 2: ASSUME (COND >= some_value && ... && Y <= some_value2)
 * after-loop
 */
void instrument_loops(
  const ait<interval_domaint> &interval_analysis,
  goto_functionst &program)
{
  Forall_goto_functions (f_it, program)
  {
    if (!f_it->second.body_available)
      continue;

    auto loop = goto_loopst(f_it->first, program, f_it->second);
    for (auto l : loop.get_loops())
    {
      std::unordered_set<expr2tc, irep2_hash> symbols;
      for (auto v : l.get_modified_loop_vars())
        symbols.insert(v);

      for (auto v : l.get_unmodified_loop_vars())
        symbols.insert(v);

      // TODO: Instrument before-loop
      // Assumption during the loop
      auto it = l.get_original_loop_exit();
      instrument_symbol_constraints(
        interval_analysis, symbols, it, f_it->second);
      // it was incremented, we are now in the next instruction
      instrument_symbol_constraints(
        interval_analysis, symbols, it, f_it->second);
    }
  }
}

void instrument_intervals(
  const ait<interval_domaint> &interval_analysis,
  goto_functiont &goto_function,
  const INTERVAL_INSTRUMENTATION_MODE instrument_mode)
{
  assert(instrument_mode != INTERVAL_INSTRUMENTATION_MODE::LOOP_MODE);
  if (!goto_function.body_available)
    return;
  std::unordered_set<expr2tc, irep2_hash> function_symbols;
  Forall_goto_program_instructions (i_it, goto_function.body)
  {
    get_symbols(i_it->code, function_symbols);
    get_symbols(i_it->guard, function_symbols);
  }
  Forall_goto_program_instructions (i_it, goto_function.body)
  {
    std::unordered_set<expr2tc, irep2_hash> local_symbols;
    get_symbols(i_it->code, local_symbols);
    get_symbols(i_it->guard, local_symbols);
    switch (instrument_mode)
    {
    case INTERVAL_INSTRUMENTATION_MODE::NO_INSTRUMENTATION:
    case INTERVAL_INSTRUMENTATION_MODE::LOOP_MODE:
      return;
    case INTERVAL_INSTRUMENTATION_MODE::ALL_INSTRUCTIONS_FULL:
      instrument_symbol_constraints(
        interval_analysis, function_symbols, i_it, goto_function);
      break;
    case INTERVAL_INSTRUMENTATION_MODE::ALL_INSTRUCTIONS_LOCAL:
      instrument_symbol_constraints(
        interval_analysis, local_symbols, i_it, goto_function);
      break;
    case INTERVAL_INSTRUMENTATION_MODE::GUARD_INSTRUCTIONS_FULL:
      if (!(i_it->is_goto() || i_it->is_assume() || i_it->is_assert()))
        continue;
      instrument_symbol_constraints(
        interval_analysis, function_symbols, i_it, goto_function);
      break;
    case INTERVAL_INSTRUMENTATION_MODE::GUARD_INSTRUCTIONS_LOCAL:
      if (!(i_it->is_goto() || i_it->is_assume() || i_it->is_assert()))
        continue;
      instrument_symbol_constraints(
        interval_analysis, local_symbols, i_it, goto_function);
      break;
    }
  }
}

void dump_intervals(
  std::ostringstream &out,
  const goto_functiont &goto_function,
  const ait<interval_domaint> &interval_analysis)
{
  forall_goto_program_instructions (i_it, goto_function.body)
  {
    auto print_vars = [&out, &i_it](const auto &map) {
      for (const auto &interval : map)
      {
        // "state,var,min,max,bot,top";
        out << fmt::format(
          "{},{},{},{},{},{},{},{},{}\n",
          i_it->location_number,
          i_it->location.line().as_string(),
          i_it->location.column().as_string(),
          i_it->location.function().as_string(),
          interval.first,
          (interval.second.lower ? *interval.second.lower : "-inf"),
          (interval.second.upper ? *interval.second.upper : "inf"),
          interval.second.is_bottom(),
          interval.second.is_top());
      }
    };
  }
}

#include <fstream>

void interval_analysis(
  goto_functionst &goto_functions,
  const namespacet &ns,
  const optionst &options,
  const INTERVAL_INSTRUMENTATION_MODE instrument_mode)
{
  fine_timet algorithm_start = current_time();
  // TODO: add options for instrumentation mode
  ait<interval_domaint> interval_analysis;
  interval_domaint::set_options(options);
  interval_analysis(goto_functions, ns);

  if (options.get_bool_option("interval-analysis-dump"))
  {
    std::ostringstream oss;
    interval_analysis.output(goto_functions, oss);
    log_status("{}", oss.str());
  }

  std::string csv_file = options.get_option("interval-analysis-csv-dump");
  if (!csv_file.empty())
  {
    std::ostringstream oss;
    oss << "state,line,column,function,var,min,max,bot,top\n";
    Forall_goto_functions (f_it, goto_functions)
      dump_intervals(oss, f_it->second, interval_analysis);

    std::ofstream csv(csv_file);
    csv << oss.str();
  }

  Forall_goto_functions (f_it, goto_functions)
  {
    optimize_function(interval_analysis, f_it->second);
  }

  if (instrument_mode == INTERVAL_INSTRUMENTATION_MODE::LOOP_MODE)
    instrument_loops(interval_analysis, goto_functions);
  else
  {
    Forall_goto_functions (f_it, goto_functions)
    {
      instrument_intervals(interval_analysis, f_it->second, instrument_mode);
    }
  }
  goto_functions.update();

  fine_timet algorithm_stop = current_time();
  log_status(
    "Interval Analysis time: {}s",
    time2string(algorithm_stop - algorithm_start));
}
