#ifndef CPROVER_STD_EXPR_H
#define CPROVER_STD_EXPR_H

#include <cassert>
#include <util/bitvector.h>
#include <util/expr.h>
#include <util/mp_arith.h>

class transt : public exprt
{
public:
  transt()
  {
    id(exprt::trans);
    operands().resize(3);
  }

  exprt &invar()
  {
    return op0();
  }
  exprt &init()
  {
    return op1();
  }
  exprt &trans()
  {
    return op2();
  }

  const exprt &invar() const
  {
    return op0();
  }
  const exprt &init() const
  {
    return op1();
  }
  const exprt &trans() const
  {
    return op2();
  }
};

extern inline const transt &to_trans(const exprt &expr)
{
  assert(expr.id() == exprt::trans && expr.operands().size() == 3);
  return static_cast<const transt &>(expr);
}

extern inline transt &to_trans(exprt &expr)
{
  assert(expr.id() == exprt::trans && expr.operands().size() == 3);
  return static_cast<transt &>(expr);
}

class symbol_exprt : public exprt
{
public:
  symbol_exprt() : exprt(exprt::symbol)
  {
  }

  explicit symbol_exprt(const irep_idt &identifier) : exprt(exprt::symbol)
  {
    set_identifier(identifier);
  }

  symbol_exprt(const irep_idt &identifier, const typet &type)
    : exprt(exprt::symbol, type)
  {
    set_identifier(identifier);
  }

  void set_identifier(const irep_idt &identifier)
  {
    this->identifier(identifier);
  }

  const irep_idt &get_identifier() const
  {
    return get("identifier");
  }
};

extern inline const symbol_exprt &to_symbol_expr(const exprt &expr)
{
  if(!(expr.id() == exprt::symbol && !expr.has_operands()))
  {
    expr.dump();
    abort();
  }
  return static_cast<const symbol_exprt &>(expr);
}

extern inline symbol_exprt &to_symbol_expr(exprt &expr)
{
  assert(expr.id() == exprt::symbol && !expr.has_operands());
  return static_cast<symbol_exprt &>(expr);
}

/*! \brief Generic base class for unary expressions
*/
class unary_exprt : public exprt
{
public:
  inline unary_exprt()
  {
    operands().resize(1);
  }

  inline explicit unary_exprt(const irep_idt &id) : exprt(id)
  {
    operands().resize(1);
  }

  inline unary_exprt(const irep_idt &_id, const exprt &_op)
    : exprt(_id, _op.type())
  {
    copy_to_operands(_op);
  }

  inline unary_exprt(const irep_idt &_id, const typet &_type)
    : exprt(_id, _type)
  {
    operands().resize(1);
  }

  inline unary_exprt(const irep_idt &_id, const exprt &_op, const typet &_type)
    : exprt(_id, _type)
  {
    copy_to_operands(_op);
  }
};

class predicate_exprt : public exprt
{
public:
  predicate_exprt() : exprt(irep_idt(), typet("bool"))
  {
  }

  predicate_exprt(const irep_idt &_id) : exprt(_id, typet("bool"))
  {
  }

  predicate_exprt(const irep_idt &_id, const exprt &_op)
    : exprt(_id, typet("bool"))
  {
    copy_to_operands(_op);
  }

  predicate_exprt(const irep_idt &_id, const exprt &_op0, const exprt &_op1)
    : exprt(_id, typet("bool"))
  {
    copy_to_operands(_op0, _op1);
  }
};

class binary_relation_exprt : public predicate_exprt
{
public:
  binary_relation_exprt()
  {
    operands().resize(2);
  }

  explicit binary_relation_exprt(const irep_idt &id) : predicate_exprt(id)
  {
    operands().resize(2);
  }

  binary_relation_exprt(
    const exprt &_lhs,
    const irep_idt &_id,
    const exprt &_rhs)
    : predicate_exprt(_id, _lhs, _rhs)
  {
  }

  inline exprt &lhs()
  {
    return op0();
  }

  inline const exprt &lhs() const
  {
    return op0();
  }

  inline exprt &rhs()
  {
    return op1();
  }

  inline const exprt &rhs() const
  {
    return op1();
  }
};

class binary_exprt : public exprt
{
public:
  binary_exprt()
  {
    operands().resize(2);
  }

