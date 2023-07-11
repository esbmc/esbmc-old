#ifndef UTIL_IREP2_UTILS_H_
#define UTIL_IREP2_UTILS_H_

#include <util/c_types.h>

#include <irep2/irep2_expr.h>
#include <util/migrate.h>
#include <util/message.h>

std::string indent_str_irep2(unsigned int indent);

// Map a base type to it's list of names
template <typename T>
class base_to_names;

template <class T>
std::string pretty_print_func(unsigned int indent, std::string ident, T obj)
{
  list_of_memberst memb = obj.tostring(indent + 2);

  std::string indentstr = indent_str_irep2(indent);
  std::string exprstr = std::move(ident);

  for(list_of_memberst::const_iterator it = memb.begin(); it != memb.end();
      it++)
  {
    exprstr += "\n" + indentstr + "* " + it->first + " : " + it->second;
  }

  return exprstr;
}

/** Test whether type is an integer. */
inline bool is_bv_type(const type2tc &t)
{
  return t->type_id == type2t::unsignedbv_id ||
         t->type_id == type2t::signedbv_id;
}

inline bool is_bv_type(const expr2tc &e)
{
  return is_bv_type(e->type);
}

/** Test whether type is a float/double. */
inline bool is_fractional_type(const type2tc &t)
{
  return t->type_id == type2t::fixedbv_id || t->type_id == type2t::floatbv_id;
}

inline bool is_fractional_type(const expr2tc &e)
{
  return is_bv_type(e->type);
}

/** Test whether type is a number type - bv, fixedbv or floatbv. */
inline bool is_number_type(const type2tc &t)
{
  return t->type_id == type2t::unsignedbv_id ||
         t->type_id == type2t::signedbv_id ||
         t->type_id == type2t::fixedbv_id || t->type_id == type2t::floatbv_id ||
         t->type_id == type2t::bool_id;
}

inline bool is_number_type(const expr2tc &e)
{
  return is_number_type(e->type);
}

inline bool is_scalar_type(const type2tc &t)
{
  return is_number_type(t) || is_pointer_type(t) || is_empty_type(t) ||
         is_code_type(t);
}

inline bool is_scalar_type(const expr2tc &e)
{
  return is_scalar_type(e->type);
}

inline bool is_multi_dimensional_array(const type2tc &t)
{
  if(is_array_type(t))
    return is_array_type(to_array_type(t).subtype);

  return false;
}

inline bool is_multi_dimensional_array(const expr2tc &e)
{
  return is_multi_dimensional_array(e->type);
}

inline bool is_constant_number(const expr2tc &t)
{
  return t->expr_id == expr2t::constant_int_id ||
         t->expr_id == expr2t::constant_fixedbv_id ||
         t->expr_id == expr2t::constant_floatbv_id ||
         t->expr_id == expr2t::constant_bool_id;
}

inline bool is_constant_expr(const expr2tc &t)
{
  return is_constant_number(t) || t->expr_id == expr2t::constant_string_id ||
         t->expr_id == expr2t::constant_struct_id ||
         t->expr_id == expr2t::constant_union_id ||
         t->expr_id == expr2t::constant_array_id ||
         t->expr_id == expr2t::constant_array_of_id ||
         t->expr_id == expr2t::constant_vector_id;
}

inline bool is_constant(const expr2tc &t)
{
  if(is_pointer_type(t) && is_symbol2t(t))
  {
    symbol2t s = to_symbol2t(t);
    if(s.thename == "NULL")
      return true;
  }
  return is_constant_expr(t);
}

inline bool is_structure_type(const type2tc &t)
{
  return t->type_id == type2t::struct_id || t->type_id == type2t::union_id;
}

inline bool is_structure_type(const expr2tc &e)
{
  return is_structure_type(e->type);
}

inline bool is_arith_expr(const expr2tc &expr)
{
  return expr->expr_id == expr2t::neg_id || expr->expr_id == expr2t::abs_id ||
         expr->expr_id == expr2t::add_id || expr->expr_id == expr2t::sub_id ||
         expr->expr_id == expr2t::mul_id || expr->expr_id == expr2t::div_id ||
         expr->expr_id == expr2t::modulus_id;
}

inline bool is_comp_expr(const expr2tc &expr)
{
  return expr->expr_id == expr2t::lessthan_id ||
         expr->expr_id == expr2t::lessthanequal_id ||
         expr->expr_id == expr2t::greaterthan_id ||
         expr->expr_id == expr2t::greaterthanequal_id ||
         expr->expr_id == expr2t::equality_id ||
         expr->expr_id == expr2t::notequal_id;
}

/** Test if expr is true. First checks whether the expr is a constant bool, and
 *  then whether it's true-valued. If these are both true, return true,
 *  otherwise return false.
 *  @param expr Expression to check for true value.
 *  @return Whether or not expr is true-valued.
 */
