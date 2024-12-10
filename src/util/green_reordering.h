#pragma once
#include <util/algorithms.h>

/**
 *  This class will reorder a expression based on lexical order,
 *  the idea is that a expression in the format:
 *
 *  c + a + b + 4 + d == b + a
 *
 *  Becomes:
 *
 *  4 + a + b + c + d == a + b
 *
 *  Note that that this assumes that a constant propagation algorithm
 *  was executed, so an expression should have at max 1 constant value.
 *
 *  Supported Operators: [+, *]
 *  Supported Types (constants/symbols): [Signed BV, Unsigned BV]
 *  Supported Relations: [==, !=, <, >, <=, >=]
 *
 */
class expr_variable_reordering : public expr2tc_algorithm
{
  typedef std::vector<symbol2tc> symbols_vec;
  typedef std::vector<constant_int2tc> values_vec;

public:
  explicit expr_variable_reordering() : expr2tc_algorithm(true)
  {
  }

  bool run(expr2tc &) override;

private:
  /**
   * Parse a binary operation and reorders it
   *
   * Since some operators have precedence this implementation
   * should take it into account.
   *
   * The reorder will check if LHS and RHS are symbols/values
   * the precedence will be: x OP y OP z OP value
   *
   *
   * @param expr binary_operation
   */
  void run_on_binop(expr2tc &expr);

  /**
   * Parse a relation by reordering the LHS and RHS
   *
   * @param expr relation
   */
  void run_on_relation(expr2tc &expr);

  /**
   * Parse the inner contents of a negation
   *
   * @param expr relation
   */
  void run_on_negation(expr2tc &expr);

  /**
   * Parses and adds the symbol or value
   *
   * This receives a reference and generates a copy of it to be later used
   *
   * @param op parent of the operation
   * @param is_lhs truth value to determine which side of the op will be checked
   * @param symbols
   * @param values
   */
  static void add_value(
    const std::shared_ptr<arith_2ops> op,
    bool is_lhs,
    symbols_vec &symbols,
    values_vec &values);

  /**
   * Parses and replaces symbols and values
   *
   * This works by tacking an expression and manually replacing its inner
   * expressions with the ordered symbols and values
   *
   * Example:
   *
   * op: (ADD ("b") ( (ADD ("7") ("a") ) -------> b + (7 + a)
   * symbols: ["a", "b"]
   * values: ["7"]
   *
   * The expression would be parsed/replaced in the following order:
   *
   * (ADD (1) ( (ADD (2) (3)) )
   *
   * 1 -> "a"
   * 2 -> "b"
   * 3 -> "7"
   *
   * @param op parent of the operation
   * @param is_lhs truth value to determine which side of the op will be checked
   * @param symbols
   * @param values
   */
  static void replace_value(
    const std::shared_ptr<arith_2ops> op,
    bool is_lhs,
    symbols_vec &symbols,
    values_vec &values);

  enum class TRANSVERSE_MODE
  {
    READ,    /// to extract the expressions
    REPLACE, /// to replace the expressions
  };

  /**
   * @brief Helper method to be used internally with transverse_binop
   *
   * @param op binaop arith
   * @param symbols symbolic variable list
   * @param values constants list
   * @param mode reading/replacing
   * @param is_lhs which side of binary operation to be checked
   */
  void parse_arith_side(
    std::shared_ptr<arith_2ops> op,
    symbols_vec &symbols,
    values_vec &values,
    TRANSVERSE_MODE mode,
    bool is_lhs);

  /**
   * Default strategy to execute an algorithm while transversing the expr
   * @param op binary operation
   * @param symbols vector of all symbols expressions
   * @param values vector of all values expressions
   */
  void transverse_binop(
    std::shared_ptr<arith_2ops> op,
    symbols_vec &symbols,
    values_vec &values,
    TRANSVERSE_MODE mode);

  /**
   * Recursively extracts all symbols and values of a binary operation
   *
   * The logic is that an expr is like a lisp:
   *
   *  (BINOP (BINOP (x) (y)) (7))
   *
   *  So the output would be:
   *
   *  symbols = [x,y], values = [7]
   *
   *
   * @param op binary operation
   * @param symbols vector of all symbols expressions
   * @param values vector of all values expressions
   */
  void transverse_read_binop(
    std::shared_ptr<arith_2ops> op,
    symbols_vec &symbols,
    values_vec &values);

  /**
   * Transverse the expression replacing its inner symbols and values
   * in alphabetical order
   *
   * This assumes that symbols is ordered and values has at most 1 element
   *
   * @param op binary operation
   * @param symbols vector of all symbols expressions
   * @param values vector of all values expressions
   */
  void transverse_replace_binop(
    std::shared_ptr<arith_2ops> op,
    symbols_vec &symbols,
    values_vec &values);
};