  explicit binary_exprt(const irep_idt &id) : exprt(id)
  {
    operands().resize(2);
  }

  binary_exprt(const exprt &_lhs, const irep_idt &_id, const exprt &_rhs)
    : exprt(_id)
  {
    copy_to_operands(_lhs, _rhs);
  }

  inline binary_exprt(
    const exprt &_lhs,
    const irep_idt &_id,
    const exprt &_rhs,
    const typet &_type)
    : exprt(_id, _type)
  {
    copy_to_operands(_lhs, _rhs);
  }
};

class plus_exprt : public binary_exprt
{
public:
  plus_exprt() : binary_exprt(exprt::plus)
  {
  }

  plus_exprt(const exprt &_lhs, const exprt &_rhs)
    : binary_exprt(_lhs, exprt::plus, _rhs)
  {
  }
};

class minus_exprt : public binary_exprt
{
public:
  minus_exprt() : binary_exprt(exprt::minus)
  {
  }

  minus_exprt(const exprt &_lhs, const exprt &_rhs)
    : binary_exprt(_lhs, exprt::minus, _rhs)
  {
  }
};

class mult_exprt : public binary_exprt
{
public:
  mult_exprt() : binary_exprt(exprt::mult)
  {
  }

  mult_exprt(const exprt &_lhs, const exprt &_rhs)
    : binary_exprt(_lhs, exprt::mult, _rhs)
  {
  }
};

class div_exprt : public binary_exprt
{
public:
  div_exprt() : binary_exprt(exprt::div)
  {
  }

  div_exprt(const exprt &_lhs, const exprt &_rhs)
    : binary_exprt(_lhs, exprt::div, _rhs)
  {
  }
};

class mod_exprt : public binary_exprt
{
public:
  mod_exprt() : binary_exprt(exprt::mod)
  {
  }

  mod_exprt(const exprt &_lhs, const exprt &_rhs)
    : binary_exprt(_lhs, exprt::mod, _rhs)
  {
  }
};

class equality_exprt : public binary_relation_exprt
{
public:
  equality_exprt() : binary_relation_exprt(exprt::equality)
  {
  }

  equality_exprt(const exprt &_lhs, const exprt &_rhs)
    : binary_relation_exprt(_lhs, exprt::equality, _rhs)
  {
  }
};

extern inline const equality_exprt &to_equality_expr(const exprt &expr)
{
  assert(expr.id() == exprt::equality && expr.operands().size() == 2);
  return static_cast<const equality_exprt &>(expr);
}

extern inline equality_exprt &to_equality_expr(exprt &expr)
{
  assert(expr.id() == exprt::equality && expr.operands().size() == 2);
  return static_cast<equality_exprt &>(expr);
}

class index_exprt : public exprt
{
public:
  index_exprt() : exprt(exprt::index)
  {
    operands().resize(2);
  }

  index_exprt(const typet &_type) : exprt(exprt::index, _type)
  {
    operands().resize(2);
  }

  inline index_exprt(const exprt &_array, const exprt &_index)
    : exprt(exprt::index, _array.type().subtype())
  {
    copy_to_operands(_array, _index);
  }

  inline index_exprt(
    const exprt &_array,
    const exprt &_index,
    const typet &_type)
    : exprt(exprt::index, _type)
  {
    copy_to_operands(_array, _index);
  }

  inline exprt &array()
  {
    return op0();
  }

  inline const exprt &array() const
  {
    return op0();
  }

  inline exprt &index()
  {
    return op1();
  }

  inline const exprt &index() const
  {
    return op1();
  }

  friend inline const index_exprt &to_index_expr(const exprt &expr)
  {
    assert(expr.id() == exprt::index && expr.operands().size() == 2);
    return static_cast<const index_exprt &>(expr);
  }

  friend inline index_exprt &to_index_expr(exprt &expr)
  {
    assert(expr.id() == exprt::index && expr.operands().size() == 2);
    return static_cast<index_exprt &>(expr);
  }
};

const index_exprt &to_index_expr(const exprt &expr);
index_exprt &to_index_expr(exprt &expr);

inline const unary_exprt &to_unary_expr(const exprt &expr)
{
  const unary_exprt &ret = static_cast<const unary_exprt &>(expr);
  assert(ret.operands().size() == 1);
  return ret;
}

