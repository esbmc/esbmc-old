#include "irep2/irep2_type.h"
#include <goto-symex/features.h>

bool ssa_features::run(symex_target_equationt::SSA_stepst &steps)
{
  features.clear();

  for (const auto &step : steps)
  {
    for (const expr2tc &e : std::array{step.cond, step.guard})
      check(e);
  }

  print_result();

  return false;
}

void ssa_features::check(const expr2tc &e)
{
  if (!e)
    return;

  // TODO: We could add a cache
  if (is_array_type(e->type))
    features.insert(SSA_FEATURES::ARRAY);

  if (is_struct_type(e->type))
    features.insert(SSA_FEATURES::STRUCTS);

  switch (e->expr_id)
  {
  case expr2t::constant_fixedbv_id:
  case expr2t::constant_floatbv_id:
    features.insert(SSA_FEATURES::NON_INTEGER_NUMERAL);
    break;

  case expr2t::expr_ids::mul_id:
  case expr2t::expr_ids::div_id:
  case expr2t::modulus_id:
  {
    // TODO: We should deal with some non-linearity here e.g.: division-by-zero
    const auto &arith_op = dynamic_cast<const arith_2ops &>(*e);
    const expr2tc side_1 = arith_op.side_1;
    const expr2tc side_2 = arith_op.side_2;
    if (
      !is_entirely_constant(arith_op.side_1) &&
      !is_entirely_constant(arith_op.side_2))
      features.insert(SSA_FEATURES::NON_LINEAR);

    break;
  }

  case expr2t::shl_id:
  case expr2t::ashr_id:
  case expr2t::lshr_id:
  case expr2t::bitor_id:
  case expr2t::bitand_id:
  case expr2t::bitxor_id:
  case expr2t::bitnand_id:
  case expr2t::bitnor_id:
  case expr2t::bitnxor_id:
  {
    features.insert(SSA_FEATURES::BITWISE_OPERATIONS);
    const auto &bit_op = dynamic_cast<const bit_2ops &>(*e);
    if (
      !is_entirely_constant(bit_op.side_1) &&
      !is_entirely_constant(bit_op.side_2))
      features.insert(SSA_FEATURES::NON_LINEAR);
    break;
  }

  case expr2t::overflow_cast_id:
  case expr2t::overflow_id:
  case expr2t::overflow_neg_id:
    features.insert(SSA_FEATURES::OVERFLOW_ASSERTIONS);
    break;

  default:
    break;
  }

  e->foreach_operand([this](const expr2tc &next) { check(next); });
}

void ssa_features::print_result() const
{
  if (features.count(SSA_FEATURES::NON_LINEAR))
    log_status("SSA: Contains NON LINEAR");
  if (features.count(SSA_FEATURES::NON_INTEGER_NUMERAL))
    log_status("SSA: Contains NON INTEGER NUMERAL");
  if (features.count(SSA_FEATURES::BITWISE_OPERATIONS))
    log_status("SSA: Contains BITWISE OPERATIONS");
  if (features.count(SSA_FEATURES::OVERFLOW_ASSERTIONS))
    log_status("SSA: Contains OVERFLOW ASSERTIONS");
  if (features.count(SSA_FEATURES::ARRAY))
    log_status("SSA: Contains ARRAY");
  if (features.count(SSA_FEATURES::STRUCTS))
    log_status("SSA: Contains STRUCTS");
}

bool ssa_features::is_entirely_constant(const expr2tc &e)
{
  if (is_constant_expr(e))
    return true;

  if (is_pointer_type(e))
    return false;

  switch (e->expr_id)
  {
  case expr2t::overflow_cast_id:
  case expr2t::overflow_id:
  case expr2t::overflow_neg_id:
    return false;

  default:
    break;
  }

  bool result = true;
  e->foreach_operand([this, &result](const expr2tc &next) {
    result &= is_entirely_constant(next);
  });

  return result;
}
