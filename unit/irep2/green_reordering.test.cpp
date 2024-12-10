#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include <irep2/irep2.h>
#include <util/green_reordering.h>
#include "cache_test_utils.h"
// ** Check if trivial cases are ok
// expressions which does not contain a symbol/value or only one at max

TEST_CASE("variable reordering tests", "[caching]")
{
  expr_variable_reordering algorithm;
  SECTION("unsigned_expr_should_not_change_value")
  {
    constant_int2tc life_expr = create_unsigned_32_value_expr(42);
    auto crc = life_expr->crc();
    algorithm.run(life_expr);

    REQUIRE(life_expr->value.compare(42) == 0);
    REQUIRE(life_expr->crc() == crc);
  }

  SECTION("signed_expr_should_not_change_value")
  {
    int meaning_of_death = ~42;
    constant_int2tc death_expr = create_signed_32_value_expr(meaning_of_death);
    auto crc = death_expr->crc();

    algorithm.run(death_expr);

    REQUIRE(death_expr->value.compare(meaning_of_death) == 0);
    REQUIRE(death_expr->crc() == crc);
  }

  SECTION("symbol_expr_should_not_change_value")
  {
    symbol2tc x = create_unsigned_32_symbol_expr("X");
    auto crc = x->crc();

    algorithm.run(x);

    REQUIRE(x->get_symbol_name() == "X");
    REQUIRE(x->crc() == crc);
  }

  SECTION("a_add_b_should_become_a_add_b")
  {
    symbol2tc a = create_unsigned_32_symbol_expr("a");
    symbol2tc b = create_unsigned_32_symbol_expr("b");

    // a + b => a + b
    add2tc add = create_unsigned_32_add_expr(a, b);
    auto crc = add->crc();

    // Check if object is created as expected
    REQUIRE(is_symbols_equal(add->side_1, a));
    REQUIRE(is_symbols_equal(add->side_2, b));

    algorithm.run(add);

    std::shared_ptr<arith_2ops> arith;
    arith = std::dynamic_pointer_cast<arith_2ops>(add);

    // Check if object is reordered correctly
    REQUIRE(is_symbols_equal(a, arith->side_1));
    REQUIRE(is_symbols_equal(b, arith->side_2));

    REQUIRE(add->crc() == crc);
  }

  SECTION("b_add_a_should_become_a_add_b")
  {
    symbol2tc a = create_unsigned_32_symbol_expr("a");
    symbol2tc b = create_unsigned_32_symbol_expr("b");

    // b + a => a + b
    add2tc add = create_unsigned_32_add_expr(b, a);
    auto crc = add->crc();

    // Check if object is created as expected
    REQUIRE(is_symbols_equal(add->side_1, b));
    REQUIRE(is_symbols_equal(add->side_2, a));

    algorithm.run(add);

    // Check if object is reordered correctly
    REQUIRE(is_symbols_equal(add->side_1, a));
    REQUIRE(is_symbols_equal(add->side_2, b));

    REQUIRE(add->crc() != crc);
  }

  SECTION("a_add_value_should_become_value_add_a")
  {
    symbol2tc a = create_unsigned_32_symbol_expr("a");
    constant_int2tc value = create_unsigned_32_value_expr(42);

    // a + 42 => a + 42
    add2tc add = create_unsigned_32_add_expr(a, value);

    // Check if object is created as expected
    REQUIRE(is_symbols_equal(add->side_1, a));
    REQUIRE(is_unsigned_equal(add->side_2, value));

    auto crc = add->crc();

    algorithm.run(add);

    // Check if object is reordered correctly
    REQUIRE(is_unsigned_equal(add->side_1, value));
    REQUIRE(is_symbols_equal(add->side_2, a));
    REQUIRE(add->crc() != crc);
  }

  SECTION("value_add_a_should_become_value_add_a")
  {
    symbol2tc a = create_unsigned_32_symbol_expr("a");
    constant_int2tc value = create_unsigned_32_value_expr(42);

    // 42 + a => a + 42
    add2tc add = create_unsigned_32_add_expr(value, a);

    // Check if object is created as expected
    REQUIRE(is_unsigned_equal(add->side_1, value));
    REQUIRE(is_symbols_equal(add->side_2, a));

    auto crc = add->crc();

    algorithm.run(add);

    // Check if object is reordered correctly
    REQUIRE(is_symbols_equal(add->side_2, a));
    REQUIRE(is_unsigned_equal(add->side_1, value));
    REQUIRE(add->crc() == crc);
  }

  SECTION("not_b_add_a_should_become_a_add_b")
  {
    symbol2tc a = create_unsigned_32_symbol_expr("a");
    symbol2tc b = create_unsigned_32_symbol_expr("b");

    // b + a
    add2tc add = create_unsigned_32_add_expr(b, a);

    // !(b + a)
    not2tc neg = create_not_expr(add);

    // Check if object is created as expected
    REQUIRE(is_symbols_equal(add->side_1, b));
    REQUIRE(is_symbols_equal(add->side_2, a));

    algorithm.run(neg);

    add2tc new_value(neg->value);
    // Check if object is reordered correctly
    REQUIRE(is_symbols_equal(new_value->side_1, a));
    REQUIRE(is_symbols_equal(new_value->side_2, b));
  }

  SECTION("neg_b_add_a_should__become_a_add_b")
  {
    symbol2tc a = create_unsigned_32_symbol_expr("a");
    symbol2tc b = create_unsigned_32_symbol_expr("b");

    // b + a
    add2tc add = create_unsigned_32_add_expr(b, a);

    // !(b + a)
    neg2tc neg = create_unsigned_32_neg_expr(add);

    // Check if object is created as expected
    REQUIRE(is_symbols_equal(add->side_1, b));
    REQUIRE(is_symbols_equal(add->side_2, a));

    algorithm.run(neg);

    add2tc new_value(neg->value);
    // Check if object is reordered correctly
    REQUIRE(is_symbols_equal(new_value->side_1, a));
    REQUIRE(is_symbols_equal(new_value->side_2, b));
  }

  SECTION("equality_1_check")
  {
    init_test_values();

    // ((y + x) + 7) == 9
    auto actual = equality_1();

    // (7 + (x + y)) == 9
    auto expected = equality_1_ordered();

    REQUIRE(actual->crc() != expected->crc());

    algorithm.run(actual);

    REQUIRE(actual->crc() == expected->crc());
  }

  SECTION("equality_2_check")
  {
    init_test_values();

    // (1 + x) == 0
    auto actual = equality_2();

    // (1 + x) == 0
    auto expected = equality_2_ordered();

    REQUIRE(actual->crc() == expected->crc());

    algorithm.run(actual);

    REQUIRE(actual->crc() == expected->crc());
  }

  SECTION("equality_3_check")
  {
    init_test_values();

    // (y + 4) == 8
    auto actual = equality_3();

    // (y + 4) == 8
    auto expected = equality_3_ordered();

    REQUIRE(actual->crc() != expected->crc());

    algorithm.run(actual);

    REQUIRE(actual->crc() == expected->crc());
  }

  SECTION("equality_4_check")
  {
    init_test_values();

    // (x + 0) == 0
    auto actual = equality_4();

    // (0 + x) == 0
    auto expected = equality_4_ordered();

    REQUIRE(actual->crc() != expected->crc());

    algorithm.run(actual);

    REQUIRE(actual->crc() == expected->crc());
  }
}
