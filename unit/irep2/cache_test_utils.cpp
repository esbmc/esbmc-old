#include "cache_test_utils.h"

bool is_symbols_equal(expr2tc &v1, expr2tc &v2)
{
  assert(v1->expr_id == expr2t::expr_ids::symbol_id);
  assert(v2->expr_id == expr2t::expr_ids::symbol_id);

  std::shared_ptr<symbol_data> symbol1;
  symbol1 = std::dynamic_pointer_cast<symbol_data>(v1);

  std::shared_ptr<symbol_data> symbol2;
  symbol2 = std::dynamic_pointer_cast<symbol_data>(v2);

  auto name1 = symbol1->get_symbol_name();
  auto name2 = symbol2->get_symbol_name();
  return symbol1->get_symbol_name() == symbol2->get_symbol_name();
}

bool is_unsigned_equal(expr2tc &v1, expr2tc &v2)
{
  assert(v1->expr_id == expr2t::expr_ids::constant_int_id);
  assert(v2->expr_id == expr2t::expr_ids::constant_int_id);

  constant_int2tc value1 = v1;
  constant_int2tc value2 = v2;

  return value1->value.compare(value2->value) == 0;
}

symbol2tc create_unsigned_32_symbol_expr(std::string name)
{
  unsignedbv_type2tc u32type(32);
  irep_idt var(name);
  symbol2tc expression(u32type, var);
  return expression;
}

constant_int2tc create_unsigned_32_value_expr(unsigned value)
{
  unsignedbv_type2tc u32type(32);
  BigInt num(value);
  constant_int2tc expression(u32type, num);
  return expression;
}

constant_int2tc create_signed_32_value_expr(int value)
{
  signedbv_type2tc i32type(32);
  BigInt num(value);
  constant_int2tc expression(i32type, num);
  return expression;
}

add2tc create_unsigned_32_add_expr(expr2tc &side1, expr2tc &side2)
{
  unsignedbv_type2tc type(32);
  add2tc expression(type, side1, side2);
  return expression;
}

neg2tc create_unsigned_32_neg_expr(expr2tc &value)
{
  unsignedbv_type2tc type(32);
  neg2tc expression(type, value);
  return expression;
}

not2tc create_not_expr(expr2tc &value)
{
  not2tc expression(value);
  return expression;
}

add2tc create_signed_32_add_expr(expr2tc &side1, expr2tc &side2)
{
  signedbv_type2tc type(32);
  add2tc expression(type, side1, side2);
  return expression;
}

mul2tc create_unsigned_32_mul_expr(expr2tc &side1, expr2tc &side2)
{
  unsignedbv_type2tc type(32);
  mul2tc expression(type, side1, side2);
  return expression;
}

mul2tc create_signed_32_mul_expr(expr2tc &side1, expr2tc &side2)
{
  signedbv_type2tc type(32);
  mul2tc expression(type, side1, side2);
  return expression;
}

lessthan2tc create_lesser_relation(expr2tc &lhs, expr2tc &rhs)
{
  lessthan2tc relation(lhs, rhs);
  return relation;
}

lessthanequal2tc create_lessthanequal_relation(expr2tc &lhs, expr2tc &rhs)
{
  lessthanequal2tc relation(lhs, rhs);
  return relation;
}

greaterthanequal2tc create_greaterthanequal_relation(expr2tc &lhs, expr2tc &rhs)
{
  greaterthanequal2tc relation(lhs, rhs);
  return relation;
}

greaterthan2tc create_greater_relation(expr2tc &lhs, expr2tc &rhs)
{
  greaterthan2tc relation(lhs, rhs);
  return relation;
}

equality2tc create_equality_relation(expr2tc &lhs, expr2tc &rhs)
{
  equality2tc relation(lhs, rhs);
  return relation;
}

void create_assignment(symex_target_equationt::SSA_stepst &output, expr2tc &rhs)
{
  symex_target_equationt::SSA_stept step1;
  step1.type = goto_trace_stept::ASSIGNMENT;
  step1.rhs = rhs;
  output.push_back(step1);
}

void create_assumption(
  symex_target_equationt::SSA_stepst &output,
  expr2tc &cond)
{
  symex_target_equationt::SSA_stept step1;
  step1.type = goto_trace_stept::ASSUME;
  step1.cond = cond;
  output.push_back(step1);
}

void create_assert(symex_target_equationt::SSA_stepst &output, expr2tc &cond)
{
  symex_target_equationt::SSA_stept step1;
  step1.type = goto_trace_stept::ASSERT;
  step1.cond = cond;
  output.push_back(step1);
}

// Pre-Built expressions
// These expressions will be used thorough the
// test cases as simple validations.
// The fuzzer should use the above method as a way to construct
// and validate expressions

namespace
{
symbol2tc Y = create_unsigned_32_symbol_expr("Y");
symbol2tc X = create_unsigned_32_symbol_expr("X");
constant_int2tc values[10];
constant_int2tc neg_values[10];

} // namespace

