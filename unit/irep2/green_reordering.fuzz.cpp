#include <cctype>
#include <cassert>
#include <util/green_reordering.h>
#include "cache_fuzz_utils.h"

extern "C" int LLVMFuzzerTestOneInput(const char *Data, size_t Size)
{
  std::string input(Data);
  expr_generator_fuzzer fuzzer(input);
  if(fuzzer.is_expr_valid())
  {
    // Check original expression
    expr2tc actual = fuzzer.convert_input_to_expression();

    auto relation = fuzzer.get_relation();
    auto binop = fuzzer.get_binop();
    auto lhs = fuzzer.get_lhs_names();
    auto rhs = fuzzer.get_rhs_names();

    // Checks if the function recreates the expression correctly
    expr2tc original_recreated =
      fuzzer.get_correct_expression(relation, binop, lhs, rhs);

    assert(actual->crc() == original_recreated->crc());

    // Sort each side
    sort(lhs.begin(), lhs.end(), [](const auto &x, const auto &y) {
      return x < y;
    });

    sort(rhs.begin(), rhs.end(), [](const auto &x, const auto &y) {
      return x < y;
    });

    // Recreate it sorted
    expr2tc expected = fuzzer.get_correct_expression(relation, binop, lhs, rhs);

    // Apply sorting on original
    expr_variable_reordering f;
    f.run(actual);

    // Check if expected == actual
    assert(actual->crc() == expected->crc());
  }
  return 0;
}