class array_of_exprt : public exprt
{
public:
  inline array_of_exprt() : exprt(exprt::arrayof)
  {
  }

  explicit inline array_of_exprt(const exprt &_what, const typet &_type)
    : exprt(exprt::arrayof, _type)
  {
    copy_to_operands(_what);
  }

  inline exprt &what()
  {
    return op0();
  }

  inline const exprt &what() const
  {
    return op0();
  }

  friend inline const array_of_exprt &to_array_of_expr(const exprt &expr)
  {
    assert(expr.id() == exprt::arrayof && expr.operands().size() == 1);
    return static_cast<const array_of_exprt &>(expr);
  }

  friend inline array_of_exprt &to_array_of_expr(exprt &expr)
  {
    assert(expr.id() == exprt::arrayof && expr.operands().size() == 1);
    return static_cast<array_of_exprt &>(expr);
  }
};

const array_of_exprt &to_array_of_expr(const exprt &expr);
array_of_exprt &to_array_of_expr(exprt &expr);

/*! \brief union constructor from single element
*/
class union_exprt : public exprt
{
public:
  inline union_exprt() : exprt(id_union)
  {
  }

  explicit inline union_exprt(const typet &_type) : exprt(id_union, _type)
  {
  }

  explicit inline union_exprt(
    const irep_idt &_component_name,
    const typet &_type)
    : exprt(id_union, _type)
  {
    set_component_name(_component_name);
  }

  friend inline const union_exprt &to_union_expr(const exprt &expr)
  {
    assert(expr.id() == id_union && expr.operands().size() == 1);
    return static_cast<const union_exprt &>(expr);
  }

  friend inline union_exprt &to_union_expr(exprt &expr)
  {
    assert(expr.id() == id_union && expr.operands().size() == 1);
    return static_cast<union_exprt &>(expr);
  }

  inline irep_idt get_component_name() const
  {
    return get(exprt::a_comp_name);
  }

  inline void set_component_name(const irep_idt &component_name)
  {
    set(exprt::a_comp_name, component_name);
  }

  inline void set_component_number(unsigned component_number)
  {
    set(exprt::a_comp_name, component_number);
  }
};

/*! \brief Cast a generic exprt to a \ref union_exprt
 *
 * This is an unchecked conversion. \a expr must be known to be \ref
 * union_exprt.
 *
 * \param expr Source expression
 * \return Object of type \ref union_exprt
 *
 * \ingroup gr_std_expr
*/
const union_exprt &to_union_expr(const exprt &expr);
/*! \copydoc to_union_expr(const exprt &)
 * \ingroup gr_std_expr
*/
union_exprt &to_union_expr(exprt &expr);

/*! \brief struct constructor from list of elements
*/
class struct_exprt : public exprt
{
public:
  inline struct_exprt() : exprt(id_struct)
  {
  }

  explicit inline struct_exprt(const typet &_type) : exprt(id_struct, _type)
  {
  }

  friend inline const struct_exprt &to_struct_expr(const exprt &expr)
  {
    assert(expr.id() == id_struct);
    return static_cast<const struct_exprt &>(expr);
  }

  friend inline struct_exprt &to_struct_expr(exprt &expr)
  {
    assert(expr.id() == id_struct);
    return static_cast<struct_exprt &>(expr);
  }
};

/*! \brief Cast a generic exprt to a \ref struct_exprt
 *
 * This is an unchecked conversion. \a expr must be known to be \ref
 * struct_exprt.
 *
 * \param expr Source expression
 * \return Object of type \ref struct_exprt
 *
 * \ingroup gr_std_expr
*/
const struct_exprt &to_struct_expr(const exprt &expr);
/*! \copydoc to_struct_expr(const exprt &)
 * \ingroup gr_std_expr
*/
struct_exprt &to_struct_expr(exprt &expr);

class object_descriptor_exprt : public exprt
{
public:
  object_descriptor_exprt() : exprt(exprt::objdesc)
  {
    operands().resize(2);
    op0().id("unknown");
    op1().id("unknown");
  }

  inline exprt &object()
  {
    return op0();
  }

  inline const exprt &object() const
  {
    return op0();
  }