inline bool is_true(const expr2tc &expr)
{
  if(is_constant_bool2t(expr) && to_constant_bool2t(expr).value)
    return true;

  if(is_constant_int2t(expr) && !to_constant_int2t(expr).value.is_zero())
    return true;

  if(
    is_constant_floatbv2t(expr) && !to_constant_floatbv2t(expr).value.is_zero())
    return true;

  if(
    is_constant_fixedbv2t(expr) && !to_constant_fixedbv2t(expr).value.is_zero())
    return true;

  return false;
}

/** Test if expr is false. First checks whether the expr is a constant bool, and
 *  then whether it's false-valued. If these are both true, return true,
 *  otherwise return false.
 *  @param expr Expression to check for false value.
 *  @return Whether or not expr is true-valued.
 */
inline bool is_false(const expr2tc &expr)
{
  if(is_constant_bool2t(expr) && !to_constant_bool2t(expr).value)
    return true;

  if(is_constant_int2t(expr) && to_constant_int2t(expr).value.is_zero())
    return true;

  if(is_constant_floatbv2t(expr) && to_constant_floatbv2t(expr).value.is_zero())
    return true;

  if(is_constant_fixedbv2t(expr) && to_constant_fixedbv2t(expr).value.is_zero())
    return true;

  return false;
}

inline expr2tc gen_true_expr()
{
  static constant_bool2tc c(true);
  return c;
}

inline expr2tc gen_false_expr()
{
  static constant_bool2tc c(false);
  return c;
}

inline expr2tc gen_long(const type2tc &type, long val)
{
  return constant_int2tc(type, BigInt(val));
}

inline expr2tc gen_ulong(BigInt v)
{
  return constant_int2tc(get_uint_type(config.ansi_c.word_size), std::move(v));
}

inline expr2tc gen_ulong(unsigned long val)
{
  return gen_ulong(BigInt(val));
}

inline expr2tc gen_slong(signed long val)
{
  constant_int2tc v(get_int_type(config.ansi_c.word_size), BigInt(val));
  return v;
}

inline const type2tc &get_array_subtype(const type2tc &type)
{
  return to_array_type(type).subtype;
}

inline const type2tc &get_vector_subtype(const type2tc &type)
{
  return to_vector_type(type).subtype;
}

inline const type2tc &get_base_array_subtype(const type2tc &type)
{
  const auto &subtype = to_array_type(type).subtype;
  if(is_array_type(subtype))
    return get_base_array_subtype(subtype);

  return subtype;
}

inline bool simplify(expr2tc &expr)
{
  if(is_nil_expr)
    return false;

  expr2tc tmp = expr->simplify();
  if(!is_nil_expr(tmp))
  {
    expr = tmp;
    return true;
  }

  return false;
}

inline void make_not(expr2tc &expr)
{
  if(is_true(expr))
  {
    expr = gen_false_expr();
    return;
  }

  if(is_false(expr))
  {
    expr = gen_true_expr();
    return;
  }

  expr2tc new_expr;
  if(is_not2t(expr))
    new_expr = to_not2t(expr).value;
  else
    new_expr = not2tc(expr);

  expr.swap(new_expr);
}

inline expr2tc conjunction(std::vector<expr2tc> cs)
{
  if(cs.empty())
    return gen_true_expr();

  expr2tc res = cs[0];
  for(std::size_t i = 1; i < cs.size(); ++i)
    res = and2tc(res, cs[i]);

  return res;
}

inline expr2tc disjunction(std::vector<expr2tc> cs)
{
  if(cs.empty())
    return gen_true_expr();

  expr2tc res = cs[0];
  for(std::size_t i = 1; i < cs.size(); ++i)
    res = or2tc(res, cs[i]);

  return res;
}

inline expr2tc gen_nondet(const type2tc &type)
{
  return sideeffect2tc(
    type,
    expr2tc(),
    expr2tc(),
    std::vector<expr2tc>(),
    type2tc(),
    sideeffect2t::nondet);
}

