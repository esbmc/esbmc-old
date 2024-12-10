#include "cache_fuzz_utils.h"
#include <climits>
namespace
{
inline char convert_char_to_letter(const char input)
{
  const char a = 'a';
  const char z = 'z';
  if(input >= a && input <= z)
    return input;
  const char range = z - a;
  const char result = (input % range) + a;
  return result;
}
} // namespace

expr2tc expr_generator_fuzzer::convert_input_to_expression()
{
  return convert_vectors_to_expression(lhs_names, rhs_names);
}

expr2tc expr_generator_fuzzer::get_correct_expression(
  expr_generator_fuzzer::relation,
  expr_generator_fuzzer::binary_operation,
  std::vector<char> lhs,
  std::vector<char> rhs)
{
  return convert_vectors_to_expression(lhs, rhs);
}

expr2tc expr_generator_fuzzer::convert_char_vector_to_expression(
  std::vector<char> entries,
  expr_generator_fuzzer::binary_operation)
{
  assert(entries.size() != 0);

  // Check trivial case
  if(entries.size() == 1)
    return create_unsigned_32_symbol_expr(std::string(1, entries[0]));

  /**
   * If it is not the trivial case then we create the initial add and apply
   * a fold like algorithm in a way that:
   *
   * a, b, c
   *
   * Becomes:
   *
   * (ADD (a) (b))   [c]
   *
   * Then:
   *
   * (ADD (ADD (a) (b)) (c))
   *
   */
  symbol2tc first = create_unsigned_32_symbol_expr(std::string(1, entries[0]));

  symbol2tc second = create_unsigned_32_symbol_expr(std::string(1, entries[1]));

  // TODO: We only support add for now
  // This is the base (ADD (a) (b))
  add2tc result = create_unsigned_32_add_expr(first, second);

  // Fold like algorithm applying the binary operation
  for(size_t i = 2; i < entries.size(); i++)
  {
    symbol2tc rhs = create_unsigned_32_symbol_expr(std::string(1, entries[i]));

    // TODO: We only support add for now
    add2tc old_add = result;
    result = create_unsigned_32_add_expr(old_add, rhs);
  }

  return result;
}

expr2tc expr_generator_fuzzer::convert_vectors_to_expression(
  std::vector<char> lhs,
  std::vector<char> rhs)
{
  /**
   * Conversion steps:
   *
   * 0 - Check preconditions
   * 1 - Process LHS
   * 2 - Process RHS
   * 3 - Unify
   */

  /* 0 - Check preconditions */
  assert(lhs.size() > 0);
  assert(rhs.size() > 0);

  /* 1 - Process LHS */
  expr2tc lhs_expr = convert_char_vector_to_expression(lhs, this->expr_binop);

  /* 2 - Process RHS */
  expr2tc rhs_expr = convert_char_vector_to_expression(rhs, this->expr_binop);

  /* 3 - Unify */
  // TODO: We only support equality for now
  return create_equality_relation(lhs_expr, rhs_expr);
}

bool expr_generator_fuzzer::is_valid_input()
{
  // 0 - Check the size
  // The minimum valid expression will be in the format 00a%a
  if(data.size() < 5)
    return false;

  /* Since this uses a integer as a index, then the size of the input
   * shouldn't be greater thant max_int
   */
  if((size_t)data.size() > (size_t)INT_MAX)
    return false;

  // 1 - Get the relation
  expr_relation = relation_from_unsigned((unsigned)data[0]);
  expr_binop = binop_from_unsigned((unsigned)data[0]);

  // 2 - Parse the LHS
  int rhs_index = -1; // storing for later use

  // The LHS begins after the binary operation and goes until a '%' is found
  // it must contain at least one symbol.
  for(size_t i = 2; i < data.size(); i++)
  {
    char buf = data[i];
    if(buf == '%')
    {
      rhs_index = i + 1;
      break;
    }
    lhs_names.push_back(convert_char_to_letter(buf));
  }

  if(rhs_index == -1)
    return false; // The expression must contain '%'

  if(lhs_names.size() == 0)
    return false;

  // Parse RHS

  // For RHS we only need to parse the rest of the string.
  for(size_t i = rhs_index; i < data.size(); i++)
  {
    char buf = data[i];
    rhs_names.push_back(convert_char_to_letter(buf));
  }

  return rhs_names.size() != 0;
}
