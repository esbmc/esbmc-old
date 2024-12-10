#pragma once

#include <utility>

#include "cache_test_utils.h"

/**
 * Helper class to be used in fuzzing targets,
 * this will help create valid expr to validate the algorithms
 *
 * The main class will work by receiving a string and parsing it as follows:
 *
 * RELATION BINOP [SYMBOLS] % SYMBOLS
 *
 * 00abc%a => a + b + c == a => (EQUAL (ADD (A) (ADD) (B) (C))) (A))
 *
 */
class expr_generator_fuzzer
{
public:
  expr_generator_fuzzer(std::string input) : data(std::move(input))
  {
    is_valid = is_valid_input();
  }

  bool is_expr_valid() const
  {
    return is_valid;
  }

  const std::vector<char> get_lhs_names()
  {
    return lhs_names;
  }
  const std::vector<char> get_rhs_names()
  {
    return rhs_names;
  }

protected:
  /**
   * Process data and validates if it is in the right format
   * @return if the input is valid
   */
  bool is_valid_input();

private:
  const std::string data;      /// Contains the input from fuzzer
  bool is_valid;               /// Saves if the input is valid and can be used
  std::vector<char> lhs_names; // Stores LHS side of the input
  std::vector<char> rhs_names; // Stores RHS side of the input

  /**
   * Used for relation, the value from fuzzer mod 6 will be used
   */
  enum class relation
  {
    EQUAL,
    UNEQUAL,
    LESSER,
    LESSER_EQUAL,
    GREATER,
    GREATER_EQUAL
  };

  /**
   * To be used for relation, the value from fuzzer mod 3 will used
   */
  enum class binary_operation
  {
    ADD,
    SUB,
    MUL
  };

  relation expr_relation = relation::EQUAL;
  binary_operation expr_binop = binary_operation::ADD;

  static inline relation relation_from_unsigned(unsigned)
  {
    // TODO: support all relations
    return relation::EQUAL;
  }
  static inline binary_operation binop_from_unsigned(unsigned)
  {
    // TODO: support all binary operations
    return binary_operation::ADD;
  }

  /**
   * Flatten lhs of rhs applying the expr_binop and expr_relation
   * @param lhs of relation with binop
   * @param rhs of relation with binop
   * @return relation expression
   *
   * @pre @lhs and @rhs are not empty
   */
  expr2tc
  convert_vectors_to_expression(std::vector<char> lhs, std::vector<char> rhs);

  /**
   * Flatten a char vector into an expressions using a binary operator
   * @param entries
   * @param binop
   * @return bionop expression
   *
   * @pre @entries is not empty
   */
  static expr2tc convert_char_vector_to_expression(
    std::vector<char> entries,
    binary_operation binop);

public:
  /**
   * Parses data and convert it to an expr2tc
   *
   * @return the expression generated
   *
   * @pre @this->data is a valid input
   */
  expr2tc convert_input_to_expression();

  /**
   * Parses data and convert it to an expr2tc
   * @param expr_relation
   * @param binop
   * @param lhs
   * @param rhs
   * @return the expression generated
   *
   * @pre @lhs and @rhs are not empty
   */
  virtual expr2tc get_correct_expression(
    relation expr_relation,
    binary_operation binop,
    std::vector<char> lhs,
    std::vector<char> rhs);

  relation get_relation() const
  {
    return expr_relation;
  }
  binary_operation get_binop() const
  {
    return expr_binop;
  }
};