inline expr2tc gen_zero(const type2tc &type, bool array_as_array_of = false)
{
  switch(type->type_id)
  {
  case type2t::bool_id:
    return gen_false_expr();

  case type2t::unsignedbv_id:
  case type2t::signedbv_id:
    return constant_int2tc(type, BigInt(0));

  case type2t::fixedbv_id:
    return constant_fixedbv2tc(fixedbvt(fixedbv_spect(to_fixedbv_type(type))));

  case type2t::floatbv_id:
    return constant_floatbv2tc(
      ieee_floatt(ieee_float_spect(to_floatbv_type(type))));

  case type2t::vector_id:
  {
    auto vec_type = to_vector_type(type);
    assert(is_constant_int2t(vec_type.array_size));
    auto s = to_constant_int2t(vec_type.array_size);

    std::vector<expr2tc> members;
    for(long int i = 0; i < s.as_long(); i++)
      members.push_back(
        gen_zero(to_vector_type(type).subtype, array_as_array_of));

    return constant_vector2tc(type, members);
  }
  case type2t::array_id:
  {
    if(array_as_array_of)
      return constant_array_of2tc(type, gen_zero(to_array_type(type).subtype));

    auto arr_type = to_array_type(type);

    assert(is_constant_int2t(arr_type.array_size));
    auto s = to_constant_int2t(arr_type.array_size);

    std::vector<expr2tc> members;
    for(long int i = 0; i < s.as_long(); i++)
      members.push_back(
        gen_zero(to_array_type(type).subtype, array_as_array_of));

    return constant_array2tc(type, members);
  }

  case type2t::pointer_id:
    return symbol2tc(type, "NULL");

  case type2t::struct_id:
  {
    auto struct_type = to_struct_type(type);

    std::vector<expr2tc> members;
    for(auto const &member_type : struct_type.members)
      members.push_back(gen_zero(member_type, array_as_array_of));

    return constant_struct2tc(type, members);
  }

  case type2t::union_id:
  {
    auto union_type = to_union_type(type);

    assert(!union_type.members.empty());
    std::vector<expr2tc> members = {
      gen_zero(union_type.members.front(), array_as_array_of)};

    return constant_union2tc(type, union_type.member_names.front(), members);
  }

  default:
    break;
  }

  log_error("Can't generate zero for type {}", get_type_id(type));
  abort();
}

inline expr2tc gen_one(const type2tc &type)
{
  switch(type->type_id)
  {
  case type2t::bool_id:
    return gen_true_expr();

  case type2t::unsignedbv_id:
  case type2t::signedbv_id:
    return constant_int2tc(type, BigInt(1));

  case type2t::fixedbv_id:
  {
    fixedbvt f(fixedbv_spect(to_fixedbv_type(type)));
    f.from_integer(BigInt(1));
    return constant_fixedbv2tc(f);
  }

  case type2t::floatbv_id:
  {
    ieee_floatt f(ieee_float_spect(to_floatbv_type(type)));
    f.from_integer(BigInt(1));
    return constant_floatbv2tc(f);
  }

  default:
    break;
  }

  log_error("Can't generate one for type {}", get_type_id(type));
  abort();
}

/**
   * @brief Distribute the functor `func` over op1 and op2
   * at least one of those must be a vector
   * 
   * Here, if one of the operands is `nil` then func must
   * support an operation between the vector subtype and nil
   *
   * @param func the functor operation e.g add, sub, mul
   * @param op1 the first operand
   * @param op2 the second operand
   * @return expr2tc with the resulting vector
   */
template <typename Func>
inline expr2tc distribute_vector_operation(Func func, expr2tc op1, expr2tc op2)
{
  assert(is_constant_vector2t(op1) || is_constant_vector2t(op2));
  /*
   * If both op1 and op2 are vectors the resulting value
   * would be the operation over each member
   *
   * Example:
   *
   * op1 = {1,2,3,4}
   * op2 = {1,1,1,1}
   * func = add
   *
   * This would result in:
   *
   * { add(op1[0], op2[0]), add(op1[1], op2[1]), ...}
   * {2,3,4,5}
   */
  if(is_constant_vector2t(op1) && is_constant_vector2t(op2))
  {
    constant_vector2tc vec1(op1);
    constant_vector2tc vec2(op2);
    for(size_t i = 0; i < vec1->datatype_members.size(); i++)
    {
      auto &A = vec1->datatype_members[i];
      auto &B = vec2->datatype_members[i];
      auto new_op = func(A->type, A, B);
      vec1->datatype_members[i] = new_op;
    }
    return vec1;
  }
  /*
   * If only one of the operator is a vector, then the result
   * would extract each value of the vector and apply the value to
   * the other operator
   *
   * Example:
   *
   * op1 = {1,2,3,4}
   * op2 = 1
   * func = add
   *
   * This would result in:
   *
   * { add(op1[0], 1), add(op1[1], 1), ...}
   * {2,3,4,5}
   */
  else
  {
    bool is_op1_vec = is_constant_vector2t(op1);
    expr2tc c = !is_op1_vec ? op1 : op2;
    constant_vector2tc vector(is_op1_vec ? op1 : op2);
    for(auto &datatype_member : vector->datatype_members)
    {
      auto &op = datatype_member;
      auto e1 = is_op1_vec ? op : c;
      auto e2 = is_op1_vec ? c : op;
      auto new_op = func(op->type, e1, e2);
      datatype_member = new_op->do_simplify();
    }
    return vector;
  }
}