  const exprt &root_object() const
  {
    const exprt *p = &object();

    while (p->id() == exprt::member || p->id() == exprt::index)
    {
      assert(p->operands().size() != 0);
      p = &p->op0();
    }

    return *p;
  }

  inline exprt &offset()
  {
    return op1();
  }

  inline const exprt &offset() const
  {
    return op1();
  }

  friend inline const object_descriptor_exprt &
  to_object_descriptor_expr(const exprt &expr)
  {
    assert(expr.id() == exprt::objdesc && expr.operands().size() == 2);
    return static_cast<const object_descriptor_exprt &>(expr);
  }

  friend inline object_descriptor_exprt &to_object_descriptor_expr(exprt &expr)
  {
    assert(expr.id() == exprt::objdesc && expr.operands().size() == 2);
    return static_cast<object_descriptor_exprt &>(expr);
  }
};

const object_descriptor_exprt &to_object_descriptor_expr(const exprt &expr);
object_descriptor_exprt &to_object_descriptor_expr(exprt &expr);

class dynamic_object_exprt : public exprt
{
public:
  dynamic_object_exprt() : exprt(exprt::dynobj)
  {
    operands().resize(2);
    op0().id("unknown");
    op1().id("unknown");
  }

  explicit dynamic_object_exprt(const typet &type) : exprt(dynobj, type)
  {
    operands().resize(2);
    op0().id("unknown");
    op1().id("unknown");
  }

  inline exprt &instance()
  {
    return op0();
  }

  inline const exprt &instance() const
  {
    return op0();
  }

  inline exprt &valid()
  {
    return op1();
  }

  inline const exprt &valid() const
  {
    return op1();
  }

  friend inline const dynamic_object_exprt &
  to_dynamic_object_expr(const exprt &expr)
  {
    assert(expr.id() == exprt::dynobj && expr.operands().size() == 2);
    return static_cast<const dynamic_object_exprt &>(expr);
  }

  friend inline dynamic_object_exprt &to_dynamic_object_expr(exprt &expr)
  {
    assert(expr.id() == exprt::dynobj && expr.operands().size() == 2);
    return static_cast<dynamic_object_exprt &>(expr);
  }
};

const dynamic_object_exprt &to_dynamic_object_expr(const exprt &expr);
dynamic_object_exprt &to_dynamic_object_expr(exprt &expr);

class typecast_exprt : public exprt
{
public:
  inline explicit typecast_exprt(const typet &_type)
    : exprt(exprt::typecast, _type)
  {
    operands().resize(1);
  }

  inline typecast_exprt(const exprt &op, const typet &_type)
    : exprt(exprt::typecast, _type)
  {
    copy_to_operands(op);
  }

  inline exprt &op()
  {
    return op0();
  }

  inline const exprt &op() const
  {
    return op0();
  }
};

extern inline const typecast_exprt &to_typecast_expr(const exprt &expr)
{
  assert(expr.id() == exprt::typecast && expr.operands().size() == 1);
  return static_cast<const typecast_exprt &>(expr);
}

extern inline typecast_exprt &to_typecast_expr(exprt &expr)
{
  assert(expr.id() == exprt::typecast && expr.operands().size() == 1);
  return static_cast<typecast_exprt &>(expr);
}

class and_exprt : public exprt
{
public:
  and_exprt() : exprt(exprt::i_and, typet("bool"))
  {
  }

  and_exprt(const exprt &op0, const exprt &op1)
    : exprt(exprt::i_and, typet("bool"))
  {
    copy_to_operands(op0, op1);
  }

  and_exprt(const exprt::operandst &op) : exprt(exprt::i_and, typet("bool"))
  {
    if (op.empty())
      make_true();
    else if (op.size() == 1)
      *this = static_cast<const and_exprt &>(op.front());
    else
      operands() = op;
  }
};

class implies_exprt : public exprt
{
public:
  implies_exprt() : exprt(exprt::implies, typet("bool"))
  {
    operands().resize(2);
  }

  implies_exprt(const exprt &op0, const exprt &op1)
    : exprt(exprt::implies, typet("bool"))
  {
    copy_to_operands(op0, op1);
  }
};

class or_exprt : public exprt
{
public:
  or_exprt() : exprt(exprt::i_or, typet("bool"))
  {
  }

