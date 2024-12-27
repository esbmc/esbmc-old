#include <algorithm>
#include <cassert>
#include <util/arith_tools.h>
#include <util/bitvector.h>
#include <util/config.h>
#include <util/expr_util.h>
#include <util/fixedbv.h>
#include <util/ieee_float.h>
#include <util/mp_arith.h>
#include <util/options.h>
#include <util/simplify_expr.h>
#include <util/simplify_expr_class.h>
#include <util/simplify_utils.h>
#include <util/std_expr.h>
#include <util/std_types.h>

//#define USE_CACHE

#ifdef USE_CACHE
struct simplify_expr_cachet
{
public:
  friend class simplify_exprt;

#  if 1
  typedef std::unordered_map<exprt, exprt, irep_full_hash, irep_full_eq>
    containert;
#  else
  typedef std::unordered_map<exprt, exprt, irep_hash> containert;
#  endif

  containert container_normal, container_no_simpl_const_objects;

  containert &container(bool simpl_const_objects)
  {
    return simpl_const_objects ? container_normal
                               : container_no_simpl_const_objects;
  }
};

simplify_expr_cachet simplify_expr_cache;
#endif

bool simplify_exprt::simplify_typecast(exprt &expr, bool simpl_const_objects)
{
  if (expr.operands().size() != 1)
    return true;

  // eliminate redundant typecasts
  if (expr.type() == expr.op0().type())
  {
    exprt tmp;
    tmp.swap(expr.op0());
    expr.swap(tmp);
    return false;
  }

  // elminiate casts to bool
  if (expr.type() == bool_typet())
  {
    equality_exprt equality;
    equality.location() = expr.location();
    equality.lhs() = expr.op0();
    equality.rhs() = gen_zero(expr.op0().type());
    simplify_node(equality, simpl_const_objects);
    equality.make_not();
    simplify_node(equality, simpl_const_objects);
    expr.swap(equality);
    return false;
  }

  // eliminate duplicate pointer typecasts
  if (
    expr.type().id() == "pointer" && expr.op0().id() == "typecast" &&
    expr.op0().type().id() == "pointer" && expr.op0().operands().size() == 1)
  {
    exprt tmp;
    tmp.swap(expr.op0().op0());
    expr.op0().swap(tmp);
    // recursive call
    simplify_node(expr, simpl_const_objects);
    return false;
  }

  const irep_idt &expr_type_id = expr.type().id();
  const exprt &operand = expr.op0();
  const irep_idt &op_type_id = operand.type().id();

  unsigned expr_width = bv_width(expr.type());
  unsigned op_width = bv_width(operand.type());

  if (operand.is_constant())
  {
    const irep_idt &value = operand.value();

    exprt new_expr("constant", expr.type());

    if (op_type_id == "c_enum" || op_type_id == "incomplete_c_enum")
    {
      BigInt int_value = string2integer(id2string(value));

      if (expr_type_id == "bool")
      {
        new_expr.value((int_value != 0) ? "true" : "false");
        expr.swap(new_expr);
        return false;
      }

      if (expr_type_id == "unsignedbv" || expr_type_id == "signedbv")
      {
        new_expr.value(integer2binary(int_value, expr_width));
        expr.swap(new_expr);
        return false;
      }

      if (expr_type_id == "c_enum" || expr_type_id == "incomplete_c_enum")
      {
        new_expr.value(integer2string(int_value));
        expr.swap(new_expr);
        return false;
      }
    }
    else if (op_type_id == "bool")
    {
      if (expr_type_id == "unsignedbv" || expr_type_id == "signedbv")
      {
        if (operand.is_true())
        {
          expr = from_integer(1, expr.type());
          return false;
        }
        if (operand.is_false())
        {
          expr = gen_zero(new_expr.type());
          return false;
        }
      }
    }
    else if (op_type_id == "unsignedbv" || op_type_id == "signedbv")
    {
      BigInt int_value =
        binary2integer(id2string(value), op_type_id == "signedbv");

      if (expr_type_id == "bool")
      {
        new_expr.make_bool(int_value != 0);
        expr.swap(new_expr);
        return false;
      }

      if (
        expr_type_id == "unsignedbv" || expr_type_id == "signedbv" ||
        expr_type_id == "bv")
      {
        new_expr.value(integer2binary(int_value, expr_width));
        expr.swap(new_expr);
        return false;
      }

      if (expr_type_id == "c_enum" || expr_type_id == "incomplete_c_enum")
      {
        new_expr.value(integer2string(int_value));
        expr.swap(new_expr);
        return false;
      }

      if (expr_type_id == "fixedbv")
      {
        // int to float
        const fixedbv_typet &f_expr_type = to_fixedbv_type(expr.type());

        fixedbvt f;
        f.spec = f_expr_type;
        f.from_integer(int_value);
        expr = f.to_expr();

        return false;
      }

      if (expr_type_id == "floatbv")
      {
        // int to float
        const floatbv_typet &f_expr_type = to_floatbv_type(expr.type());

        ieee_floatt f;
        f.spec = f_expr_type;
        f.from_integer(int_value);
        expr = f.to_expr();

        return false;
      }
    }
    else if (op_type_id == "fixedbv")
    {
      if (expr_type_id == "unsignedbv" || expr_type_id == "signedbv")
      {
        // cast from float to int
        fixedbvt f(to_constant_expr(expr.op0()));
        expr = from_integer(f.to_integer(), expr.type());
        return false;
      }
      if (expr_type_id == "fixedbv")
      {
        // float to double or double to float
        fixedbvt f(to_constant_expr(expr.op0()));
        f.round(to_fixedbv_type(expr.type()));
        expr = f.to_expr();
        return false;
      }
    }
    else if (op_type_id == "floatbv")
    {
      if (expr_type_id == "unsignedbv" || expr_type_id == "signedbv")
      {
        // cast from float to int
        ieee_floatt f(to_constant_expr(expr.op0()));
        expr = from_integer(f.to_integer(), expr.type());
        return false;
      }
      if (expr_type_id == "floatbv")
      {
        // float to double or double to float
        ieee_floatt f(to_constant_expr(expr.op0()));
        f.change_spec(to_floatbv_type(expr.type()));
        expr = f.to_expr();
        return false;
      }
    }
    else if (op_type_id == "bv")
    {
      if (
        expr_type_id == "unsignedbv" || expr_type_id == "signedbv" ||
        expr_type_id == "floatbv")
      {
        BigInt int_value = binary2integer(id2string(value), false);
        new_expr.value(integer2binary(int_value, expr_width));
        expr.swap(new_expr);
        return false;
      }
    }
  }
  else if (operand.id() == "typecast") // typecast of typecast
  {
    if (
      operand.operands().size() == 1 && op_type_id == expr_type_id &&
      (expr_type_id == "unsignedbv" || expr_type_id == "signedbv") &&
      expr_width <= op_width)
    {
      exprt tmp;
      tmp.swap((irept &)expr.op0().op0());
      expr.op0().swap(tmp);
      return false;
    }
  }

  // propagate type casts into arithmetic operators

  if (
    (op_type_id == "unsignedbv" || op_type_id == "signedbv") &&
    (expr_type_id == "unsignedbv" || expr_type_id == "signedbv") &&
    (operand.id() == "+" || operand.id() == "-" || operand.id() == "unary-" ||
     operand.id() == "*") &&
    expr_width <= op_width)
  {
    exprt new_expr;
    new_expr.swap(expr.op0());
    new_expr.type() = expr.type();

    Forall_operands (it, new_expr)
    {
      it->make_typecast(expr.type());
      simplify_rec(*it, simpl_const_objects); // recursive call
    }

    expr.swap(new_expr);

    return false;
  }

  return true;
}

bool simplify_exprt::simplify_dereference(exprt &expr)
{
  if (expr.operands().size() != 1)
    return true;

  if (expr.op0().type().id() != "pointer")
    return true;

  if (expr.op0().is_address_of())
  {
    if (expr.op0().operands().size() == 1)
    {
      exprt tmp;
      tmp.swap(expr.op0().op0());
      expr.swap(tmp);
      return false;
    }
  }

  return true;
}

