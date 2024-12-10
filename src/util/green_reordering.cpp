#include <util/green_reordering.h>
#include <array>

bool expr_variable_reordering::run(expr2tc &e)
{
  switch(e->expr_id)
  {
    // BINARY OPS
  case expr2t::expr_ids::add_id:
  case expr2t::expr_ids::mul_id:
    run_on_binop(e);
    break;
    // RELATIONS
  case expr2t::expr_ids::equality_id:
  case expr2t::expr_ids::notequal_id:
  case expr2t::expr_ids::lessthan_id:
  case expr2t::expr_ids::greaterthan_id:
  case expr2t::expr_ids::lessthanequal_id:
  case expr2t::expr_ids::greaterthanequal_id:
    run_on_relation(e);
    break;
    // Negations
  case expr2t::expr_ids::neg_id:
  case expr2t::expr_ids::not_id:
    run_on_negation(e);
    break;
  default:
    break; // don't care
  }
  return true;
}

void expr_variable_reordering::run_on_binop(expr2tc &expr)
{
  std::shared_ptr<arith_2ops> arith;
  arith = std::dynamic_pointer_cast<arith_2ops>(expr);
  // If arith is false then the cast failed
  assert(arith);

  /** Reorder both sides recursively
   *
   * The idea of this algorithm is to parse all symbols and values of a
   * expression with the same operator and rewrite it.
   *
   * 1. Create a list of symbols and values
   * 2. Order it in crescent order
   * 3. Change all values
   */

  run(arith->side_1);
  run(arith->side_2);

  symbols_vec symbols; // annotates all inner symbols
  values_vec values;   // annotates all inner values

  // 1. Create a list of symbols and values
  this->transverse_read_binop(arith, symbols, values);

  // 2. Order it in crescent order
  sort(symbols.begin(), symbols.end(), [](const auto &lhs, const auto &rhs) {
    return lhs->get_symbol_name() > rhs->get_symbol_name();
  });

  // A constant propagation should have been executed prior to this
  assert(values.size() <= 1);

  // 3. Change all values
  this->transverse_replace_binop(arith, symbols, values);
}

void expr_variable_reordering::run_on_negation(expr2tc &expr)
{
  if(expr->expr_id == expr2t::expr_ids::neg_id)
  {
    std::shared_ptr<arith_1op> arith;
    arith = std::dynamic_pointer_cast<arith_1op>(expr);
    // If arith is false then the cast failed
    assert(arith);
    run(arith->value);
  }
  else if(expr->expr_id == expr2t::expr_ids::not_id)
  {
    std::shared_ptr<bool_1op> arith;
    arith = std::dynamic_pointer_cast<bool_1op>(expr);
    // If arith is false then the cast failed
    assert(arith);
    run(arith->value);
  }
}

void expr_variable_reordering::run_on_relation(expr2tc &expr)
{
  std::shared_ptr<relation_data> relation;
  relation = std::dynamic_pointer_cast<relation_data>(expr);

  // If relation is false then the cast failed
  assert(relation);

  // Reorder both sides
  run(relation->side_1);
  run(relation->side_2);
}

inline void expr_variable_reordering::transverse_read_binop(
  const std::shared_ptr<arith_2ops> op,
  expr_variable_reordering::symbols_vec &symbols,
  expr_variable_reordering::values_vec &values)
{
  transverse_binop(op, symbols, values, TRANSVERSE_MODE::READ);
}

inline void expr_variable_reordering::transverse_replace_binop(
  const std::shared_ptr<arith_2ops> op,
  expr_variable_reordering::symbols_vec &symbols,
  expr_variable_reordering::values_vec &values)
{
  transverse_binop(op, symbols, values, TRANSVERSE_MODE::REPLACE);
}

void expr_variable_reordering::parse_arith_side(
  const std::shared_ptr<arith_2ops> op,
  expr_variable_reordering::symbols_vec &symbols,
  expr_variable_reordering::values_vec &values,
  expr_variable_reordering::TRANSVERSE_MODE mode,
  bool is_lhs)
{
  bool side_is_same_operand = is_lhs ? op->side_1->expr_id == op->expr_id
                                     : op->side_2->expr_id == op->expr_id;
  // Check if LHS is the same binary operation of parent
  if(side_is_same_operand)
  {
    std::shared_ptr<arith_2ops> arith;
    arith =
      std::dynamic_pointer_cast<arith_2ops>(is_lhs ? op->side_1 : op->side_2);
    transverse_binop(arith, symbols, values, mode);
  }

  switch(mode)
  {
  case TRANSVERSE_MODE::READ:
  {
    add_value(op, is_lhs, symbols, values);
    break;
  }
  case TRANSVERSE_MODE::REPLACE:
  {
    replace_value(op, is_lhs, symbols, values);
    break;
  }
  }
}

void expr_variable_reordering::transverse_binop(
  const std::shared_ptr<arith_2ops> op,
  expr_variable_reordering::symbols_vec &symbols,
  expr_variable_reordering::values_vec &values,
  expr_variable_reordering::TRANSVERSE_MODE mode)
{
  parse_arith_side(op, symbols, values, mode, true);  // LHS
  parse_arith_side(op, symbols, values, mode, false); // RHS
}

void expr_variable_reordering::add_value(
  const std::shared_ptr<arith_2ops> op,
  bool is_lhs,
  expr_variable_reordering::symbols_vec &symbols,
  expr_variable_reordering::values_vec &values)
{
  auto side_expr = is_lhs ? op->side_1 : op->side_2;
  switch(side_expr->expr_id)
  {
  case expr2t::expr_ids::symbol_id:
  {
    symbol2tc symbol;
    symbol = side_expr;
    symbols.push_back(symbol);
    break;
  }
  case expr2t::expr_ids::constant_int_id:
  {
    constant_int2tc value;
    value = side_expr;
    values.push_back(value);
    break;
  }
  default:; // Continue parsing without adding anything
  }
}

void expr_variable_reordering::replace_value(
  const std::shared_ptr<arith_2ops> op,
  bool is_lhs,
  symbols_vec &symbols,
  values_vec &values)
{
  auto side_expr = is_lhs ? op->side_1 : op->side_2;
  bool should_change = side_expr->expr_id == expr2t::expr_ids::symbol_id ||
                       side_expr->expr_id == expr2t::expr_ids::constant_int_id;
  if(should_change)
  {
    expr2tc to_add;
    if(!values.empty())
    {
      to_add = values.back();
      values.pop_back();
    }
    else
    {
      to_add = symbols.back();
      symbols.pop_back();
    }
    if(is_lhs)
      op->side_1 = to_add;
    else
      op->side_2 = to_add;
  }
}