  or_exprt(const exprt &op0, const exprt &op1)
    : exprt(exprt::i_or, typet("bool"))
  {
    copy_to_operands(op0, op1);
  }

  or_exprt(const exprt &op0, const exprt &op1, const exprt &op2)
    : exprt(exprt::i_or, typet("bool"))
  {
    copy_to_operands(op0, op1, op2);
  }

  or_exprt(const exprt::operandst &op) : exprt(exprt::i_or, typet("bool"))
  {
    if (op.empty())
      make_false();
    else if (op.size() == 1)
      *this = static_cast<const or_exprt &>(op.front());
    else
      operands() = op;
  }
};

class address_of_exprt : public exprt
{
public:
  explicit address_of_exprt(const exprt &op)
    : exprt(exprt::addrof, typet("pointer"))
  {
    assert(!op.is_constant());
    assert(op.id() != exprt::addrof);
    type().subtype() = op.type();
    copy_to_operands(op);
  }

  explicit address_of_exprt() : exprt(exprt::addrof, typet("pointer"))
  {
    operands().resize(1);
  }

  exprt &object()
  {
    return op0();
  }

  const exprt &object() const
  {
    return op0();
  }
};

extern inline const address_of_exprt &to_address_of_expr(const exprt &expr)
{
  assert(expr.id() == exprt::addrof && expr.operands().size() == 1);
  return static_cast<const address_of_exprt &>(expr);
}

extern inline address_of_exprt &to_address_of_expr(exprt &expr)
{
  assert(expr.id() == exprt::addrof && expr.operands().size() == 1);
  return static_cast<address_of_exprt &>(expr);
}

class not_exprt : public exprt
{
public:
  explicit not_exprt(const exprt &op) : exprt(exprt::i_not, typet("bool"))
  {
    copy_to_operands(op);
  }

  not_exprt() : exprt(exprt::i_not, typet("bool"))
  {
    operands().resize(1);
  }
};

class dereference_exprt : public exprt
{
public:
  explicit dereference_exprt(const typet &type) : exprt(exprt::deref, type)
  {
    operands().resize(1);
  }

  explicit dereference_exprt(const exprt &op, const typet &tp)
    : exprt(exprt::deref, tp.subtype())
  {
    // This constructor copies an exprt to op0.
    // It uses typet's subtype.
    copy_to_operands(op);
  }

  dereference_exprt() : exprt(exprt::deref)
  {
    operands().resize(1);
  }
};

class if_exprt : public exprt
{
public:
  if_exprt(const exprt &cond, const exprt &t, const exprt &f)
    : exprt(exprt::i_if)
  {
    copy_to_operands(cond, t, f);
    type() = t.type();
  }

  if_exprt() : exprt(exprt::i_if)
  {
    operands().resize(3);
  }

  exprt &cond()
  {
    return op0();
  }

  const exprt &cond() const
  {
    return op0();
  }

  exprt &true_case()
  {
    return op1();
  }

  const exprt &true_case() const
  {
    return op1();
  }

  exprt &false_case()
  {
    return op2();
  }

  const exprt &false_case() const
  {
    return op2();
  }
};

extern inline const if_exprt &to_if_expr(const exprt &expr)
{
  assert(expr.id() == exprt::i_if && expr.operands().size() == 3);
  return static_cast<const if_exprt &>(expr);
}

extern inline if_exprt &to_if_expr(exprt &expr)
{
  assert(expr.id() == exprt::i_if && expr.operands().size() == 3);
  return static_cast<if_exprt &>(expr);
}

class with_exprt : public exprt
{
public:
  with_exprt(const exprt &_old, const exprt &_where, const exprt &_new_value)
    : exprt(exprt::with)
  {
    copy_to_operands(_old, _where, _new_value);
    type() = _old.type();
  }

  with_exprt() : exprt(exprt::with)
  {
    operands().resize(3);
  }

  exprt &old()
  {
    return op0();
  }

  const exprt &old() const
  {
    return op0();
  }

  exprt &where()
  {
    return op1();
  }

  const exprt &where() const
  {
    return op1();
  }

  exprt &new_value()
  {
    return op2();
  }

  const exprt &new_value() const
  {
    return op2();
  }
};