bool simplify_exprt::simplify_address_of(exprt &expr)
{
  if (expr.operands().size() != 1)
    return true;

  if (expr.type().id() != "pointer")
    return true;

  if (expr.op0().id() == "NULL-object")
  {
    exprt constant("constant", expr.type());
    constant.value("NULL");
    expr.swap(constant);
    return false;
  }
  if (expr.op0().id() == "index")
  {
    exprt &index_expr = expr.op0();

    if (index_expr.operands().size() == 2)
    {
      if (!index_expr.op1().is_zero())
      {
        // we normalize &a[i] to (&a[0])+i
        exprt offset;
        offset.swap(index_expr.op1());
        index_expr.op1() = gen_zero(offset.type());

        exprt addition("+", expr.type());
        addition.move_to_operands(expr, offset);

        expr.swap(addition);
        return false;
      }
    }
  }

  return true;
}

exprt simplify_exprt::pointer_offset(
  const exprt &expr,
  const typet &type,
  bool simpl_const_objects)
{
  if (expr.id() == "symbol" || expr.id() == "string-constant")
  {
    return gen_zero(type);
  }
  if (expr.id() == "member")
  {
    assert(expr.operands().size() == 1);
    // need to count members here
    return nil_exprt();
  }
  else if (expr.id() == "index")
  {
    const index_exprt &index_expr = to_index_expr(expr);
    const exprt &array = index_expr.array();
    const exprt &index = index_expr.index();

    exprt array_offset = pointer_offset(array, type, simpl_const_objects);
    if (array_offset.is_nil())
      return array_offset;

    // actually would need to multiply here
    exprt result = index;
    if (result.is_nil())
      return result;

    if (result.type() != type)
    {
      result.make_typecast(type);
      simplify_typecast(result, simpl_const_objects);
    }

    return result;
  }
  else
    return nil_exprt();
}

bool simplify_exprt::simplify_pointer_offset(
  exprt &expr,
  bool simpl_const_objects)
{
  if (expr.operands().size() != 1)
    return true;

  exprt &ptr = expr.op0();

  if (ptr.type().id() != "pointer")
    return true;

  if (ptr.is_address_of())
  {
    if (ptr.operands().size() != 1)
      return true;

    exprt &object = ptr.op0();
    exprt tmp = pointer_offset(object, expr.type(), simpl_const_objects);

    if (tmp.is_not_nil())
    {
      expr = tmp;
      return false;
    }
  }
  else if (ptr.id() == "typecast") // pointer typecast
  {
    // need to be careful here
    if (ptr.operands().size() != 1)
      return true;

    // first see if that is zero
    exprt ptr_off("pointer_offset", expr.type());
    ptr_off.copy_to_operands(ptr.op0());
    simplify_node(ptr_off, simpl_const_objects);

    if (ptr_off.is_zero())
    {
      expr = ptr_off;
      return false;
    }
  }
  else if (ptr.id() == "+") // pointer arithmetic
  {
    std::list<exprt> ptr_expr;
    std::list<exprt> int_expr;

    forall_operands (it, ptr)
    {
      if (it->type().id() == "pointer")
        ptr_expr.push_back(*it);
      else if (!it->is_zero())
        int_expr.push_back(*it);
    }

    if (ptr_expr.size() == 1)
    {
      exprt ptr_off("pointer_offset", expr.type());
      ptr_off.copy_to_operands(ptr_expr.front());
      simplify_node(ptr_off, simpl_const_objects);

      if (int_expr.empty())
        expr = ptr_off;
      else
      {
        expr = exprt("+", expr.type());
        expr.reserve_operands(int_expr.size() + 1);

        expr.copy_to_operands(ptr_off);

        for (std::list<exprt>::const_iterator it = int_expr.begin();
             it != int_expr.end();
             it++)
        {
          expr.copy_to_operands(*it);
          if (it->type() != expr.type())
          {
            expr.operands().back().make_typecast(expr.type());
            simplify_node(expr.operands().back(), simpl_const_objects);
          }
        }

        simplify_node(expr, simpl_const_objects);
      }
      return false;
    }
  }

  return true;
}

bool simplify_exprt::simplify_multiplication(exprt &expr)
{
  // check to see if it is a number type
  if (!is_number(expr.type()))
    return true;

  // vector of operands
  exprt::operandst &operands = expr.operands();

  // result of the simplification
  bool result = true;

  // position of the constant
  exprt::operandst::iterator constant;

  // true if we have found a constant
  bool found = false;

  // scan all the operands
  for (exprt::operandst::iterator it = operands.begin(); it != operands.end();)
  {
    // if one of the operands is not a number return
    if (!is_number(it->type()))
      return true;

    // if one of the operands is zero the result is zero
    // note: not true on IEEE floating point arithmetic
    if (it->is_zero())
    {
      expr = gen_zero(expr.type());
      return false;
    }

    // true if the given operand has to be erased
    bool do_erase = false;

    // if this is a constant of the same time as the result
    if (it->is_constant() && it->type() == expr.type())
    {
      if (found)
      {
        // update the constant factor
        if (!constant->mul(*it))
          do_erase = true;
      }
      else
      {
        // set it as the constant factor if this is the first
        constant = it;
        found = true;
      }
    }

    // erase the factor if necessary
    if (do_erase)
    {
      it = operands.erase(it);
      result = false;
    }
    else
      // move to the next operand
      it++;
  }

  if (operands.size() == 1)
  {
    exprt product(operands.front());
    expr.swap(product);

    result = false;
  }
  else
  {
    // if the constant is a one and there are other factors
    if (found && constant->is_one())
    {
      // just delete it
      operands.erase(constant);
      result = false;

      if (operands.size() == 1)
      {
        exprt product(operands.front());
        expr.swap(product);
      }
    }
  }

  return result;
}

bool simplify_exprt::simplify_division(exprt &expr)
{
  if (!is_number(expr.type()))
    return true;

  if (expr.operands().size() != 2)
    return true;

  if (expr.type() != expr.op0().type() || expr.type() != expr.op1().type())
    return true;

  if (expr.type().is_signedbv() || expr.type().is_unsignedbv())
  {
    BigInt int_value0, int_value1;
    bool ok0, ok1;

    ok0 = !to_integer(expr.op0(), int_value0);
    ok1 = !to_integer(expr.op1(), int_value1);

    if (ok1 && int_value1 == 0)
      return true;

    if ((ok1 && int_value1 == 1) || (ok0 && int_value0 == 0))
    {
      exprt tmp;
      tmp.swap(expr.op0());
      expr.swap(tmp);
      return false;
    }

    if (ok0 && ok1)
    {
      BigInt result = int_value0 / int_value1;
      exprt tmp = from_integer(result, expr.type());

      if (tmp.is_not_nil())
      {
        expr.swap(tmp);
        return false;
      }
    }
  }
  else if (expr.type().is_fixedbv())
  {
    // division by one?
    if (expr.op1().is_constant() && expr.op1().is_one())
    {
      exprt tmp;
      tmp.swap(expr.op0());
      expr.swap(tmp);
      return false;
    }

    if (expr.op0().is_constant() && expr.op1().is_constant())
    {
      fixedbvt f0(to_constant_expr(expr.op0()));
      fixedbvt f1(to_constant_expr(expr.op1()));
      if (!f1.is_zero())
      {
        f0 /= f1;
        expr = f0.to_expr();
        return false;
      }
    }
  }
  else if (expr.type().is_floatbv())
  {
    // division by one?
    if (expr.op1().is_constant() && expr.op1().is_one())
    {
      exprt tmp;
      tmp.swap(expr.op0());
      expr.swap(tmp);
      return false;
    }

    if (expr.op0().is_constant() && expr.op1().is_constant())
    {
      ieee_floatt f0(to_constant_expr(expr.op0()));
      ieee_floatt f1(to_constant_expr(expr.op1()));

      if (!f1.is_zero())
      {
        f0 /= f1;
        expr = f0.to_expr();
        return false;
      }
    }
  }
  return true;
}

