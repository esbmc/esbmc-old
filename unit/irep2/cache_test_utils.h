#pragma once
#include <irep2/irep2.h>
#include <irep2/irep2_utils.h>
#include <goto-symex/symex_target_equation.h>

bool is_symbols_equal(expr2tc &v1, expr2tc &v2);

bool is_unsigned_equal(expr2tc &v1, expr2tc &v2);

symbol2tc create_unsigned_32_symbol_expr(std::string name);

constant_int2tc create_unsigned_32_value_expr(unsigned value);

constant_int2tc create_signed_32_value_expr(int value);

add2tc create_unsigned_32_add_expr(expr2tc &side1, expr2tc &side2);

neg2tc create_unsigned_32_neg_expr(expr2tc &value);

not2tc create_not_expr(expr2tc &value);

add2tc create_signed_32_add_expr(expr2tc &side1, expr2tc &side2);

mul2tc create_unsigned_32_mul_expr(expr2tc &side1, expr2tc &side2);

mul2tc create_signed_32_mul_expr(expr2tc &side1, expr2tc &side2);

lessthan2tc create_lesser_relation(expr2tc &lhs, expr2tc &rhs);

lessthanequal2tc create_lessthanequal_relation(expr2tc &lhs, expr2tc &rhs);

greaterthanequal2tc
create_greaterthanequal_relation(expr2tc &lhs, expr2tc &rhs);

greaterthan2tc create_greater_relation(expr2tc &lhs, expr2tc &rhs);

equality2tc create_equality_relation(expr2tc &lhs, expr2tc &rhs);

void create_assignment(
  symex_target_equationt::SSA_stepst &output,
  expr2tc &rhs);

void create_assumption(
  symex_target_equationt::SSA_stepst &output,
  expr2tc &cond);

void create_assert(symex_target_equationt::SSA_stepst &output, expr2tc &cond);

// Pre-Built expressions
// These expressions will be used thorough the
// test cases as simple validations.
// The fuzzer should use the above method as a way to construct
// and validate expressions
void init_test_values();

// ((y + x) + 7) == 9
expr2tc equality_1();
// ((x + y) + 7) == 9
expr2tc equality_1_ordered();

void is_equality_1_equivalent(expr2tc &actual, expr2tc &expected);

// (1 + x) == 0
expr2tc equality_2();
// (x + 1) == 0
expr2tc equality_2_ordered();

// (y + 4) == 8
expr2tc equality_3();
// (y + 4) == 8
expr2tc equality_3_ordered();

// (x + 0) == 0
expr2tc equality_4();
// (x + 0) == 0
expr2tc equality_4_ordered();