extern inline const with_exprt &to_with_expr(const exprt &expr)
{
  assert(expr.id() == exprt::with && expr.operands().size() == 3);
  return static_cast<const with_exprt &>(expr);
}

extern inline with_exprt &to_with_expr(exprt &expr)
{
  assert(expr.id() == exprt::with && expr.operands().size() == 3);
  return static_cast<with_exprt &>(expr);
}

class member_exprt : public exprt
{
public:
  explicit member_exprt(const exprt &op) : exprt(exprt::member)
  {
    copy_to_operands(op);
  }

  explicit member_exprt(const typet &type) : exprt(exprt::member, type)
  {
    operands().resize(1);
  }

  member_exprt(const exprt &op, const irep_idt &component_name)
    : exprt(exprt::member)
  {
    copy_to_operands(op);
    set_component_name(component_name);
  }

  inline member_exprt(
    const exprt &op,
    const irep_idt &component_name,
    const typet &_type)
    : exprt(exprt::member, _type)
  {
    copy_to_operands(op);
    set_component_name(component_name);
  }

  inline member_exprt(const irep_idt &component_name, const typet &_type)
    : exprt(exprt::member, _type)
  {
    set_component_name(component_name);
  }

  member_exprt() : exprt(exprt::member)
  {
    operands().resize(1);
  }

  irep_idt get_component_name() const
  {
    return get("component_name");
  }

  void set_component_name(const irep_idt &component_name)
  {
    this->component_name(component_name);
  }

  inline const exprt &struct_op() const
  {
    return op0();
  }

  inline exprt &struct_op()
  {
    return op0();
  }
};

inline const member_exprt &to_member_expr(const exprt &expr)
{
  assert(expr.id() == exprt::member);
  return static_cast<const member_exprt &>(expr);
}

inline member_exprt &to_member_expr(exprt &expr)
{
  assert(expr.id() == exprt::member);
  return static_cast<member_exprt &>(expr);
}

class type_exprt : public exprt
{
public:
  type_exprt() : exprt(exprt::i_type)
  {
  }

  explicit type_exprt(const typet &type) : exprt(exprt::i_type, type)
  {
  }
};

class constant_exprt : public exprt
{
public:
  inline constant_exprt() : exprt(exprt::constant)
  {
  }

  inline explicit constant_exprt(const typet &type)
    : exprt(exprt::constant, type)
  {
  }

  inline constant_exprt(
    const irep_idt &_value,
    const irep_idt &_cformat,
    const typet &_type)
    : exprt(exprt::constant, _type)
  {
    set("#cformat", _cformat);
    set_value(_value);
  }

  constant_exprt(const BigInt &value, const typet &type)
    : constant_exprt(
        integer2binary(value, bv_width(type)),
        integer2string(value),
        type)
  {
  }

  inline const irep_idt &get_value() const
  {
    return get("value");
  }

  inline void set_value(const irep_idt &value)
  {
    set("value", value);
  }
};

/*! \brief Cast a generic exprt to a \ref constant_exprt
 *
 * This is an unchecked conversion. \a expr must be known to be \ref
 * constant_exprt.
 *
 * \param expr Source expression
 * \return Object of type \ref constant_exprt
 *
 * \ingroup gr_std_expr
*/
inline const constant_exprt &to_constant_expr(const exprt &expr)
{
  assert(expr.id() == exprt::constant);
  return static_cast<const constant_exprt &>(expr);
}

/*! \copydoc to_constant_expr(const exprt &)
 * \ingroup gr_std_expr
*/
inline constant_exprt &to_constant_expr(exprt &expr)
{
  assert(expr.id() == exprt::constant);
  return static_cast<constant_exprt &>(expr);
}

/*! \brief The boolean constant true
*/
class true_exprt : public constant_exprt
{
public:
  true_exprt() : constant_exprt(typet("bool"))
  {
    set_value(exprt::i_true);
  }
};

class false_exprt : public constant_exprt
{
public:
  false_exprt() : constant_exprt(typet("bool"))
  {
    set_value(exprt::i_false);
  }
};

class nil_exprt : public exprt
{
public:
  nil_exprt() : exprt(static_cast<const exprt &>(get_nil_irep()))
  {
  }
};

#endif