bool simplify_exprt::simplify_modulo(exprt &expr)
{
  if (!is_number(expr.type()))
    return true;

  if (expr.operands().size() != 2)
    return true;

  if (expr.type().is_signedbv() || expr.type().is_unsignedbv())
  {
    if (expr.type() == expr.op0().type() && expr.type() == expr.op1().type())
    {
      BigInt int_value0, int_value1;
      bool ok0, ok1;

      ok0 = !to_integer(expr.op0(), int_value0);
      ok1 = !to_integer(expr.op1(), int_value1);

      if (ok1 && int_value1 == 0)
        return true; // division by zero

      if ((ok1 && int_value1 == 1) || (ok0 && int_value0 == 0))
      {
        expr = gen_zero(expr.type());
        return false;
      }

      if (ok0 && ok1)
      {
        BigInt result = int_value0 % int_value1;
        exprt tmp = from_integer(result, expr.type());

        if (tmp.is_not_nil())
        {
          expr.swap(tmp);
          return false;
        }
      }
    }
  }

  return true;
}

bool simplify_exprt::simplify_addition_substraction(
  exprt &expr,
  bool simpl_const_objects)
{
  if (!is_number(expr.type()) && expr.type().id() != "pointer")
    return true;

  bool result = true;

  exprt::operandst &operands = expr.operands();
  Forall_operands (it, expr)
  {
    it->remove(irept::a_cformat);
  }

  if (expr.id() == "+")
  {
    exprt::operandst::iterator const_sum;
    bool const_sum_set = false;

    for (exprt::operandst::iterator it = operands.begin();
         it != operands.end();)
    {
      bool do_erase = false;

      if (is_number(it->type()))
      {
        if (it->is_zero())
          do_erase = true;
        else if (it->is_constant())
        {
          if (!const_sum_set)
          {
            const_sum = it;
            const_sum_set = true;
          }
          else
          {
            if (!const_sum->sum(*it))
              do_erase = true;
          }
        }
      }

      if (do_erase)
      {
        it = operands.erase(it);
        result = false;
      }
      else
        it++;
    }

    if (operands.size() == 0)
    {
      expr = gen_zero(expr.type());
      return false;
    }
    if (operands.size() == 1)
    {
      exprt tmp(operands.front());
      expr.swap(tmp);
      return false;
    }
  }
  else if (expr.id() == "-")
  {
    exprt::operandst subtrahends;
    exprt minuend;
    // Sum the subtrahend portions, then if the minuend is constant, attempt to
    // subtract from it.

    exprt::operandst ops = expr.operands();
    if (ops.size() == 0)
      return true;

    assert(ops.size() > 1); // This should probably have become a unary-

    // Remove minuend
    exprt::operandst::iterator it;
    it = ops.begin();
    minuend = *it;
    it++;

    // If this is a binary operation, we might be able to solve right now.
    if (ops.size() == 2)
    {
      if (minuend.id() == "constant" && it->id() == "constant")
      {
        minuend.subtract(*it);
        expr.swap(minuend);
        return false;
      }

      return true;
    }

    // A large subtract; so collect subtrahend portions
    for (; it != ops.end(); it++)
      subtrahends.push_back(*it);

    exprt an_add("+", expr.type());
    an_add.operands() = subtrahends;
    simplify_rec(an_add, simpl_const_objects);

    // We should now have a list of operands, one of which might be constant. If
    // the minuend is constant, and a subtracting operand is constant, perform
    // that subtraction.
    if (minuend.id() == "constant")
    {
      subtrahends = an_add.operands();
      for (it = subtrahends.begin(); it != subtrahends.end(); it++)
      {
        if (it->id() == "constant")
        {
          // Hurrah, we can perform a constant subtraction.
          minuend.subtract(*it);
          subtrahends.erase(it);
          result = false;
        }
      }
    }

    if (subtrahends.size() == 0)
    {
      exprt tmp(minuend);
      expr.swap(tmp);
      return false;
    }

    // Reconstruct a subtract expr
    expr.operands().clear();
    expr.operands().push_back(minuend);
    for (it = subtrahends.begin(); it != subtrahends.end(); it++)
      expr.operands().push_back(*it);
    // result variable will determine whether we've simplified at all.
  }

  return result;
}

bool simplify_exprt::simplify_bitwise(exprt &expr)
{
  if (!is_bitvector_type(expr.type()))
    return true;

  unsigned width = bv_width(expr.type());

  bool result = true;

  while (expr.operands().size() >= 2)
  {
    const irep_idt &a_str = expr.op0().value();
    const irep_idt &b_str = expr.op1().value();

    if (!expr.op0().is_constant())
      break;

    if (!expr.op1().is_constant())
      break;

    if (expr.op0().type() != expr.type())
      break;

    if (expr.op1().type() != expr.type())
      break;

    assert(a_str.size() == b_str.size());

    exprt new_op("constant", expr.type());
    std::string new_value;
    new_value.resize(width);

    if (expr.id() == "bitand")
    {
      for (unsigned i = 0; i < width; i++)
        new_value[i] = (a_str[i] == '1' && b_str[i] == '1') ? '1' : '0';
    }
    else if (expr.id() == "bitor")
    {
      for (unsigned i = 0; i < width; i++)
        new_value[i] = (a_str[i] == '1' || b_str[i] == '1') ? '1' : '0';
    }
    else if (expr.id() == "bitxor")
    {
      for (unsigned i = 0; i < width; i++)
        new_value[i] = ((a_str[i] == '1') != (b_str[i] == '1')) ? '1' : '0';
    }
    else
      break;

    new_op.value(new_value);

    // erase first operand
    expr.operands().erase(expr.operands().begin());
    expr.op0().swap(new_op);

    result = false;
  }

  if (expr.operands().size() == 1)
  {
    exprt tmp;
    tmp.swap(expr.op0());
    expr.swap(tmp);
    return false;
  }

  return result;
}

bool simplify_exprt::simplify_concatenation(exprt &expr)
{
  bool result = true;

  if (is_bitvector_type(expr.type()))
  {
    // first, turn bool into bvec[1]
    for (auto &op : expr.operands())
    {
      if (op.is_true() || op.is_false())
      {
        bool value = op.is_true();
        op = exprt("constant", typet("unsignedbv"));
        op.type().width(1);
        op.value(value ? "1" : "0");
      }
    }

    // search for neighboring constants to merge
    unsigned i = 0;

    while (i < expr.operands().size() - 1)
    {
      exprt &opi = expr.operands()[i];
      exprt &opn = expr.operands()[i + 1];

      if (
        opi.is_constant() && opn.is_constant() &&
        is_bitvector_type(opi.type()) && is_bitvector_type(opn.type()))
      {
        // merge!
        const std::string new_value =
          opi.value().as_string() + opn.value().as_string();
        opi.value(new_value);
        opi.type().width(new_value.size());
        // erase opn
        expr.operands().erase(expr.operands().begin() + i + 1);
        result = true;
      }
      else
        i++;
    }
  }

  // { x } = x
  if (expr.operands().size() == 1)
  {
    exprt tmp;
    tmp.swap(expr.op0());
    expr.swap(tmp);
    result = false;
  }

  return result;
}

bool simplify_exprt::simplify_shifts(exprt &expr)
{
  if (!is_number(expr.type()))
    return true;

  if (expr.operands().size() != 2)
    return true;

  BigInt distance;

  if (to_integer(expr.op1(), distance))
    return true;

  if (distance == 0)
  {
    exprt tmp;
    tmp.swap(expr.op0());
    expr.swap(tmp);
    return false;
  }

  BigInt value;

  if (to_integer(expr.op0(), value))
    return true;

  if (
    expr.op0().type().id() == "unsignedbv" ||
    expr.op0().type().id() == "signedbv")
  {
    BigInt width = string2integer(id2string(expr.op0().type().width()));

    if (expr.id() == "lshr")
    {
      // this is to guard against large values of distance
      if (distance >= width)
      {
        expr = gen_zero(expr.type());
        return false;
      }
      if (distance >= 0)
      {
        value /= power(2, distance);
        expr = from_integer(value, expr.type());
        return false;
      }
    }
    else if (expr.id() == "ashr")
    {
      // this is to simulate an arithmetic right shift
      if (distance >= 0)
      {
        BigInt new_value = (distance >= width) ? 0 : value / power(2, distance);

        if (value < 0 && new_value == 0)
          new_value = -1;

        expr = from_integer(new_value, expr.type());
        return false;
      }
    }
    else if (expr.id() == "shl")
    {
      // this is to guard against large values of distance
      if (distance >= width)
      {
        expr = gen_zero(expr.type());
        return false;
      }
      if (distance >= 0)
      {
        value *= power(2, distance);
        expr = from_integer(value, expr.type());
        return false;
      }
    }
  }

  return true;
}