void init_test_values()
{
  static bool is_initialized = false;
  if(is_initialized)
    return;
  for(int i = 0; i < 10; i++)
  {
    values[i] = create_signed_32_value_expr(i);
    neg_values[i] = create_signed_32_value_expr(0 - i);
  }
  is_initialized = true;
}

// ((y + x) + 7) == 9
expr2tc equality_1()
{
  add2tc add_1 = create_signed_32_add_expr(Y, X);
  add2tc add_2 = create_signed_32_add_expr(add_1, values[7]);
  equality2tc result = create_equality_relation(add_2, values[9]);
  return result;
}

// (7 + (x + y)) == 9
expr2tc equality_1_ordered()
{
  add2tc add_1 = create_signed_32_add_expr(values[7], X);
  add2tc add_2 = create_signed_32_add_expr(add_1, Y);
  equality2tc result = create_equality_relation(add_2, values[9]);
  return result;
}

// (-2 + (x + y)) == 0
expr2tc equality_1_green_normal()
{
  add2tc add_1 = create_signed_32_add_expr(neg_values[2], X);
  add2tc add_2 = create_signed_32_add_expr(add_1, Y);
  equality2tc result = create_equality_relation(add_2, values[0]);
  return result;
}

void is_equality_1_equivalent(expr2tc &actual, expr2tc &expected)
{
  std::shared_ptr<equality2t> actual_relation;
  actual_relation = std::dynamic_pointer_cast<equality2t>(actual);
  std::shared_ptr<equality2t> expected_relation;
  expected_relation = std::dynamic_pointer_cast<equality2t>(expected);

  // RHS OF RELATION
  bool is_rhs_value_equal =
    is_unsigned_equal(actual_relation->side_2, expected_relation->side_2);
  assert(is_rhs_value_equal);

  // LHS OF RELATION

  std::shared_ptr<arith_2ops> actual_outter_add;
  actual_outter_add =
    std::dynamic_pointer_cast<arith_2ops>(actual_relation->side_1);

  std::shared_ptr<arith_2ops> expected_outter_add;
  expected_outter_add =
    std::dynamic_pointer_cast<arith_2ops>(expected_relation->side_1);

  // First symbol
  bool outter_symbol =
    is_unsigned_equal(actual_outter_add->side_2, expected_outter_add->side_2);
  assert(outter_symbol);

  // Inner add
  std::shared_ptr<arith_2ops> actual_inner_add;
  actual_inner_add =
    std::dynamic_pointer_cast<arith_2ops>(actual_outter_add->side_1);

  std::shared_ptr<arith_2ops> expected_inner_add;
  expected_inner_add =
    std::dynamic_pointer_cast<arith_2ops>(expected_outter_add->side_1);

  assert(
    is_symbols_equal(actual_inner_add->side_1, expected_inner_add->side_1));
  assert(
    is_symbols_equal(actual_inner_add->side_2, expected_inner_add->side_2));

  assert(actual->crc() == expected->crc());
}

// (1 + x) == 0
expr2tc equality_2()
{
  add2tc add_1 = create_signed_32_add_expr(values[1], X);
  equality2tc result = create_equality_relation(add_1, values[0]);
  return result;
}

// (1 + x) == 0
expr2tc equality_2_ordered()
{
  add2tc add_1 = create_signed_32_add_expr(values[1], X);
  equality2tc result = create_equality_relation(add_1, values[0]);
  return result;
}

// (x + 1) == 0
expr2tc equality_2_green_normal()
{
  return equality_2_ordered();
}

// (y + 4) == 8
expr2tc equality_3()
{
  add2tc add_1 = create_signed_32_add_expr(Y, values[4]);
  equality2tc result = create_equality_relation(add_1, values[8]);
  return result;
}

// (4 + y) == 8
expr2tc equality_3_ordered()
{
  add2tc add_1 = create_signed_32_add_expr(values[4], Y);
  equality2tc result = create_equality_relation(add_1, values[8]);
  return result;
}

// (y - 4) == 0
expr2tc equality_3_green_normal()
{
  add2tc add_1 = create_signed_32_add_expr(Y, neg_values[4]);
  equality2tc result = create_equality_relation(add_1, values[0]);
  return result;
}

// (x + 0) == 0
expr2tc equality_4()
{
  add2tc add_1 = create_signed_32_add_expr(X, values[0]);
  equality2tc result = create_equality_relation(add_1, values[0]);
  return result;
}

// (0 + x) == 0
expr2tc equality_4_ordered()
{
  add2tc add_1 = create_signed_32_add_expr(values[0], X);
  equality2tc result = create_equality_relation(add_1, values[0]);
  return result;
}

// (x + 0) == 0
expr2tc equality_4_green_normal()
{
  return equality_4();
}