/**
   * @brief Distribute a function between one or two operands,
   * at least one of those must be a vector
   *
   * @param id the id for the operation
   * @param op1 the first operand
   * @param op2 the second operand
   * @param rm rounding mode (for ieee)
   * @return expr2tc with the resulting vector
   */
inline expr2tc distribute_vector_operation(
  expr2t::expr_ids id,
  expr2tc op1,
  expr2tc op2 = expr2tc(),
  expr2tc rm = expr2tc())
{
#ifndef NDEBUG
  assert(is_vector_type(op1) || (op2 && is_vector_type(op2)));
#endif
  auto is_op1_vector = is_vector_type(op1);
  auto vector_length = is_op1_vector ? to_vector_type(op1->type).array_size
                                     : to_vector_type(op2->type).array_size;
  assert(is_constant_int2t(vector_length));

  auto vector_subtype = is_op1_vector ? to_vector_type(op1->type).subtype
                                      : to_vector_type(op2->type).subtype;
  auto result = is_op1_vector ? gen_zero(op1->type) : gen_zero(op2->type);

  /*
   * If both op1 and op2 are vectors the resulting value
   * would be the operation over each member
   *
   * Example:
   *
   * op1 = {1,2,3,4}
   * op2 = {1,1,1,1}
   * func = add
   *
   * This would result in:
   *
   * { add(op1[0], op2[0]), add(op1[1], op2[1]), ...}
   * {2,3,4,5}
   */

  /*
   * If only one of the operator is a vector, then the result
   * would extract each value of the vector and apply the value to
   * the other operator
   *
   * Example:
   *
   * op1 = {1,2,3,4}
   * op2 = 1
   * func = add
   *
   * This would result in:
   *
   * { add(op1[0], 1), add(op1[1], 1), ...}
   * {2,3,4,5}
   */
  for(size_t i = 0; i < to_constant_int2t(vector_length).as_ulong(); i++)
  {
    BigInt position(i);
    type2tc vector_type =
      to_vector_type(is_vector_type(op1->type) ? op1->type : op2->type).subtype;
    expr2tc local_op1 = op1;
    if(is_vector_type(op1->type))
    {
      local_op1 = index2tc(
        to_vector_type(op1->type).subtype,
        op1,
        constant_int2tc(get_uint32_type(), position));
    }

    expr2tc local_op2 = op2;
    if(op2 && is_vector_type(op2->type))
    {
      local_op2 = index2tc(
        to_vector_type(op2->type).subtype,
        op2,
        constant_int2tc(get_uint32_type(), position));
    }

    expr2tc to_add;
    switch(id)
    {
    case expr2t::neg_id:
      to_add = neg2tc(vector_type, local_op1);
      break;
    case expr2t::bitnot_id:
      to_add = bitnot2tc(vector_type, local_op1);
      break;
    case expr2t::sub_id:
      to_add = sub2tc(vector_type, local_op1, local_op2);
      break;
    case expr2t::mul_id:
      to_add = mul2tc(vector_type, local_op1, local_op2);
      break;
    case expr2t::div_id:
      to_add = div2tc(vector_type, local_op1, local_op2);
      break;
    case expr2t::modulus_id:
      to_add = modulus2tc(vector_type, local_op1, local_op2);
      break;
    case expr2t::add_id:
      to_add = add2tc(vector_type, local_op1, local_op2);
      break;
    case expr2t::shl_id:
      to_add = shl2tc(vector_type, local_op1, local_op2);
      break;
    case expr2t::bitxor_id:
      to_add = bitxor2tc(vector_type, local_op1, local_op2);
      break;
    case expr2t::bitor_id:
      to_add = bitor2tc(vector_type, local_op1, local_op2);
      break;
    case expr2t::bitand_id:
      to_add = bitand2tc(vector_type, local_op1, local_op2);
      break;
    case expr2t::ieee_add_id:
      to_add = ieee_add2tc(vector_type, local_op1, local_op2, rm);
      break;
    case expr2t::ieee_div_id:
      to_add = ieee_div2tc(vector_type, local_op1, local_op2, rm);
      break;
    case expr2t::ieee_sub_id:
      to_add = ieee_sub2tc(vector_type, local_op1, local_op2, rm);
      break;
    case expr2t::ieee_mul_id:
      to_add = ieee_mul2tc(vector_type, local_op1, local_op2, rm);
      break;
    default:
      assert(0 && "Unsupported operation for Vector");
      abort();
    }
    to_constant_vector2t(result).datatype_members[i] = to_add;
  }
  return result;
}

#endif /* UTIL_IREP2_UTILS_H_ */