bool simplify_exprt::simplify_if_implies(
  exprt &expr,
  const exprt &cond,
  bool truth,
  bool &new_truth)
{
  if (expr == cond)
  {
    new_truth = truth;
    return false;
  }

  if (truth && cond.id() == "<" && expr.id() == "<")
  {
    if (
      cond.op0() == expr.op0() && cond.op1().is_constant() &&
      expr.op1().is_constant() && cond.op1().type() == expr.op1().type())
    {
      const irep_idt &type_id = cond.op1().type().id();
      if (type_id == "unsignedbv")
      {
        const BigInt i1, i2;
        if (
          binary2integer(cond.op1().value().as_string(), false) >=
          binary2integer(expr.op1().value().as_string(), false))
        {
          new_truth = true;
          return false;
        }
      }
      else if (type_id == "signedbv")
      {
        const BigInt i1, i2;
        if (
          binary2integer(cond.op1().value().as_string(), true) >=
          binary2integer(expr.op1().value().as_string(), true))
        {
          new_truth = true;
          return false;
        }
      }
    }
    if (
      cond.op1() == expr.op1() && cond.op0().is_constant() &&
      expr.op0().is_constant() && cond.op0().type() == expr.op0().type())
    {
      const irep_idt &type_id = cond.op1().type().id();
      if (type_id == "unsignedbv")
      {
        const BigInt i1, i2;
        if (
          binary2integer(cond.op1().value().as_string(), false) <=
          binary2integer(expr.op1().value().as_string(), false))
        {
          new_truth = true;
          return false;
        }
      }
      else if (type_id == "signedbv")
      {
        const BigInt i1, i2;
        if (
          binary2integer(cond.op1().value().as_string(), true) <=
          binary2integer(expr.op1().value().as_string(), true))
        {
          new_truth = true;
          return false;
        }
      }
    }
  }

  return true;
}

bool simplify_exprt::simplify_if_recursive(
  exprt &expr,
  const exprt &cond,
  bool truth)
{
  if (expr.type().is_bool())
  {
    bool new_truth;

    if (!simplify_if_implies(expr, cond, truth, new_truth))
    {
      if (new_truth)
      {
        expr.make_true();
        return false;
      }

      expr.make_false();
      return false;
    }
  }

  bool result = true;

  Forall_operands (it, expr)
    result = simplify_if_recursive(*it, cond, truth) && result;

  return result;
}

bool simplify_exprt::simplify_if_conj(exprt &expr, const exprt &cond)
{
  forall_operands (it, cond)
  {
    if (expr == *it)
    {
      expr.make_true();
      return false;
    }
  }

  bool result = true;

  Forall_operands (it, expr)
    result = simplify_if_conj(*it, cond) && result;

  return result;
}

bool simplify_exprt::simplify_if_disj(exprt &expr, const exprt &cond)
{
  forall_operands (it, cond)
  {
    if (expr == *it)
    {
      expr.make_false();
      return false;
    }
  }

  bool result = true;

  Forall_operands (it, expr)
    result = simplify_if_disj(*it, cond) && result;

  return result;
}

bool simplify_exprt::simplify_if_branch(
  exprt &trueexpr,
  exprt &falseexpr,
  const exprt &cond,
  bool simpl_const_objects)
{
  bool tresult = true;
  bool fresult = true;

  if (cond.is_and())
  {
    tresult = simplify_if_conj(trueexpr, cond) && tresult;
    fresult = simplify_if_recursive(falseexpr, cond, false) && fresult;
  }
  else if (cond.id() == "or")
  {
    tresult = simplify_if_recursive(trueexpr, cond, true) && tresult;
    fresult = simplify_if_disj(falseexpr, cond) && fresult;
  }
  else
  {
    tresult = simplify_if_recursive(trueexpr, cond, true) && tresult;
    fresult = simplify_if_recursive(falseexpr, cond, false) && fresult;
  }

  if (!tresult)
    simplify_rec(trueexpr, simpl_const_objects);
  if (!fresult)
    simplify_rec(falseexpr, simpl_const_objects);

  return tresult && fresult;
}

bool simplify_exprt::simplify_if_cond(exprt &expr, bool simpl_const_objects)
{
  bool result = true;
  bool tmp = false;

  while (!tmp)
  {
    tmp = true;

    if (expr.is_and())
    {
      if (expr.has_operands())
      {
        exprt::operandst &operands = expr.operands();
        for (exprt::operandst::iterator it1 = operands.begin();
             it1 != operands.end();
             it1++)
        {
          for (exprt::operandst::iterator it2 = operands.begin();
               it2 != operands.end();
               it2++)
          {
            if (it1 != it2)
              tmp = simplify_if_recursive(*it1, *it2, true) && tmp;
          }
        }
      }
    }

    if (!tmp)
      simplify_rec(expr, simpl_const_objects);

    result = tmp && result;
  }

  return result;
}

bool simplify_exprt::simplify_if(exprt &expr)
{
  exprt::operandst &operands = expr.operands();
  bool result = true;

  if (operands.size() == 3)
  {
    exprt &cond = operands.front();
    exprt &truevalue = *(++operands.begin());
    exprt &falsevalue = operands.back();

    if (truevalue == falsevalue)
    {
      exprt tmp;
      tmp.swap(truevalue);
      expr.swap(tmp);
      return false;
    }

    if (do_simplify_if)
    {
      if (cond.id() == "not")
      {
        exprt tmp;
        tmp.swap(cond.op0());
        cond.swap(tmp);
        truevalue.swap(falsevalue);
      }

      if (expr.type() == bool_typet())
      {
        if (truevalue.is_true() && falsevalue.is_false())
        {
          exprt tmp;
          tmp.swap(cond);
          expr.swap(tmp);
          return false;
        }
        if (truevalue.is_false() && falsevalue.is_true())
        {
          exprt tmp;
          tmp.swap(cond);
          tmp.make_not();
          expr.swap(tmp);
          return false;
        }
      }
    }

    if (cond.is_true())
    {
      exprt tmp;
      tmp.swap(truevalue);
      expr.swap(tmp);
      return false;
    }

    if (cond.is_false())
    {
      exprt tmp;
      tmp.swap(falsevalue);
      expr.swap(tmp);
      return false;
    }
  }

  return result;
}

bool simplify_exprt::simplify_switch(exprt &)
{
  return true;
}

bool simplify_exprt::simplify_not(exprt &expr, bool simpl_const_objects)
{
  if (expr.operands().size() != 1)
    return true;

  exprt &op = expr.op0();

  if (!expr.type().is_bool() || !op.type().is_bool())
    return true;

  if (op.id() == "not") // (not not a) == a
  {
    if (op.operands().size() == 1)
    {
      exprt tmp;
      tmp.swap(op.op0());
      expr.swap(tmp);
      return false;
    }
  }
  else if (op.is_false())
  {
    expr.make_true();
    return false;
  }
  else if (op.is_true())
  {
    expr.make_false();
    return false;
  }
  else if (op.is_and() || op.id() == "or")
  {
    exprt tmp;
    tmp.swap(op);
    expr.swap(tmp);

    Forall_operands (it, expr)
    {
      it->make_not();
      simplify_node(*it, simpl_const_objects);
    }

    expr.id(expr.is_and() ? "or" : "and");

    return false;
  }

  return true;
}

bool simplify_exprt::simplify_boolean(exprt &expr, bool simpl_const_objects)
{
  if (!expr.has_operands())
    return true;

  exprt::operandst &operands = expr.operands();

  if (!expr.type().is_bool())
    return true;

  if (expr.id() == "=>")
  {
    if (
      operands.size() != 2 || !operands.front().type().is_bool() ||
      !operands.back().type().is_bool())
      return true;

    // turn a => b into !a || b

    expr.id("or");
    expr.op0().make_not();
    simplify_node(expr.op0(), simpl_const_objects);
    simplify_node(expr, simpl_const_objects);
    return false;
  }
  if (expr.id() == "<=>")
  {
    if (
      operands.size() != 2 || !operands.front().type().is_bool() ||
      !operands.back().type().is_bool())
      return true;

    if (operands.front().is_false())
    {
      expr.id("not");
      operands.erase(operands.begin());
      return false;
    }
    if (operands.front().is_true())
    {
      exprt tmp(operands.back());
      expr.swap(tmp);
      return false;
    }
    else if (operands.back().is_false())
    {
      expr.id("not");
      operands.erase(++operands.begin());
      return false;
    }
    else if (operands.back().is_true())
    {
      exprt tmp(operands.front());
      expr.swap(tmp);
      return false;
    }
  }
  else if (expr.id() == "or" || expr.is_and() || expr.id() == "xor")
  {
    if (operands.size() == 0)
      return true;

    bool result = true;

    exprt::operandst::const_iterator last;
    bool last_set = false;

    for (exprt::operandst::iterator it = operands.begin();
         it != operands.end();)
    {
      if (!it->type().is_bool())
        return true;

      bool is_true = it->is_true();
      bool is_false = it->is_false();

      if (expr.is_and() && is_false)
      {
        expr.make_false();
        return false;
      }
      if (expr.id() == "or" && is_true)
      {
        expr.make_true();
        return false;
      }

      bool erase;

      if (expr.is_and())
        erase = is_true;
      else
        erase = is_false;

      if (last_set && *it == *last && (expr.id() == "or" || expr.is_and()))
        erase = true; // erase duplicate operands

      if (erase)
      {
        it = operands.erase(it);
        result = false;
      }
      else
      {
        last = it;
        last_set = true;
        it++;
      }
    }

    // search for a and !a
    if (expr.is_and() || expr.id() == "or")
    {
      // first gather all the a's with !a

      std::set<exprt> expr_set;

      forall_operands (it, expr)
        if (
          it->id() == "not" && it->operands().size() == 1 &&
          it->type().is_bool())
          expr_set.insert(it->op0());

      // now search for a

      forall_operands (it, expr)
        if (expr_set.find(*it) != expr_set.end())
        {
          expr.make_bool(expr.id() == "or");
          return false;
        }
    }

    if (operands.size() == 0)
    {
      if (expr.is_and())
        expr.make_true();
      else
        expr.make_false();

      return false;
    }
    if (operands.size() == 1)
    {
      exprt tmp(operands.front());
      expr.swap(tmp);
      return false;
    }

    return result;
  }
  else if (expr.id() == "=" || expr.id() == "notequal")
  {
    if (operands.size() == 2 && operands.front() == operands.back())
    {
      if (expr.id() == "=")
      {
        expr.make_true();
        return false;
      }

      expr.make_false();
      return false;
    }
  }

  return true;
}

bool simplify_exprt::simplify_bitnot(exprt &expr)
{
  if (!expr.has_operands())
    return true;

  exprt::operandst &operands = expr.operands();

  if (operands.size() != 1)
    return true;

  exprt &op = operands.front();

  if (
    expr.type().id() == "bv" || expr.type().id() == "unsignedbv" ||
    expr.type().id() == "signedbv")
  {
    if (op.type() == expr.type())
    {
      if (op.id() == "constant")
      {
        std::string value = op.value().as_string();

        for (char &i : value)
          i = (i == '0') ? '1' : '0';

        exprt tmp("constant", op.type());
        tmp.value(value);
        expr.swap(tmp);
        return false;
      }
    }
  }

  return true;
}

bool simplify_exprt::get_values(const exprt &expr, value_listt &value_list)
{
  if (expr.is_constant())
  {
    BigInt int_value;
    if (to_integer(expr, int_value))
      return true;

    value_list.insert(int_value);

    return false;
  }
  if (expr.id() == "if")
  {
    if (expr.operands().size() != 3)
      return true;

    return get_values(expr.op1(), value_list) ||
           get_values(expr.operands().back(), value_list);
  }

  return true;
}

bool simplify_exprt::simplify_inequality(exprt &expr, bool simpl_const_objects)
{
  exprt::operandst &operands = expr.operands();

  if (!expr.type().is_bool())
    return true;

  if (operands.size() != 2)
    return true;

  // types must match
  if (expr.op0().type() != expr.op1().type())
    return true;

  // first see if we compare to a constant

  bool op0_is_const = expr.op0().is_constant();
  bool op1_is_const = expr.op1().is_constant();

  if (op0_is_const && op1_is_const)
  {
    if (expr.op0().type().is_bool())
    {
      bool v0 = expr.op0().is_true();
      bool v1 = expr.op1().is_true();

      if (expr.id() == "=")
      {
        expr.make_bool(v0 == v1);
        return false;
      }
      if (expr.id() == "notequal")
      {
        expr.make_bool(v0 != v1);
        return false;
      }
    }
    else if (expr.op0().type().id() == "fixedbv")
    {
      fixedbvt f0(to_constant_expr(expr.op0()));
      fixedbvt f1(to_constant_expr(expr.op1()));

      if (expr.id() == "notequal")
        expr.make_bool(f0 != f1);
      else if (expr.id() == "=")
        expr.make_bool(f0 == f1);
      else if (expr.id() == ">=")
        expr.make_bool(f0 >= f1);
      else if (expr.id() == "<=")
        expr.make_bool(f0 <= f1);
      else if (expr.id() == ">")
        expr.make_bool(f0 > f1);
      else if (expr.id() == "<")
        expr.make_bool(f0 < f1);
      else
        assert(false);

      return false;
    }
    else if (expr.op0().type().id() == "floatbv")
    {
      ieee_floatt f0(to_constant_expr(expr.op0()));
      ieee_floatt f1(to_constant_expr(expr.op1()));

      if (expr.id() == "notequal")
        expr.make_bool(f0 != f1);
      else if (expr.id() == "=")
        expr.make_bool(f0 == f1);
      else if (expr.id() == ">=")
        expr.make_bool(f0 >= f1);
      else if (expr.id() == "<=")
        expr.make_bool(f0 <= f1);
      else if (expr.id() == ">")
        expr.make_bool(f0 > f1);
      else if (expr.id() == "<")
        expr.make_bool(f0 < f1);
      else
        assert(false);

      return false;
    }
    else
    {
      BigInt v0, v1;

      if (to_integer(expr.op0(), v0))
        return true;

      if (to_integer(expr.op1(), v1))
        return true;

      if (expr.id() == "notequal")
        expr.make_bool(v0 != v1);
      else if (expr.id() == "=")
        expr.make_bool(v0 == v1);
      else if (expr.id() == ">=")
        expr.make_bool(v0 >= v1);
      else if (expr.id() == "<=")
        expr.make_bool(v0 <= v1);
      else if (expr.id() == ">")
        expr.make_bool(v0 > v1);
      else if (expr.id() == "<")
        expr.make_bool(v0 < v1);
      else
        assert(false);

      return false;
    }
  }
  else if (op0_is_const)
  {
    // we want the constant on the RHS

    if (expr.id() == ">=")
      expr.id("<=");
    else if (expr.id() == "<=")
      expr.id(">=");
    else if (expr.id() == ">")
      expr.id("<");
    else if (expr.id() == "<")
      expr.id(">");

    expr.op0().swap(expr.op1());

    simplify_inequality_constant(expr, simpl_const_objects);
    return false;
  }
  else if (op1_is_const)
  {
    return simplify_inequality_constant(expr, simpl_const_objects);
  }
  else
    return simplify_inequality_not_constant(expr, simpl_const_objects);

  assert(false);
  return false;
}

bool simplify_exprt::eliminate_common_addends(exprt &op0, exprt &op1)
{
  // we can't eliminate zeros
  if (op0.is_zero() || op1.is_zero())
    return true;

  if (op0.id() == "+")
  {
    bool result = true;

    Forall_operands (it, op0)
      if (!eliminate_common_addends(*it, op1))
        result = false;

    return result;
  }
  if (op1.id() == "+")
  {
    bool result = true;

    Forall_operands (it, op1)
      if (!eliminate_common_addends(op0, *it))
        result = false;

    return result;
  }
  else if (op0 == op1)
  {
    // elimination!
    op0 = gen_zero(op0.type());
    op1 = gen_zero(op1.type());
    return false;
  }

  return true;
}

bool simplify_exprt::simplify_inequality_not_constant(
  exprt &expr,
  bool simpl_const_objects)
{
  exprt::operandst &operands = expr.operands();

  // eliminate strict inequalities
  if (expr.id() == "notequal")
  {
    expr.id("=");
    simplify_inequality_not_constant(expr, simpl_const_objects);
    expr.make_not();
    simplify_not(expr, simpl_const_objects);
    return false;
  }
  if (expr.id() == ">")
  {
    expr.id(">=");
    // swap operands
    expr.op0().swap(expr.op1());
    simplify_inequality_not_constant(expr, simpl_const_objects);
    expr.make_not();
    simplify_not(expr, simpl_const_objects);
    return false;
  }
  else if (expr.id() == "<")
  {
    expr.id(">=");
    simplify_inequality_not_constant(expr, simpl_const_objects);
    expr.make_not();
    simplify_not(expr, simpl_const_objects);
    return false;
  }
  else if (expr.id() == "<=")
  {
    expr.id(">=");
    // swap operands
    expr.op0().swap(expr.op1());
    simplify_inequality_not_constant(expr, simpl_const_objects);
    return false;
  }

  // now we only have >=, =

  assert(expr.id() == ">=" || expr.id() == "=");

  // syntactically equal?

  if (operands.front() == operands.back())
  {
    expr.make_true();
    return false;
  }

  // try constants

  value_listt values0, values1;

  bool ok0 = !get_values(expr.op0(), values0);
  bool ok1 = !get_values(expr.op1(), values1);

  if (ok0 && ok1)
  {
    bool first = true;
    bool result = false; // dummy initialization to prevent warning
    bool ok = true;

    // compare possible values

    forall_value_list (it0, values0)
      forall_value_list (it1, values1)
      {
        bool tmp = false;
        const BigInt &int_value0 = *it0;
        const BigInt &int_value1 = *it1;

        if (expr.id() == ">=")
          tmp = (int_value0 >= int_value1);
        else if (expr.id() == "=")
          tmp = (int_value0 == int_value1);
        else
          assert(0);

        if (first)
        {
          result = tmp;
          first = false;
        }
        else if (result != tmp)
        {
          ok = false;
          break;
        }
      }

    if (ok)
    {
      expr.make_bool(result);
      return false;
    }
  }

  // see if we can eliminate common addends on both sides
  // on bit-vectors, this is only sound on '='
  if (expr.id() == "=")
    if (!eliminate_common_addends(expr.op0(), expr.op1()))
    {
      // remove zeros
      simplify_node(expr.op0(), simpl_const_objects);
      simplify_node(expr.op1(), simpl_const_objects);
      simplify_inequality(expr, simpl_const_objects);
      return false;
    }

  return true;
}

bool simplify_exprt::simplify_inequality_constant(
  exprt &expr,
  bool simpl_const_objects)
{
  assert(expr.op1().is_constant());

  if (expr.op1().type().id() == "pointer")
    return true;

  // is it a separation predicate?

  if (expr.op0().id() == "+")
  {
    // see if there is a constant in the sum

    if (expr.id() == "=" || expr.id() == "notequal")
    {
      BigInt constant = 0;
      bool changed = false;

      Forall_operands (it, expr.op0())
      {
        if (it->is_constant())
        {
          BigInt i;
          if (!to_integer(*it, i))
          {
            constant += i;
            *it = gen_zero(it->type());
            changed = true;
          }
        }
      }

      if (changed)
      {
        // adjust constant
        BigInt i;
        to_integer(expr.op1(), i);
        i -= constant;
        expr.op1() = from_integer(i, expr.op1().type());

        simplify_addition_substraction(expr.op0(), simpl_const_objects);
        simplify_inequality(expr, simpl_const_objects);
        return false;
      }
    }
  }

  // is the constant zero?

  if (expr.op1().is_zero())
  {
    if (expr.id() == ">=" && expr.op0().type().id() == "unsignedbv")
    {
      // zero is always smaller or equal something unsigned
      expr.make_true();
      return false;
    }

    exprt &operand = expr.op0();

    if (expr.id() == "=")
    {
      // rules below do not hold for >=
      if (operand.id() == "unary-")
      {
        if (operand.operands().size() != 1)
          return true;
        exprt tmp;
        tmp.swap(operand.op0());
        operand.swap(tmp);
        return false;
      }
      if (operand.id() == "+")
      {
        // simplify a+-b=0 to a=b

        if (operand.operands().size() == 2)
        {
          // if we have -b+a=0, make that a+(-b)=0

          if (operand.op0().id() == "unary-")
            operand.op0().swap(operand.op1());

          if (
            operand.op1().id() == "unary-" &&
            operand.op1().operands().size() == 1)
          {
            exprt tmp(expr.id(), expr.type());
            tmp.operands().resize(2);
            tmp.op0().swap(operand.op0());
            tmp.op1().swap(operand.op1().op0());
            expr.swap(tmp);
            return false;
          }
        }
      }
    }
  }

  return true;
}

bool simplify_exprt::simplify_relation(exprt &expr, bool simpl_const_objects)
{
  bool result = true;

  // special case
  if (
    (expr.id() == "=" || expr.id() == "notequal") &&
    expr.operands().size() == 2 && expr.op0().type().id() == "pointer")
  {
    const exprt *other = nullptr;

    if (expr.op0().is_constant() && expr.op0().value().as_string() == "NULL")
      other = &(expr.op1());
    else if (
      expr.op1().is_constant() && expr.op1().value().as_string() == "NULL")
      other = &(expr.op0());

    if (other != nullptr)
    {
      if (other->is_address_of() && other->operands().size() == 1)
      {
        if (other->op0().id() == "symbol" || other->op0().id() == "member")
        {
          expr.make_bool(expr.id() != "=");
          return false;
        }
      }
    }
  }

  if (
    expr.id() == "=" || expr.id() == "notequal" || expr.id() == ">=" ||
    expr.id() == "<=" || expr.id() == ">" || expr.id() == "<")
    result = simplify_inequality(expr, simpl_const_objects) && result;

  return result;
}

bool simplify_exprt::simplify_ieee_float_relation(exprt &expr)
{
  exprt::operandst &operands = expr.operands();

  if (!expr.type().is_bool())
    return true;

  if (operands.size() != 2)
    return true;

  // types must match
  if (expr.op0().type() != expr.op1().type())
    return true;

  if (expr.op0().type().id() != "floatbv")
    return true;

  // first see if we compare to a constant

  if (expr.op0().is_constant() && expr.op1().is_constant())
  {
    ieee_floatt f0(to_constant_expr(expr.op0()));
    ieee_floatt f1(to_constant_expr(expr.op1()));

    if (expr.id() == "ieee_float_notequal")
      expr.make_bool((f0 != f1));
    else if (expr.id() == "ieee_float_equal")
      expr.make_bool((f0 == f1));
    else
      assert(false);

    return false;
  }

  if (expr.op0() == expr.op1())
  {
    // x!=x is the same as saying isnan(op)
    exprt isnan("isnan", bool_typet());
    isnan.copy_to_operands(expr.op0());

    if (expr.id() == "ieee_float_notequal")
    {
    }
    else if (expr.id() == "ieee_float_equal")
      isnan.make_not();
    else
      assert(false);

    expr.swap(isnan);
    return false;
  }

  return true;
}

bool simplify_exprt::simplify_with(exprt &expr)
{
  bool result = true;

  if ((expr.operands().size() % 2) != 1)
    return true;

  // now look at first operand

  if (expr.op0().type().id() == "struct")
  {
    if (expr.op0().id() == "struct" || expr.op0().id() == "constant")
    {
      while (expr.operands().size() > 1)
      {
        const irep_idt &component_name = expr.op1().component_name();

        if (!to_struct_type(expr.op0().type()).has_component(component_name))
          return result;

        unsigned number =
          to_struct_type(expr.op0().type()).component_number(component_name);

        expr.op0().operands()[number].swap(expr.op2());

        expr.operands().erase(++expr.operands().begin());
        expr.operands().erase(++expr.operands().begin());

        result = false;
      }
    }
  }
  else if (expr.op0().type().is_array())
  {
    if (expr.op0().is_array() || expr.op0().id() == "constant")
    {
      while (expr.operands().size() > 1)
      {
        BigInt i;

        if (to_integer(expr.op1(), i))
          break;

        if (i < 0 || i >= expr.op0().operands().size())
          break;

        if (!expr.op2().is_constant())
          break;

        expr.op0().operands()[i.to_uint64()].swap(expr.op2());

        expr.operands().erase(++expr.operands().begin());
        expr.operands().erase(++expr.operands().begin());

        result = false;
      }
    }
  }

  if (expr.operands().size() == 1)
  {
    exprt tmp;
    tmp.swap(expr.op0());
    expr.swap(tmp);
    result = false;
  }

  return result;
}

bool simplify_exprt::simplify_index(index_exprt &expr, bool simpl_const_objects)
{
  if (expr.operands().size() != 2)
    return true;

  if (expr.op0().id() == "with")
  {
    exprt &with_expr = expr.op0();

    if (with_expr.operands().size() != 3)
      return true;

    if (with_expr.op1() == expr.op1())
    {
      // simplify (e with [i:=v])[i] to v
      exprt tmp;
      tmp.swap(with_expr.op2());
      expr.swap(tmp);
      return false;
    }

    // turn (a with i:=x)[j] into (i==j)?x:a[j]
    // watch out that the type of i and j might be different
    equality_exprt equality_expr(expr.op1(), with_expr.op1());

    if (equality_expr.lhs().type() != equality_expr.rhs().type())
      equality_expr.rhs().make_typecast(equality_expr.lhs().type());

    simplify_relation(equality_expr, simpl_const_objects);

    index_exprt new_index_expr;
    new_index_expr.type() = expr.type();
    new_index_expr.array() = with_expr.op0();
    new_index_expr.index() = expr.op1();

    simplify_index(new_index_expr, simpl_const_objects); // recursive call

    exprt if_expr("if", expr.type());
    if_expr.reserve_operands(3);
    if_expr.move_to_operands(equality_expr);
    if_expr.copy_to_operands(with_expr.op2());
    if_expr.move_to_operands(new_index_expr);

    simplify_if(if_expr);

    expr.swap(if_expr);

    return false;
  }
  else if (!simpl_const_objects)
  {
    // index under address_of, can't simplify access
    return true;
  }
  else if (expr.op0().id() == "constant" || expr.op0().is_array())
  {
    BigInt i;

    if (to_integer(expr.op1(), i))
    {
    }
    else if (i < 0 || i >= expr.op0().operands().size())
    {
      // out of bounds
    }
    else
    {
      // ok
      exprt tmp;
      tmp.swap(expr.op0().operands()[i.to_uint64()]);
      expr.swap(tmp);
      return false;
    }
  }
  else if (expr.op0().id() == "string-constant")
  {
    BigInt i;

    const irep_idt &value = expr.op0().value();

    if (to_integer(expr.op1(), i))
    {
    }
    else if (i < 0 || i > value.size())
    {
      // out of bounds
    }
    else if (bv_width(expr.type()) == 8) /* only for char and char8_t strings */
    {
      // terminating zero?
      char v = (i == value.size()) ? 0 : value[i.to_uint64()];
      exprt tmp = from_integer(v, expr.type());
      expr.swap(tmp);
      return false;
    }
  }
  else if (expr.op0().id() == "array_of")
  {
    if (expr.op0().operands().size() == 1)
    {
      exprt tmp;
      tmp.swap(expr.op0().op0());
      expr.swap(tmp);
      return false;
    }
  }

  return true;
}

bool simplify_exprt::simplify_object(exprt &expr)
{
  if (expr.id() == "+")
  {
    if (expr.type().id() == "pointer")
    {
      // kill integers from sum
      for (unsigned i = 0; i < expr.operands().size(); i++)
        if (expr.operands()[i].type().id() == "pointer")
        {
          exprt tmp = expr.operands()[i];
          expr.swap(tmp);
          simplify_object(expr);
          return false;
        }
    }
  }
  else if (expr.id() == "typecast")
  {
    if (expr.operands().size() == 1 && expr.op0().type().id() == "pointer")
    {
      exprt tmp;
      tmp.swap(expr.op0());
      expr.swap(tmp);
      simplify_object(expr);
      return false;
    }
  }

  return true;
}

bool simplify_exprt::simplify_pointer_object(exprt &expr)
{
  if (expr.operands().size() != 1)
    return true;

  exprt &op = expr.op0();

  return simplify_object(op);
}

bool simplify_exprt::simplify_is_dynamic_object(exprt &expr)
{
  if (expr.operands().size() != 1)
    return true;

  exprt &op = expr.op0();

  return simplify_object(op);
}

tvt simplify_exprt::objects_equal(const exprt &a, const exprt &b)
{
  if (a == b)
    return tvt(true);

  if (
    a.is_address_of() && b.is_address_of() && a.operands().size() == 1 &&
    b.operands().size() == 1)
    return objects_equal_address_of(a.op0(), b.op0());

  if (
    a.id() == "constant" && b.id() == "constant" &&
    a.value().as_string() == "NULL" && b.value().as_string() == "NULL")
    return tvt(true);

  return tvt(tvt::TV_UNKNOWN);
}

tvt simplify_exprt::objects_equal_address_of(const exprt &a, const exprt &b)
{
  if (a == b)
    return tvt(true);

  if (a.id() == "symbol" && b.id() == "symbol")
  {
    if (a.identifier() == b.identifier())
      return tvt(true);
  }
  else if (a.id() == "index" && b.id() == "index")
  {
    if (a.operands().size() == 2 && b.operands().size() == 2)
      return objects_equal_address_of(a.op0(), b.op0());
  }
  else if (a.id() == "member" && b.id() == "member")
  {
    if (a.operands().size() == 1 && b.operands().size() == 1)
      return objects_equal_address_of(a.op0(), b.op0());
  }

  return tvt(tvt::TV_UNKNOWN);
}

bool simplify_exprt::simplify_same_object(exprt &expr)
{
  if (expr.operands().size() != 2)
    return true;

  bool result = true;

  if (!simplify_object(expr.op0()))
    result = false;
  if (!simplify_object(expr.op1()))
    result = false;

  tvt res = objects_equal(expr.op0(), expr.op1());

  if (res.is_true())
  {
    expr.make_true();
    return false;
  }
  if (res.is_false())
  {
    expr.make_false();
    return false;
  }

  return result;
}

bool simplify_exprt::simplify_dynamic_size(exprt &expr)
{
  if (expr.operands().size() != 1)
    return true;

  exprt &op = expr.op0();

  return simplify_object(op);
}

bool simplify_exprt::simplify_valid_object(exprt &expr)
{
  if (expr.operands().size() != 1)
    return true;

  exprt &op = expr.op0();

  return simplify_object(op);
}

bool simplify_exprt::simplify_member(member_exprt &expr)
{
  if (expr.operands().size() != 1)
    return true;

  const irep_idt &component_name = expr.component_name();

  exprt &op = expr.op0();

  if (op.id() == "with")
  {
    if (op.operands().size() >= 3)
    {
      exprt::operandst &operands = op.operands();

      while (operands.size() > 1)
      {
        exprt &op1 = operands[operands.size() - 2];
        exprt &op2 = operands[operands.size() - 1];

        if (op1.component_name() == component_name)
        {
          // found it!
          exprt tmp;
          tmp.swap(op2);
          expr.swap(tmp);
          return false;
        }
        // something else, get rid of it
        operands.resize(operands.size() - 2);
      }

      if (op.operands().size() == 1)
      {
        exprt tmp;
        tmp.swap(op.op0());
        op.swap(tmp);
        // do this recursively
        simplify_member(expr);
      }

      return false;
    }
  }
  else if (op.id() == "struct" || op.id() == "constant")
  {
    if (op.type().id() == "struct")
    {
      const struct_typet &struct_type = to_struct_type(op.type());
      if (struct_type.has_component(component_name))
      {
        unsigned number = struct_type.component_number(component_name);
        exprt tmp;
        tmp.swap(op.operands()[number]);
        expr.swap(tmp);
        return false;
      }
    }
  }

  return true;
}

struct saj_tablet
{
  const char *id;
  const char *type_id;
} const saj_table[] = {
  {"+", "complex"},         {"+", "unsignedbv"},    {"+", "signedbv"},
  {"+", "floatbv"},         {"+", "pointer"},       {"*", "complex"},
  {"*", "unsignedbv"},      {"*", "signedbv"},      {"*", "floatbv"},
  {"and", "bool"},          {"or", "bool"},         {"xor", "bool"},
  {"bitand", "unsignedbv"}, {"bitand", "signedbv"}, {"bitand", "floatbv"},
  {"bitor", "unsignedbv"},  {"bitor", "signedbv"},  {"bitor", "floatbv"},
  {"bitxor", "unsignedbv"}, {"bitxor", "signedbv"}, {"bitxor", "floatbv"},
  {nullptr, nullptr}};

bool sort_and_join(const irep_idt &id, const irep_idt &type_id)
{
  for (unsigned i = 0; saj_table[i].id != nullptr; i++)
    if (id == saj_table[i].id && type_id == saj_table[i].type_id)
      return true;

  return false;
}

bool simplify_exprt::sort_and_join(exprt &expr)
{
  bool result = true;

  if (!expr.has_operands())
    return true;

  if (!::sort_and_join(expr.id(), expr.type().id()))
    return true;

  // check operand types

  forall_operands (it, expr)
    if (!::sort_and_join(expr.id(), it->type().id()))
      return true;

  // join expressions

  for (unsigned i = 0; i < expr.operands().size();)
  {
    if (expr.operands()[i].id() == expr.id())
    {
      unsigned no_joined = expr.operands()[i].operands().size();

      expr.operands().insert(
        expr.operands().begin() + i + 1,
        expr.operands()[i].operands().begin(),
        expr.operands()[i].operands().end());

      expr.operands().erase(expr.operands().begin() + i);

      i += no_joined;

      result = false;
    }
    else
      i++;
  }

  // sort it

  result = sort_operands(expr.operands()) && result;

  return result;
}

bool simplify_exprt::simplify_unary_minus(exprt &expr)
{
  if (
    config.options.get_bool_option("int-encoding") &&
    !expr.type().is_fixedbv() && !expr.type().is_floatbv() &&
    !expr.type().is_signedbv())
    // Never simplify a unary minus if we're using integer encoding. The SMT
    // solver is going to have its own negative representation, and this
    // conflicts with the current irep representation of binary-in-a-string.
    // Specifically, the current 01_cmbc_Malloc1 test encodes:
    //
    // o = n - 1;
    //
    // as
    //
    // o = n + 4294967295
    //
    // Which may be fine in bit-vector mode, but that calculation does _not_
    // wrap around in integer mode. So, block such simplification of unary-'s.
    //
    // Update: After further thought that kind of overflowing is fine for
    // _signed_ types. This is because the binary2integer routine will observe
    // that the expr is signed, and interpret its value as negative. Which is
    // ok.
    return true;

  if (expr.operands().size() != 1)
    return true;

  if (!is_number(expr.type()))
    return true;

  exprt &operand = expr.op0();

  if (expr.type() != operand.type())
    return true;

  if (operand.id() == "unary-")
  {
    // cancel out "-(-x)" to "x"
    if (operand.operands().size() != 1)
      return true;

    if (!is_number(operand.op0().type()))
      return true;

    exprt tmp;
    tmp.swap(expr.op0().op0());
    expr.swap(tmp);
    return false;
  }
  if (operand.is_constant())
  {
    if (expr.type().is_signedbv() || expr.type().is_unsignedbv())
    {
      BigInt int_value;

      if (to_integer(expr.op0(), int_value))
        return true;

      exprt tmp = from_integer(-int_value, expr.type());

      if (tmp.is_nil())
        return true;

      expr.swap(tmp);

      return false;
    }
    if (expr.type().is_fixedbv())
    {
      fixedbvt f(to_constant_expr(expr.op0()));
      f.negate();
      expr = f.to_expr();
      return false;
    }
    else if (expr.type().is_floatbv())
    {
      ieee_floatt f(to_constant_expr(expr.op0()));
      f.negate();
      expr = f.to_expr();
      return false;
    }
  }

  return true;
}

bool simplify_exprt::simplify_node(exprt &expr, bool simpl_const_objects)
{
  if (!expr.has_operands())
    return true;

  bool result = true;

  result = sort_and_join(expr) && result;

  if (expr.id() == "typecast")
    result = simplify_typecast(expr, simpl_const_objects) && result;
  else if (
    expr.id() == "=" || expr.id() == "notequal" || expr.id() == ">" ||
    expr.id() == "<" || expr.id() == ">=" || expr.id() == "<=")
    result = simplify_relation(expr, simpl_const_objects) && result;
  else if (expr.id() == "if")
    result = simplify_if(expr) && result;
  else if (expr.id() == "with")
    result = simplify_with(expr) && result;
  else if (expr.id() == "index")
    result = simplify_index(to_index_expr(expr), simpl_const_objects) && result;
  else if (expr.id() == "member")
    result = simplify_member(to_member_expr(expr)) && result;
  else if (expr.id() == "pointer_object")
    result = simplify_pointer_object(expr) && result;
  else if (expr.id() == "is_dynamic_object")
    result = simplify_is_dynamic_object(expr) && result;
  else if (expr.id() == "same-object")
    result = simplify_same_object(expr) && result;
  else if (expr.id() == "dynamic_size")
    result = simplify_dynamic_size(expr) && result;
  else if (expr.id() == "valid_object")
    result = simplify_valid_object(expr) && result;
  else if (expr.id() == "switch")
    result = simplify_switch(expr) && result;
  else if (expr.id() == "/")
    result = simplify_division(expr) && result;
  else if (expr.id() == "mod")
    result = simplify_modulo(expr) && result;
  else if (expr.id() == "bitnot")
    result = simplify_bitnot(expr) && result;
  else if (expr.id() == "mod")
  {
  }
  else if (
    expr.id() == "bitnot" || expr.id() == "bitand" || expr.id() == "bitor" ||
    expr.id() == "bitxor")
    result = simplify_bitwise(expr) && result;
  else if (expr.id() == "ashr" || expr.id() == "lshr" || expr.id() == "shl")
    result = simplify_shifts(expr) && result;
  else if (expr.id() == "+" || expr.id() == "-")
    result =
      simplify_addition_substraction(expr, simpl_const_objects) && result;
  else if (expr.id() == "*")
    result = simplify_multiplication(expr) && result;
  else if (expr.id() == "unary-")
    result = simplify_unary_minus(expr) && result;
  else if (expr.id() == "not")
    result = simplify_not(expr, simpl_const_objects) && result;
  else if (
    expr.id() == "=>" || expr.id() == "<=>" || expr.id() == "or" ||
    expr.id() == "xor" || expr.is_and())
    result = simplify_boolean(expr, simpl_const_objects) && result;
  else if (expr.id() == "comma")
  {
    if (expr.operands().size() != 0)
    {
      exprt tmp;
      tmp.swap(expr.operands()[expr.operands().size() - 1]);
      expr.swap(tmp);
      result = false;
    }
  }
  else if (expr.id() == "dereference")
    result = simplify_dereference(expr) && result;
  else if (expr.is_address_of())
    result = simplify_address_of(expr) && result;
  else if (expr.id() == "pointer_offset")
    result = simplify_pointer_offset(expr, simpl_const_objects) && result;
  else if (expr.id() == "concatenation")
    result = simplify_concatenation(expr) && result;
  else if (
    expr.id() == "ieee_float_equal" || expr.id() == "ieee_float_notequal")
    result = simplify_ieee_float_relation(expr) && result;

  return result;
}

bool simplify_exprt::simplify_rec(exprt &expr, bool simpl_const_objects)
{
  // look up in cache

#ifdef USE_CACHE
  std::pair<simplify_expr_cachet::containert::iterator, bool> cache_result =
    simplify_expr_cache.container(simpl_const_objects)
      .insert(std::pair<exprt, exprt>(expr, exprt()));

  if (!cache_result.second) // found!
  {
    const exprt &new_expr = cache_result.first->second;

    if (new_expr.id() == "")
      return true; // no change

    expr = new_expr;
    return false;
  }
#endif

  bool result = true;
  if (expr.is_address_of())
    simpl_const_objects = false;

  if (expr.has_operands())
    Forall_operands (it, expr)
      if (!simplify_rec(*it, simpl_const_objects)) // recursive call
        result = false;

  if (!simplify_node(expr, simpl_const_objects))
    result = false;

#ifdef USE_CACHE
  // save in cache
  if (!result)
    cache_result.first->second = expr;
#endif

  return result;
}

bool simplify(exprt &expr)
{
  if (config.options.get_bool_option("no-simplify"))
    return true;

  simplify_exprt simplify_expr;

  return simplify_expr.simplify(expr);
}