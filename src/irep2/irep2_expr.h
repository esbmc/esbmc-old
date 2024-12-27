#ifndef IREP2_EXPR_H_
#define IREP2_EXPR_H_

#include <util/config.h>
#include <util/c_types.h>
#include <util/fixedbv.h>
#include <util/ieee_float.h>
#include <irep2/irep2_type.h>

// So - make some type definitions for the different types we're going to be
// working with. This is to avoid the repeated use of template names in later
// definitions. If you'd like to add another type - don't. Vast tracts of code
// only expect the types below, it's be extremely difficult to hack new ones in.

// Start of definitions for expressions. Forward decs,

// Iterate, in the preprocessor, over all expr ids and produce a forward
// class declaration for them
#define _ESBMC_IREP2_FWD_DEC(r, data, elem) class BOOST_PP_CAT(elem, 2t);
BOOST_PP_LIST_FOR_EACH(_ESBMC_IREP2_FWD_DEC, foo, ESBMC_LIST_OF_EXPRS)

// Data definitions.

class constant2t : public expr2t
{
public:
  constant2t(const type2tc &t, expr2t::expr_ids id) : expr2t(t, id)
  {
  }
  constant2t(const constant2t &ref) = default;
};

class constant_int_data : public constant2t
{
public:
  constant_int_data(const type2tc &t, expr2t::expr_ids id, const BigInt &bint)
    : constant2t(t, id), value(bint)
  {
  }
  constant_int_data(const constant_int_data &ref) = default;

  BigInt value;

  // Type mangling:
  typedef esbmct::
    field_traits<BigInt, constant_int_data, &constant_int_data::value>
      value_field;
  typedef esbmct::expr2t_traits<value_field> traits;
};

class constant_fixedbv_data : public constant2t
{
public:
  constant_fixedbv_data(
    const type2tc &t,
    expr2t::expr_ids id,
    const fixedbvt &fbv)
    : constant2t(t, id), value(std::move(fbv))
  {
  }
  constant_fixedbv_data(const constant_fixedbv_data &ref) = default;

  fixedbvt value;

  // Type mangling:
  typedef esbmct::
    field_traits<fixedbvt, constant_fixedbv_data, &constant_fixedbv_data::value>
      value_field;
  typedef esbmct::expr2t_traits<value_field> traits;
};

class constant_floatbv_data : public constant2t
{
public:
  constant_floatbv_data(
    const type2tc &t,
    expr2t::expr_ids id,
    const ieee_floatt &ieeebv)
    : constant2t(t, id), value(std::move(ieeebv))
  {
  }
  constant_floatbv_data(const constant_floatbv_data &ref) = default;

  ieee_floatt value;

  // Type mangling:
  typedef esbmct::field_traits<
    ieee_floatt,
    constant_floatbv_data,
    &constant_floatbv_data::value>
    value_field;
  typedef esbmct::expr2t_traits<value_field> traits;
};

class constant_datatype_data : public constant2t
{
public:
  constant_datatype_data(
    const type2tc &t,
    expr2t::expr_ids id,
    std::vector<expr2tc> m)
    : constant2t(t, id), datatype_members(std::move(m))
  {
  }
  constant_datatype_data(const constant_datatype_data &ref) = default;

  std::vector<expr2tc> datatype_members;

  // Type mangling:
  typedef esbmct::field_traits<
    std::vector<expr2tc>,
    constant_datatype_data,
    &constant_datatype_data::datatype_members>
    datatype_members_field;
  typedef esbmct::expr2t_traits<datatype_members_field> traits;
};

class constant_union_data : public constant_datatype_data
{
public:
  constant_union_data(
    const type2tc &t,
    expr2t::expr_ids id,
    irep_idt init_field,
    std::vector<expr2tc> m)
    : constant_datatype_data(t, id, std::move(m)), init_field(init_field)
  {
  }
  constant_union_data(const constant_union_data &) = default;

  irep_idt init_field;

  // Type mangling:
  typedef esbmct::field_traits<
    irep_idt,
    constant_union_data,
    &constant_union_data::init_field>
    init_field_field;
  typedef esbmct::expr2t_traits<datatype_members_field, init_field_field>
    traits;
};

class constant_bool_data : public constant2t
{
public:
  constant_bool_data(const type2tc &t, expr2t::expr_ids id, bool value)
    : constant2t(t, id), value(value)
  {
  }
  constant_bool_data(const constant_bool_data &ref) = default;

  bool value;

  // Type mangling:
  typedef esbmct::
    field_traits<bool, constant_bool_data, &constant_bool_data::value>
      value_field;
  typedef esbmct::expr2t_traits<value_field> traits;
};

class constant_array_of_data : public constant2t
{
public:
  constant_array_of_data(
    const type2tc &t,
    expr2t::expr_ids id,
    const expr2tc &value)
    : constant2t(t, id), initializer(value)
  {
  }
  constant_array_of_data(const constant_array_of_data &ref) = default;

  expr2tc initializer;

  // Type mangling:
  typedef esbmct::field_traits<
    expr2tc,
    constant_array_of_data,
    &constant_array_of_data::initializer>
    initializer_field;
  typedef esbmct::expr2t_traits<initializer_field> traits;
};

class constant_string_data : public constant2t
{
public:
  enum kindt
  {
    DEFAULT, /* "" */
    WIDE,    /* L"" */
    UNICODE, /* u8"", u"" and U"" */
  };

  constant_string_data(
    const type2tc &t,
    expr2t::expr_ids id,
    const irep_idt &v,
    kindt kind)
    : constant2t(t, id), value(v), kind(kind)
  {
  }
  constant_string_data(const constant_string_data &ref) = default;

  irep_idt value;
  kindt kind;

  // Type mangling:
  typedef esbmct::
    field_traits<irep_idt, constant_string_data, &constant_string_data::value>
      value_field;
  typedef esbmct::
    field_traits<kindt, constant_string_data, &constant_string_data::kind>
      kind_field;
  typedef esbmct::expr2t_traits<value_field, kind_field> traits;
};

class symbol_data : public expr2t
{
public:
  enum renaming_level
  {
    level0,
    level1,
    level2,
    level1_global,
    level2_global
  };

  symbol_data(
    const type2tc &t,
    expr2t::expr_ids id,
    const irep_idt &v,
    renaming_level lev,
    unsigned int l1,
    unsigned int l2,
    unsigned int tr,
    unsigned int node)
    : expr2t(t, id),
      thename(v),
      rlevel(lev),
      level1_num(l1),
      level2_num(l2),
      thread_num(tr),
      node_num(node)
  {
  }
  symbol_data(const symbol_data &ref) = default;

  virtual std::string get_symbol_name() const;

  // So: I want to make this private, however then all the templates accessing
  // it can't access it; and the typedef for symbol_expr_methods further down
  // can't access it too, no matter how many friends I add.
  irep_idt thename;
  renaming_level rlevel;
  unsigned int level1_num; // Function activation record
  unsigned int level2_num; // SSA variable number
  unsigned int thread_num;
  unsigned int node_num;

  // Type mangling:
  typedef esbmct::field_traits<irep_idt, symbol_data, &symbol_data::thename>
    thename_field;
  typedef esbmct::
    field_traits<renaming_level, symbol_data, &symbol_data::rlevel>
      rlevel_field;
  typedef esbmct::
    field_traits<unsigned int, symbol_data, &symbol_data::level1_num>
      level1_num_field;
  typedef esbmct::
    field_traits<unsigned int, symbol_data, &symbol_data::level2_num>
      level2_num_field;
  typedef esbmct::
    field_traits<unsigned int, symbol_data, &symbol_data::thread_num>
      thread_num_field;
  typedef esbmct::
    field_traits<unsigned int, symbol_data, &symbol_data::node_num>
      node_num_field;
  typedef esbmct::expr2t_traits<
    thename_field,
    rlevel_field,
    level1_num_field,
    level2_num_field,
    thread_num_field,
    node_num_field>
    traits;
};

class typecast_data : public expr2t
{
public:
  typecast_data(
    const type2tc &t,
    expr2t::expr_ids id,
    const expr2tc &v,
    const expr2tc &r)
    : expr2t(t, id), from(v), rounding_mode(r)
  {
  }
  typecast_data(const typecast_data &ref) = default;

  expr2tc from;
  expr2tc rounding_mode;

  // Type mangling:
  typedef esbmct::field_traits<expr2tc, typecast_data, &typecast_data::from>
    from_field;
  typedef esbmct::
    field_traits<expr2tc, typecast_data, &typecast_data::rounding_mode>
      rounding_mode_field;
  typedef esbmct::expr2t_traits<from_field, rounding_mode_field> traits;
};

class bitcast_data : public expr2t
{
public:
  bitcast_data(const type2tc &t, expr2t::expr_ids id, const expr2tc &v)
    : expr2t(t, id), from(v)
  {
  }
  bitcast_data(const bitcast_data &ref) = default;

  expr2tc from;

  // Type mangling:
  typedef esbmct::field_traits<expr2tc, bitcast_data, &bitcast_data::from>
    from_field;
  typedef esbmct::expr2t_traits<from_field> traits;
};

class if_data : public expr2t
{
public:
  if_data(
    const type2tc &t,
    expr2t::expr_ids id,
    const expr2tc &c,
    const expr2tc &tv,
    const expr2tc &fv)
    : expr2t(t, id), cond(c), true_value(tv), false_value(fv)
  {
  }
  if_data(const if_data &ref) = default;

  expr2tc cond;
  expr2tc true_value;
  expr2tc false_value;

  // Type mangling:
  typedef esbmct::field_traits<expr2tc, if_data, &if_data::cond> cond_field;
  typedef esbmct::field_traits<expr2tc, if_data, &if_data::true_value>
    true_value_field;
  typedef esbmct::field_traits<expr2tc, if_data, &if_data::false_value>
    false_value_field;
  typedef esbmct::expr2t_traits<cond_field, true_value_field, false_value_field>
    traits;
};

class relation_data : public expr2t
{
public:
  relation_data(
    const type2tc &t,
    expr2t::expr_ids id,
    const expr2tc &s1,
    const expr2tc &s2)
    : expr2t(t, id), side_1(s1), side_2(s2)
  {
  }
  relation_data(const relation_data &ref) = default;

  expr2tc side_1;
  expr2tc side_2;

  // Type mangling:
  typedef esbmct::field_traits<expr2tc, relation_data, &relation_data::side_1>
    side_1_field;
  typedef esbmct::field_traits<expr2tc, relation_data, &relation_data::side_2>
    side_2_field;
  typedef esbmct::expr2t_traits<side_1_field, side_2_field> traits;
};

class logical_ops : public expr2t
{
public:
  logical_ops(const type2tc &t, expr2t::expr_ids id) : expr2t(t, id)
  {
  }
  logical_ops(const logical_ops &ref) = default;
};

class bool_1op : public logical_ops
{
public:
  bool_1op(const type2tc &t, expr2t::expr_ids id, const expr2tc &v)
    : logical_ops(t, id), value(v)
  {
  }
  bool_1op(const bool_1op &ref) = default;

  expr2tc value;

  // Type mangling:
  typedef esbmct::field_traits<expr2tc, bool_1op, &bool_1op::value> value_field;
  typedef esbmct::expr2t_traits_notype<value_field> traits;
};

class logic_2ops : public logical_ops
{
public:
  logic_2ops(
    const type2tc &t,
    expr2t::expr_ids id,
    const expr2tc &s1,
    const expr2tc &s2)
    : logical_ops(t, id), side_1(s1), side_2(s2)
  {
  }
  logic_2ops(const logic_2ops &ref) = default;

  expr2tc side_1;
  expr2tc side_2;

  // Type mangling:
  typedef esbmct::field_traits<expr2tc, logic_2ops, &logic_2ops::side_1>
    side_1_field;
  typedef esbmct::field_traits<expr2tc, logic_2ops, &logic_2ops::side_2>
    side_2_field;
  typedef esbmct::expr2t_traits<side_1_field, side_2_field> traits;
};

class bitops : public expr2t
{
public:
  bitops(const type2tc &t, expr2t::expr_ids id) : expr2t(t, id)
  {
  }
  bitops(const bitops &ref) = default;
};

class bitnot_data : public bitops
{
public:
  bitnot_data(const type2tc &t, expr2t::expr_ids id, const expr2tc &v)
    : bitops(t, id), value(v)
  {
  }
  bitnot_data(const bitnot_data &ref) = default;

  expr2tc value;

  // Type mangling:
  typedef esbmct::field_traits<expr2tc, bitnot_data, &bitnot_data::value>
    value_field;
  typedef esbmct::expr2t_traits<value_field> traits;
};

class bit_2ops : public bitops
{
public:
  bit_2ops(
    const type2tc &t,
    expr2t::expr_ids id,
    const expr2tc &s1,
    const expr2tc &s2)
    : bitops(t, id), side_1(s1), side_2(s2)
  {
  }
  bit_2ops(const bit_2ops &ref) = default;

  expr2tc side_1;
  expr2tc side_2;

  // Type mangling:
  typedef esbmct::field_traits<expr2tc, bit_2ops, &bit_2ops::side_1>
    side_1_field;
  typedef esbmct::field_traits<expr2tc, bit_2ops, &bit_2ops::side_2>
    side_2_field;
  typedef esbmct::expr2t_traits<side_1_field, side_2_field> traits;
};

class arith_ops : public expr2t
{
public:
  arith_ops(const type2tc &t, expr2t::expr_ids id) : expr2t(t, id)
  {
  }
  arith_ops(const arith_ops &ref) = default;
};

class arith_1op : public arith_ops
{
public:
  arith_1op(const type2tc &t, arith_ops::expr_ids id, const expr2tc &v)
    : arith_ops(t, id), value(v)
  {
  }
  arith_1op(const arith_1op &ref) = default;

  expr2tc value;

  // Type mangling:
  typedef esbmct::field_traits<expr2tc, arith_1op, &arith_1op::value>
    value_field;
  typedef esbmct::expr2t_traits<value_field> traits;
};

class arith_2ops : public arith_ops
{
public:
  arith_2ops(
    const type2tc &t,
    arith_ops::expr_ids id,
    const expr2tc &v1,
    const expr2tc &v2);

  arith_2ops(const arith_2ops &ref) = default;

  expr2tc side_1;
  expr2tc side_2;

  // Type mangling:
  typedef esbmct::field_traits<expr2tc, arith_2ops, &arith_2ops::side_1>
    side_1_field;
  typedef esbmct::field_traits<expr2tc, arith_2ops, &arith_2ops::side_2>
    side_2_field;
  typedef esbmct::expr2t_traits<side_1_field, side_2_field> traits;
};

class ieee_arith_1op : public arith_ops
{
public:
  ieee_arith_1op(
    const type2tc &t,
    arith_ops::expr_ids id,
    const expr2tc &v,
    const expr2tc &rm)
    : arith_ops(t, id), rounding_mode(rm), value(v)
  {
  }
  ieee_arith_1op(const ieee_arith_1op &ref) = default;

  expr2tc rounding_mode;
  expr2tc value;

  // Type mangling:
  typedef esbmct::
    field_traits<expr2tc, ieee_arith_1op, &ieee_arith_1op::rounding_mode>
      rounding_mode_field;
  typedef esbmct::field_traits<expr2tc, ieee_arith_1op, &ieee_arith_1op::value>
    value_field;
  typedef esbmct::expr2t_traits<rounding_mode_field, value_field> traits;
};

class ieee_arith_2ops : public arith_ops
{
public:
  ieee_arith_2ops(
    const type2tc &t,
    arith_ops::expr_ids id,
    const expr2tc &v1,
    const expr2tc &v2,
    const expr2tc &rm)
    : arith_ops(t, id), rounding_mode(rm), side_1(v1), side_2(v2)
  {
  }
  ieee_arith_2ops(const ieee_arith_2ops &ref) = default;

  expr2tc rounding_mode;
  expr2tc side_1;
  expr2tc side_2;

  // Type mangling:
  typedef esbmct::
    field_traits<expr2tc, ieee_arith_2ops, &ieee_arith_2ops::rounding_mode>
      rounding_mode_field;
  typedef esbmct::
    field_traits<expr2tc, ieee_arith_2ops, &ieee_arith_2ops::side_1>
      side_1_field;
  typedef esbmct::
    field_traits<expr2tc, ieee_arith_2ops, &ieee_arith_2ops::side_2>
      side_2_field;
  typedef esbmct::expr2t_traits<rounding_mode_field, side_1_field, side_2_field>
    traits;
};

class ieee_arith_3ops : public arith_ops
{
public:
  ieee_arith_3ops(
    const type2tc &t,
    arith_ops::expr_ids id,
    const expr2tc &v1,
    const expr2tc &v2,
    const expr2tc &v3,
    const expr2tc &rm)
    : arith_ops(t, id), rounding_mode(rm), value_1(v1), value_2(v2), value_3(v3)
  {
  }
  ieee_arith_3ops(const ieee_arith_3ops &ref) = default;

  expr2tc rounding_mode;
  expr2tc value_1;
  expr2tc value_2;
  expr2tc value_3;

  // Type mangling:
  typedef esbmct::
    field_traits<expr2tc, ieee_arith_3ops, &ieee_arith_3ops::rounding_mode>
      rounding_mode_field;
  typedef esbmct::
    field_traits<expr2tc, ieee_arith_3ops, &ieee_arith_3ops::value_1>
      value_1_field;
  typedef esbmct::
    field_traits<expr2tc, ieee_arith_3ops, &ieee_arith_3ops::value_2>
      value_2_field;
  typedef esbmct::
    field_traits<expr2tc, ieee_arith_3ops, &ieee_arith_3ops::value_3>
      value_3_field;
  typedef esbmct::expr2t_traits<
    rounding_mode_field,
    value_1_field,
    value_2_field,
    value_3_field>
    traits;
};

class same_object_data : public expr2t
{
public:
  same_object_data(
    const type2tc &t,
    expr2t::expr_ids id,
    const expr2tc &v1,
    const expr2tc &v2)
    : expr2t(t, id), side_1(v1), side_2(v2)
  {
  }
  same_object_data(const same_object_data &ref) = default;

  expr2tc side_1;
  expr2tc side_2;

  // Type mangling:
  typedef esbmct::
    field_traits<expr2tc, same_object_data, &same_object_data::side_1>
      side_1_field;
  typedef esbmct::
    field_traits<expr2tc, same_object_data, &same_object_data::side_2>
      side_2_field;
  typedef esbmct::expr2t_traits<side_1_field, side_2_field> traits;
};

class pointer_ops : public expr2t
{
public:
  pointer_ops(const type2tc &t, expr2t::expr_ids id, const expr2tc &p)
    : expr2t(t, id), ptr_obj(p)
  {
  }
  pointer_ops(const pointer_ops &ref) = default;

  expr2tc ptr_obj;

  // Type mangling:
  typedef esbmct::field_traits<expr2tc, pointer_ops, &pointer_ops::ptr_obj>
    ptr_obj_field;
  typedef esbmct::expr2t_traits<ptr_obj_field> traits;
};

// Special class for invalid_pointer2t, which needs always-construct forcing
class invalid_pointer_ops : public pointer_ops
{
public:
  // Forward constructors downwards
  invalid_pointer_ops(const type2tc &t, expr2t::expr_ids id, const expr2tc &p)
    : pointer_ops(t, id, p)
  {
  }
  invalid_pointer_ops(const invalid_pointer_ops &ref) = default;

  // Type mangling:
  typedef esbmct::expr2t_traits_notype<ptr_obj_field> traits;
};

class byte_ops : public expr2t
{
public:
  byte_ops(const type2tc &t, expr2t::expr_ids id) : expr2t(t, id)
  {
  }
  byte_ops(const byte_ops &ref) = default;
};

class byte_extract_data : public byte_ops
{
public:
  byte_extract_data(
    const type2tc &t,
    expr2t::expr_ids id,
    const expr2tc &s,
    const expr2tc &o,
    bool be)
    : byte_ops(t, id), source_value(s), source_offset(o), big_endian(be)
  {
  }
  byte_extract_data(const byte_extract_data &ref) = default;

  expr2tc source_value;
  expr2tc source_offset;
  bool big_endian;

  // Type mangling:
  typedef esbmct::
    field_traits<expr2tc, byte_extract_data, &byte_extract_data::source_value>
      source_value_field;
  typedef esbmct::
    field_traits<expr2tc, byte_extract_data, &byte_extract_data::source_offset>
      source_offset_field;
  typedef esbmct::
    field_traits<bool, byte_extract_data, &byte_extract_data::big_endian>
      big_endian_field;
  typedef esbmct::
    expr2t_traits<source_value_field, source_offset_field, big_endian_field>
      traits;
};

class byte_update_data : public byte_ops
{
public:
  byte_update_data(
    const type2tc &t,
    expr2t::expr_ids id,
    const expr2tc &s,
    const expr2tc &o,
    const expr2tc &v,
    bool be)
    : byte_ops(t, id),
      source_value(s),
      source_offset(o),
      update_value(v),
      big_endian(be)
  {
  }
  byte_update_data(const byte_update_data &ref) = default;

  expr2tc source_value;
  expr2tc source_offset;
  expr2tc update_value;
  bool big_endian;

  // Type mangling:
  typedef esbmct::
    field_traits<expr2tc, byte_update_data, &byte_update_data::source_value>
      source_value_field;
  typedef esbmct::
    field_traits<expr2tc, byte_update_data, &byte_update_data::source_offset>
      source_offset_field;
  typedef esbmct::
    field_traits<expr2tc, byte_update_data, &byte_update_data::update_value>
      update_value_field;
  typedef esbmct::
    field_traits<bool, byte_update_data, &byte_update_data::big_endian>
      big_endian_field;
  typedef esbmct::expr2t_traits<
    source_value_field,
    source_offset_field,
    update_value_field,
    big_endian_field>
    traits;
};

class datatype_ops : public expr2t
{
public:
  datatype_ops(const type2tc &t, expr2t::expr_ids id) : expr2t(t, id)
  {
  }
  datatype_ops(const datatype_ops &ref) = default;
};

class with_data : public datatype_ops
{
public:
  with_data(
    const type2tc &t,
    datatype_ops::expr_ids id,
    const expr2tc &sv,
    const expr2tc &uf,
    const expr2tc &uv)
    : datatype_ops(t, id), source_value(sv), update_field(uf), update_value(uv)
  {
  }
  with_data(const with_data &ref) = default;

  expr2tc source_value;
  expr2tc update_field;
  expr2tc update_value;

  // Type mangling:
  typedef esbmct::field_traits<expr2tc, with_data, &with_data::source_value>
    source_value_field;
  typedef esbmct::field_traits<expr2tc, with_data, &with_data::update_field>
    update_field_field;
  typedef esbmct::field_traits<expr2tc, with_data, &with_data::update_value>
    update_value_field;
  typedef esbmct::
    expr2t_traits<source_value_field, update_field_field, update_value_field>
      traits;
};

class member_data : public datatype_ops
{
public:
  member_data(
    const type2tc &t,
    datatype_ops::expr_ids id,
    const expr2tc &sv,
    const irep_idt &m)
    : datatype_ops(t, id), source_value(sv), member(m)
  {
  }
  member_data(const member_data &ref) = default;

  expr2tc source_value;
  irep_idt member;

  // Type mangling:
  typedef esbmct::field_traits<expr2tc, member_data, &member_data::source_value>
    source_value_field;
  typedef esbmct::field_traits<irep_idt, member_data, &member_data::member>
    member_field;
  typedef esbmct::expr2t_traits<source_value_field, member_field> traits;
};

class index_data : public datatype_ops
{
public:
  index_data(
    const type2tc &t,
    datatype_ops::expr_ids id,
    const expr2tc &sv,
    const expr2tc &i)
    : datatype_ops(t, id), source_value(sv), index(i)
  {
  }
  index_data(const index_data &ref) = default;

  expr2tc source_value;
  expr2tc index;

  // Type mangling:
  typedef esbmct::field_traits<expr2tc, index_data, &index_data::source_value>
    source_value_field;
  typedef esbmct::field_traits<expr2tc, index_data, &index_data::index>
    index_field;
  typedef esbmct::expr2t_traits<source_value_field, index_field> traits;
};

class string_ops : public expr2t
{
public:
  string_ops(const type2tc &t, datatype_ops::expr_ids id, const expr2tc &s)
    : expr2t(t, id), string(s)
  {
  }
  string_ops(const string_ops &ref) = default;

  expr2tc string;

  // Type mangling:
  typedef esbmct::field_traits<expr2tc, string_ops, &string_ops::string>
    string_field;
  typedef esbmct::expr2t_traits<string_field> traits;
};

class overflow_ops : public expr2t
{
public:
  overflow_ops(const type2tc &t, datatype_ops::expr_ids id, const expr2tc &v)
    : expr2t(t, id), operand(v)
  {
  }
  overflow_ops(const overflow_ops &ref) = default;

  expr2tc operand;

  // Type mangling:
  typedef esbmct::field_traits<expr2tc, overflow_ops, &overflow_ops::operand>
    operand_field;
  typedef esbmct::expr2t_traits<operand_field> traits;
};

class overflow_cast_data : public overflow_ops
{
public:
  overflow_cast_data(
    const type2tc &t,
    datatype_ops::expr_ids id,
    const expr2tc &v,
    unsigned int b)
    : overflow_ops(t, id, v), bits(b)
  {
  }
  overflow_cast_data(const overflow_cast_data &ref) = default;

  unsigned int bits;

  // Type mangling:
  typedef esbmct::field_traits<expr2tc, overflow_ops, &overflow_ops::operand>
    operand_field;
  typedef esbmct::
    field_traits<unsigned int, overflow_cast_data, &overflow_cast_data::bits>
      bits_field;
  typedef esbmct::expr2t_traits<operand_field, bits_field> traits;
};

class dynamic_object_data : public expr2t
{
public:
  dynamic_object_data(
    const type2tc &t,
    expr2t::expr_ids id,
    const expr2tc &i,
    bool inv,
    bool unk)
    : expr2t(t, id), instance(i), invalid(inv), unknown(unk)
  {
  }
  dynamic_object_data(const dynamic_object_data &ref) = default;

  expr2tc instance;
  bool invalid;
  bool unknown;

  // Type mangling:
  typedef esbmct::
    field_traits<expr2tc, dynamic_object_data, &dynamic_object_data::instance>
      instance_field;
  typedef esbmct::
    field_traits<bool, dynamic_object_data, &dynamic_object_data::invalid>
      invalid_field;
  typedef esbmct::
    field_traits<bool, dynamic_object_data, &dynamic_object_data::unknown>
      unknown_field;
  typedef esbmct::expr2t_traits<instance_field, invalid_field, unknown_field>
    traits;
};

class dereference_data : public expr2t
{
public:
  dereference_data(const type2tc &t, expr2t::expr_ids id, const expr2tc &v)
    : expr2t(t, id), value(v)
  {
  }
  dereference_data(const dereference_data &ref) = default;

  expr2tc value;

  // Type mangling:
  typedef esbmct::
    field_traits<expr2tc, dereference_data, &dereference_data::value>
      value_field;
  typedef esbmct::expr2t_traits<value_field> traits;
};

class object_ops : public expr2t
{
public:
  object_ops(const type2tc &t, expr2t::expr_ids id, const expr2tc &v)
    : expr2t(t, id), value(v)
  {
  }
  object_ops(const object_ops &ref) = default;

  expr2tc value;

  // Type mangling:
  typedef esbmct::field_traits<expr2tc, object_ops, &object_ops::value>
    value_field;
  typedef esbmct::expr2t_traits_notype<value_field> traits;
};

class sideeffect_data : public expr2t
{
public:
  /** Enumeration identifying each particular kind of side effect. The values
   *  themselves are entirely self explanatory. */
  enum allockind
  {
    malloc,
    realloc,
    alloca,
    cpp_new,
    cpp_new_arr,
    nondet,
    va_arg,
    printf2,
    function_call,
    preincrement,
    postincrement,
    predecrement,
    postdecrement
  };

  sideeffect_data(
    const type2tc &t,
    expr2t::expr_ids id,
    const expr2tc &op,
    const expr2tc &sz,
    std::vector<expr2tc> args,
    const type2tc &tp,
    allockind k)
    : expr2t(t, id),
      operand(op),
      size(sz),
      arguments(std::move(args)),
      alloctype(tp),
      kind(k)
  {
  }
  sideeffect_data(const sideeffect_data &ref) = default;

  expr2tc operand;
  expr2tc size;
  std::vector<expr2tc> arguments;
  type2tc alloctype;
  allockind kind;

  // Type mangling:
  typedef esbmct::
    field_traits<expr2tc, sideeffect_data, &sideeffect_data::operand>
      operand_field;
  typedef esbmct::field_traits<expr2tc, sideeffect_data, &sideeffect_data::size>
    size_field;
  typedef esbmct::field_traits<
    std::vector<expr2tc>,
    sideeffect_data,
    &sideeffect_data::arguments>
    arguments_field;
  typedef esbmct::
    field_traits<type2tc, sideeffect_data, &sideeffect_data::alloctype>
      alloctype_field;
  typedef esbmct::
    field_traits<allockind, sideeffect_data, &sideeffect_data::kind>
      kind_field;
  typedef esbmct::expr2t_traits<
    operand_field,
    size_field,
    arguments_field,
    alloctype_field,
    kind_field>
    traits;
};

class code_base : public expr2t
{
public:
  code_base(const type2tc &t, expr2t::expr_ids id) : expr2t(t, id)
  {
  }
  code_base(const code_base &ref) = default;
};

class code_block_data : public code_base
{
public:
  code_block_data(const type2tc &t, expr2t::expr_ids id, std::vector<expr2tc> v)
    : code_base(t, id), operands(std::move(v))
  {
  }
  code_block_data(const code_block_data &ref) = default;

  std::vector<expr2tc> operands;

  // Type mangling:
  typedef esbmct::field_traits<
    std::vector<expr2tc>,
    code_block_data,
    &code_block_data::operands>
    operands_field;
  typedef esbmct::expr2t_traits<operands_field> traits;
};

class code_assign_data : public code_base
{
public:
  code_assign_data(
    const type2tc &t,
    expr2t::expr_ids id,
    const expr2tc &ta,
    const expr2tc &s)
    : code_base(t, id), target(ta), source(s)
  {
  }
  code_assign_data(const code_assign_data &ref) = default;

  expr2tc target;
  expr2tc source;

  // Type mangling:
  typedef esbmct::
    field_traits<expr2tc, code_assign_data, &code_assign_data::target>
      target_field;
  typedef esbmct::
    field_traits<expr2tc, code_assign_data, &code_assign_data::source>
      source_field;
  typedef esbmct::expr2t_traits<target_field, source_field> traits;
};

class code_decl_data : public code_base
{
public:
  code_decl_data(const type2tc &t, expr2t::expr_ids id, const irep_idt &v)
    : code_base(t, id), value(v)
  {
  }
  code_decl_data(const code_decl_data &ref) = default;

  irep_idt value;

  // Type mangling:
  typedef esbmct::field_traits<irep_idt, code_decl_data, &code_decl_data::value>
    value_field;
  typedef esbmct::expr2t_traits<value_field> traits;
};

class code_printf_data : public code_base
{
public:
  code_printf_data(
    const type2tc &t,
    expr2t::expr_ids id,
    std::vector<expr2tc> v,
    const std::string &b)
    : code_base(t, id), operands(std::move(v)), bs_name(b)
  {
  }
  code_printf_data(const code_printf_data &ref) = default;

  std::vector<expr2tc> operands;
  // Base name
  std::string bs_name;

  // Type mangling:
  typedef esbmct::field_traits<
    std::vector<expr2tc>,
    code_printf_data,
    &code_printf_data::operands>
    operands_field;
  typedef esbmct::expr2t_traits<operands_field> traits;
};

class code_expression_data : public code_base
{
public:
  code_expression_data(const type2tc &t, expr2t::expr_ids id, const expr2tc &o)
    : code_base(t, id), operand(o)
  {
  }
  code_expression_data(const code_expression_data &ref) = default;

  expr2tc operand;

  // Type mangling:
  typedef esbmct::
    field_traits<expr2tc, code_expression_data, &code_expression_data::operand>
      operand_field;
  typedef esbmct::expr2t_traits_notype<operand_field> traits;
};

class code_goto_data : public code_base
{
public:
  code_goto_data(const type2tc &t, expr2t::expr_ids id, const irep_idt &tg)
    : code_base(t, id), target(tg)
  {
  }
  code_goto_data(const code_goto_data &ref) = default;

  irep_idt target;

  // Type mangling:
  typedef esbmct::
    field_traits<irep_idt, code_goto_data, &code_goto_data::target>
      target_field;
  typedef esbmct::expr2t_traits<target_field> traits;
};

class object_desc_data : public expr2t
{
public:
  object_desc_data(
    const type2tc &t,
    expr2t::expr_ids id,
    const expr2tc &o,
    const expr2tc &offs,
    unsigned int align)
    : expr2t(t, id), object(o), offset(offs), alignment(align)
  {
  }
  object_desc_data(const object_desc_data &ref) = default;

  expr2tc object;
  expr2tc offset;
  unsigned int alignment;

  // Type mangling:
  typedef esbmct::
    field_traits<expr2tc, object_desc_data, &object_desc_data::object>
      object_field;
  typedef esbmct::
    field_traits<expr2tc, object_desc_data, &object_desc_data::offset>
      offset_field;
  typedef esbmct::
    field_traits<unsigned int, object_desc_data, &object_desc_data::alignment>
      alignment_field;
  typedef esbmct::expr2t_traits<object_field, offset_field, alignment_field>
    traits;
};

class code_funccall_data : public code_base
{
public:
  code_funccall_data(
    const type2tc &t,
    expr2t::expr_ids id,
    const expr2tc &r,
    const expr2tc &func,
    std::vector<expr2tc> ops)
    : code_base(t, id), ret(r), function(func), operands(std::move(ops))
  {
  }
  code_funccall_data(const code_funccall_data &ref) = default;

  expr2tc ret;
  expr2tc function;
  std::vector<expr2tc> operands;

  // Type mangling:
  typedef esbmct::
    field_traits<expr2tc, code_funccall_data, &code_funccall_data::ret>
      ret_field;
  typedef esbmct::
    field_traits<expr2tc, code_funccall_data, &code_funccall_data::function>
      function_field;
  typedef esbmct::field_traits<
    std::vector<expr2tc>,
    code_funccall_data,
    &code_funccall_data::operands>
    operands_field;
  typedef esbmct::expr2t_traits<ret_field, function_field, operands_field>
    traits;
};

class code_comma_data : public code_base
{
public:
  code_comma_data(
    const type2tc &t,
    expr2t::expr_ids id,
    const expr2tc &s1,
    const expr2tc &s2)
    : code_base(t, id), side_1(s1), side_2(s2)
  {
  }
  code_comma_data(const code_comma_data &ref) = default;

  expr2tc side_1;
  expr2tc side_2;

  // Type mangling:
  typedef esbmct::
    field_traits<expr2tc, code_comma_data, &code_comma_data::side_1>
      side_1_field;
  typedef esbmct::
    field_traits<expr2tc, code_comma_data, &code_comma_data::side_2>
      side_2_field;
  typedef esbmct::expr2t_traits<side_1_field, side_2_field> traits;
};

class code_asm_data : public code_base
{
public:
  code_asm_data(const type2tc &t, expr2t::expr_ids id, const irep_idt &v)
    : code_base(t, id), value(v)
  {
  }
  code_asm_data(const code_asm_data &ref) = default;

  irep_idt value;

  // Type mangling:
  typedef esbmct::field_traits<irep_idt, code_asm_data, &code_asm_data::value>
    value_field;
  typedef esbmct::expr2t_traits<value_field> traits;
};

class code_cpp_catch_data : public code_base
{
public:
  code_cpp_catch_data(
    const type2tc &t,
    expr2t::expr_ids id,
    std::vector<irep_idt> el)
    : code_base(t, id), exception_list(std::move(el))
  {
  }
  code_cpp_catch_data(const code_cpp_catch_data &ref) = default;

  std::vector<irep_idt> exception_list;

  // Type mangling:
  typedef esbmct::field_traits<
    std::vector<irep_idt>,
    code_cpp_catch_data,
    &code_cpp_catch_data::exception_list>
    exception_list_field;
  typedef esbmct::expr2t_traits<exception_list_field> traits;
};

class code_cpp_throw_data : public code_base
{
public:
  code_cpp_throw_data(
    const type2tc &t,
    expr2t::expr_ids id,
    const expr2tc &o,
    std::vector<irep_idt> l)
    : code_base(t, id), operand(o), exception_list(std::move(l))
  {
  }
  code_cpp_throw_data(const code_cpp_throw_data &ref) = default;

  expr2tc operand;
  std::vector<irep_idt> exception_list;

  // Type mangling:
  typedef esbmct::
    field_traits<expr2tc, code_cpp_throw_data, &code_cpp_throw_data::operand>
      operand_field;
  typedef esbmct::field_traits<
    std::vector<irep_idt>,
    code_cpp_throw_data,
    &code_cpp_throw_data::exception_list>
    exception_list_field;
  typedef esbmct::expr2t_traits<operand_field, exception_list_field> traits;
};

class code_cpp_throw_decl_data : public code_base
{
public:
  code_cpp_throw_decl_data(
    const type2tc &t,
    expr2t::expr_ids id,
    std::vector<irep_idt> l)
    : code_base(t, id), exception_list(std::move(l))
  {
  }
  code_cpp_throw_decl_data(const code_cpp_throw_decl_data &ref) = default;

  std::vector<irep_idt> exception_list;

  // Type mangling:
  typedef esbmct::field_traits<
    std::vector<irep_idt>,
    code_cpp_throw_decl_data,
    &code_cpp_throw_decl_data::exception_list>
    exception_list_field;
  typedef esbmct::expr2t_traits<exception_list_field> traits;
};

class extract_data : public expr2t
{
public:
  extract_data(
    const type2tc &t,
    expr2t::expr_ids id,
    const expr2tc &_from,
    unsigned int _upper,
    unsigned int _lower)
    : expr2t(t, id), from(_from), upper(_upper), lower(_lower)
  {
  }
  extract_data(const extract_data &ref) = default;

  expr2tc from;
  unsigned int upper;
  unsigned int lower;

  // Type mangling:
  typedef esbmct::field_traits<expr2tc, extract_data, &extract_data::from>
    from_field;
  typedef esbmct::field_traits<unsigned int, extract_data, &extract_data::upper>
    upper_field;
  typedef esbmct::field_traits<unsigned int, extract_data, &extract_data::lower>
    lower_field;
  typedef esbmct::expr2t_traits<from_field, upper_field, lower_field> traits;
};

// Give everything a typedef name. Use this to construct both the templated
// expression methods, but also the container class which needs the template
// parameters too.
// Given how otherwise this means typing a large amount of template arguments
// again and again, this gets macro'd.

#define irep_typedefs(basename, superclass)                                    \
  template <typename... Args>                                                  \
  inline expr2tc basename##2tc(Args && ...args)                                \
  {                                                                            \
    return expr2tc(std::static_pointer_cast<expr2t>(                           \
      std::make_shared<basename##2t>(std::forward<Args>(args)...)));           \
  }                                                                            \
  typedef esbmct::expr_methods2<basename##2t, superclass, superclass::traits>  \
    basename##_expr_methods;                                                   \
  extern template class esbmct::                                               \
    expr_methods2<basename##2t, superclass, superclass::traits>;               \
  extern template class esbmct::                                               \
    irep_methods2<basename##2t, superclass, superclass::traits>;

// This can't be replaced by iterating over all expr ids in preprocessing
// magic because the mapping between top level expr class and it's data holding
// object isn't regular: the data class depends on /what/ the expression /is/.
irep_typedefs(constant_int, constant_int_data);
irep_typedefs(constant_fixedbv, constant_fixedbv_data);
irep_typedefs(constant_floatbv, constant_floatbv_data);
irep_typedefs(constant_struct, constant_datatype_data);
irep_typedefs(constant_union, constant_union_data);
irep_typedefs(constant_array, constant_datatype_data);
irep_typedefs(constant_vector, constant_datatype_data);
irep_typedefs(constant_bool, constant_bool_data);
irep_typedefs(constant_array_of, constant_array_of_data);
irep_typedefs(constant_string, constant_string_data);
irep_typedefs(symbol, symbol_data);
irep_typedefs(nearbyint, typecast_data);
irep_typedefs(typecast, typecast_data);
irep_typedefs(bitcast, bitcast_data);
irep_typedefs(if, if_data);
irep_typedefs(equality, relation_data);
irep_typedefs(notequal, relation_data);
irep_typedefs(lessthan, relation_data);
irep_typedefs(greaterthan, relation_data);
irep_typedefs(lessthanequal, relation_data);
irep_typedefs(greaterthanequal, relation_data);
irep_typedefs(not, bool_1op);
irep_typedefs(and, logic_2ops);
irep_typedefs(or, logic_2ops);
irep_typedefs(xor, logic_2ops);
irep_typedefs(implies, logic_2ops);
irep_typedefs(bitand, bit_2ops);
irep_typedefs(bitor, bit_2ops);
irep_typedefs(bitxor, bit_2ops);
irep_typedefs(bitnand, bit_2ops);
irep_typedefs(bitnor, bit_2ops);
irep_typedefs(bitnxor, bit_2ops);
irep_typedefs(lshr, bit_2ops);
irep_typedefs(bitnot, bitnot_data);
irep_typedefs(neg, arith_1op);
irep_typedefs(abs, arith_1op);
irep_typedefs(add, arith_2ops);
irep_typedefs(sub, arith_2ops);
irep_typedefs(mul, arith_2ops);
irep_typedefs(div, arith_2ops);
irep_typedefs(ieee_add, ieee_arith_2ops);
irep_typedefs(ieee_sub, ieee_arith_2ops);
irep_typedefs(ieee_mul, ieee_arith_2ops);
irep_typedefs(ieee_div, ieee_arith_2ops);
irep_typedefs(ieee_fma, ieee_arith_3ops);
irep_typedefs(ieee_sqrt, ieee_arith_1op);
irep_typedefs(modulus, arith_2ops);
irep_typedefs(shl, bit_2ops);
irep_typedefs(ashr, bit_2ops);
irep_typedefs(same_object, same_object_data);
irep_typedefs(pointer_offset, pointer_ops);
irep_typedefs(pointer_object, pointer_ops);
irep_typedefs(pointer_capability, pointer_ops);
irep_typedefs(address_of, pointer_ops);
irep_typedefs(byte_extract, byte_extract_data);
irep_typedefs(byte_update, byte_update_data);
irep_typedefs(with, with_data);
irep_typedefs(member, member_data);
irep_typedefs(index, index_data);
irep_typedefs(isnan, bool_1op);
irep_typedefs(overflow, overflow_ops);
irep_typedefs(overflow_cast, overflow_cast_data);
irep_typedefs(overflow_neg, overflow_ops);
irep_typedefs(unknown, expr2t);
irep_typedefs(invalid, expr2t);
irep_typedefs(null_object, expr2t);
irep_typedefs(dynamic_object, dynamic_object_data);
irep_typedefs(dereference, dereference_data);
irep_typedefs(valid_object, object_ops);
irep_typedefs(deallocated_obj, object_ops);
irep_typedefs(dynamic_size, object_ops);
irep_typedefs(sideeffect, sideeffect_data);
irep_typedefs(code_block, code_block_data);
irep_typedefs(code_assign, code_assign_data);
irep_typedefs(code_init, code_assign_data);
irep_typedefs(code_decl, code_decl_data);
irep_typedefs(code_dead, code_decl_data);
irep_typedefs(code_printf, code_printf_data);
irep_typedefs(code_expression, code_expression_data);
irep_typedefs(code_return, code_expression_data);
irep_typedefs(code_skip, expr2t);
irep_typedefs(code_free, code_expression_data);
irep_typedefs(code_goto, code_goto_data);
irep_typedefs(object_descriptor, object_desc_data);
irep_typedefs(code_function_call, code_funccall_data);
irep_typedefs(code_comma, code_comma_data);
irep_typedefs(invalid_pointer, invalid_pointer_ops);
irep_typedefs(code_asm, code_asm_data);
irep_typedefs(code_cpp_del_array, code_expression_data);
irep_typedefs(code_cpp_delete, code_expression_data);
irep_typedefs(code_cpp_catch, code_cpp_catch_data);
irep_typedefs(code_cpp_throw, code_cpp_throw_data);
irep_typedefs(code_cpp_throw_decl, code_cpp_throw_decl_data);
irep_typedefs(code_cpp_throw_decl_end, code_cpp_throw_decl_data);
irep_typedefs(isinf, bool_1op);
irep_typedefs(isnormal, bool_1op);
irep_typedefs(isfinite, bool_1op);
irep_typedefs(signbit, overflow_ops);
irep_typedefs(popcount, overflow_ops);
irep_typedefs(bswap, arith_1op);
irep_typedefs(concat, bit_2ops);
irep_typedefs(extract, extract_data);

/** Constant integer class.
 *  Records a constant integer of an arbitary precision, signed or unsigned.
 *  Simplification operations will cause the integer to be clipped to whatever
 *  bit size is in expr type.
 *  @extends constant_int_data
 */
class constant_int2t : public constant_int_expr_methods
{
public:
  /** Primary constructor.
   *  @param type Type of this integer.
   *  @param input BigInt object containing the integer we're dealing with
   */
  constant_int2t(const type2tc &type, const BigInt &input)
    : constant_int_expr_methods(type, constant_int_id, input)
  {
  }
  constant_int2t(const constant_int2t &ref) = default;

  /** Accessor for fetching machine-word unsigned integer of this constant */
  unsigned long as_ulong() const;
  /** Accessor for fetching machine-word integer of this constant */
  long as_long() const;

  static std::string field_names[esbmct::num_type_fields];
};

/** Constant fixedbv class. Records a fixed-width number in what I assume
 *  to be mantissa/exponent form, but which is described throughout CBMC code
 *  as fraction/integer parts. Stored in a fixedbvt.
 *  @extends constant_fixedbv_data
 */
class constant_fixedbv2t : public constant_fixedbv_expr_methods
{
public:
  /** Primary constructor.
   *  @param type Type of this expression.
   *  @param value fixedbvt object containing number we'll be operating on
   */
  constant_fixedbv2t(const fixedbvt &value)
    : constant_fixedbv_expr_methods(
        value.spec.get_type(),
        constant_fixedbv_id,
        value)
  {
  }
  constant_fixedbv2t(const constant_fixedbv2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

/** Constant floatbv class. Records a floating-point number,
 *  Stored in a ieee_floatt.
 *  @extends constant_floatbv_data
 */
class constant_floatbv2t : public constant_floatbv_expr_methods
{
public:
  /** Primary constructor.
   *  @param type Type of this expression.
   *  @param value ieee_floatt object containing number we'll be operating on
   */
  constant_floatbv2t(const ieee_floatt &value)
    : constant_floatbv_expr_methods(
        value.spec.get_type(),
        constant_floatbv_id,
        value)
  {
  }
  constant_floatbv2t(const constant_floatbv2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

/** Constant boolean value.
 *  Contains a constant bool; rather self explanatory.
 *  @extends constant_bool_data
 */
class constant_bool2t : public constant_bool_expr_methods
{
public:
  /** Primary constructor. @param value True or false */
  constant_bool2t(bool value)
    : constant_bool_expr_methods(get_bool_type(), constant_bool_id, value)
  {
  }
  constant_bool2t(const constant_bool2t &ref) = default;

  /** Return whether contained boolean is true. */
  bool is_true() const;
  /** Return whether contained boolean is false. */
  bool is_false() const;

  static std::string field_names[esbmct::num_type_fields];
};

/** Constant class for string constants.
 *  Contains an irep_idt representing the constant string.
 *  @extends constant_string_data
 */
class constant_string2t : public constant_string_expr_methods
{
public:
  /** Primary constructor.
   *  @param type Type of this string; presumably an array_type2t.
   *  @param stringref String pool'd string we're dealing with
   *  @param kind The kind of string literal:
   *              - DEFAULT: `""`
   *              - WIDE   : `L""`
   *              - UNICODE: `u8""`, `u""` and `U""`
   */
  constant_string2t(const type2tc &type, const irep_idt &stringref, kindt kind)
    : constant_string_expr_methods(type, constant_string_id, stringref, kind)
  {
  }
  constant_string2t(const constant_string2t &ref) = default;

  /** Convert string to a constant length array of characters */
  expr2tc to_array() const;

  /**
   * sizeof(literal)/sizeof(*literal), i.e., the number of elements in the
   * underlying array, including the '\0' terminator
   */
  size_t array_size() const;

  /**
   * Extract the i-th element from the string for i between 0 and
   * to_array_type(this->type).array_size (not the same as value.c_str()[i] when
   * to_array_type(this->type).subtype != char type)
   */
  expr2tc at(size_t i) const;

  static std::string field_names[esbmct::num_type_fields];
};

/** Constant structure.
 *  Contains a vector of expressions containing each member of the struct
 *  we're dealing with, corresponding to the types and field names in the
 *  struct_type2t type.
 *  @extends constant_datatype_data
 */
class constant_struct2t : public constant_struct_expr_methods
{
public:
  /** Primary constructor.
   *  @param type Type of this structure, presumably a struct_type2t
   *  @param membrs Vector of member values that make up this struct.
   */
  constant_struct2t(const type2tc &type, const std::vector<expr2tc> &members)
    : constant_struct_expr_methods(type, constant_struct_id, members)
  {
  }
  constant_struct2t(const constant_struct2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Constant union expression.
 *  Almost the same as constant_struct2t - a vector of members corresponding
 *  to the members described in the type. However, it seems the values pumped
 *  at us by CBMC only ever have one member (at position 0) representing the
 *  most recent value written to the union.
 *  @extend constant_union_data
 */
class constant_union2t : public constant_union_expr_methods
{
public:
  /** Primary constructor.
   *  @param type Type of this structure, presumably a union_type2t
   *  @param members Vector of member values that make up this union.
   */
  constant_union2t(
    const type2tc &type,
    irep_idt init_field,
    const std::vector<expr2tc> &members)
    : constant_union_expr_methods(type, constant_union_id, init_field, members)
  {
    assert(is_union_type(type));
  }
  constant_union2t(const constant_union2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

/** Constant array.
 *  Contains a vector of array elements, pretty self explanatory. Only valid if
 *  its type has a constant sized array, can't have constant arrays of dynamic
 *  or infinitely sized arrays.
 *  @extends constant_datatype_data
 */
class constant_array2t : public constant_array_expr_methods
{
public:
  /** Primary constructor.
   *  @param type Type of this array, must be a constant sized array
   *  @param membrs Vector of elements in this array
   */
  constant_array2t(const type2tc &type, const std::vector<expr2tc> &members)
    : constant_array_expr_methods(type, constant_array_id, members)
  {
  }
  constant_array2t(const constant_array2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Constant array.
 *  Contains a vector of array elements, pretty self explanatory. Only valid if
 *  its type has a constant sized array, can't have constant arrays of dynamic
 *  or infinitely sized arrays.
 *  @extends constant_datatype_data
 */
class constant_vector2t : public constant_vector_expr_methods
{
public:
  /** Primary constructor.
   *  @param type Type of this array, must be a constant sized array
   *  @param membrs Vector of elements in this array
   */
  constant_vector2t(const type2tc &type, const std::vector<expr2tc> &members)
    : constant_vector_expr_methods(type, constant_vector_id, members)
  {
  }
  constant_vector2t(const constant_vector2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

/** Constant array of one particular value.
 *  Expression with array type, possibly dynamic or infinitely sized, with
 *  all elements initialized to a single value.
 *  @extends constant_array_of_data
 */
class constant_array_of2t : public constant_array_of_expr_methods
{
public:
  /** Primary constructor.
   *  @param type Type of this expression, must be an array.
   *  @param init Initializer for each element in this array
   */
  constant_array_of2t(const type2tc &type, const expr2tc &init)
    : constant_array_of_expr_methods(type, constant_array_of_id, init)
  {
  }
  constant_array_of2t(const constant_array_of2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

/** Symbol type.
 *  Contains the name of some variable. Various levels of renaming.
 *  @extends symbol_data
 */
class symbol2t : public symbol_expr_methods
{
public:
  /** Primary constructor
   *  @param type Type that this symbol has
   *  @param init Name of this symbol
   */

  symbol2t(
    const type2tc &type,
    const irep_idt &init,
    renaming_level lev = level0,
    unsigned int l1 = 0,
    unsigned int l2 = 0,
    unsigned int trd = 0,
    unsigned int node = 0)
    : symbol_expr_methods(type, symbol_id, init, lev, l1, l2, trd, node)
  {
    /* At some point in the past, symbols named "NULL" and "0" were equivalent.
     * The symbol called "0" should no longer be created for uniformity reasons.
     * Confirm that here, since support for it has been removed from smt_convt.
     * No other reason to disallow "0" as a symbol. */
    assert(init != "0");
  }

  symbol2t(const symbol2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

/** Nearbyint expression.
 *  Represents a rounding operation on a floatbv, we extend typecast as
 *  it already have a field for the rounding mode
 *  @extends typecast_data
 */
class nearbyint2t : public nearbyint_expr_methods
{
public:
  /** Primary constructor.
   *  @param type Type to round to
   *  @param from Expression to round from.
   *  @param rounding_mode Rounding mode, important only for floatbvs
   */
  nearbyint2t(
    const type2tc &type,
    const expr2tc &from,
    const expr2tc &rounding_mode)
    : nearbyint_expr_methods(type, nearbyint_id, from, rounding_mode)
  {
  }

  /** Primary constructor. This constructor defaults the rounding mode to
   *  the __ESBMC_rounding_mode symbol
   *  @param type Type to round to
   *  @param from Expression to round from.
   */
  nearbyint2t(const type2tc &type, const expr2tc &from)
    : nearbyint_expr_methods(
        type,
        nearbyint_id,
        from,
        symbol2tc(get_int32_type(), "c:@__ESBMC_rounding_mode"))
  {
  }

  nearbyint2t(const nearbyint2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Typecast expression.
 *  Represents cast from contained expression 'from' to the type of this
 *  typecast.
 *  @extends typecast_data
 */
class typecast2t : public typecast_expr_methods
{
public:
  /** Primary constructor.
   *  @param type Type to typecast to
   *  @param from Expression to cast from.
   *  @param rounding_mode Rounding mode, important only for floatbvs
   */
  typecast2t(
    const type2tc &type,
    const expr2tc &from,
    const expr2tc &rounding_mode)
    : typecast_expr_methods(type, typecast_id, from, rounding_mode)
  {
  }

  /** Primary constructor. This constructor defaults the rounding mode to
   *  the __ESBMC_rounding_mode symbol
   *  @param type Type to typecast to
   *  @param from Expression to cast from.
   */
  typecast2t(const type2tc &type, const expr2tc &from)
    : typecast_expr_methods(
        type,
        typecast_id,
        from,
        symbol2tc(get_int32_type(), "c:@__ESBMC_rounding_mode"))
  {
  }

  typecast2t(const typecast2t &ref) = default;
  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Bitcast expression.
 *  Represents cast from contained expression 'from' to the type of this
 *  typecast... but where the cast is performed at a 'bit representation' level.
 *  That is: the 'from' field is not interpreted by its logical value, but
 *  instead by the corresponding bit representation. The prime example of this
 *  is bitcasting floats: if one typecasted them to integers, they would be
 *  rounded; bitcasting them produces the bit-representation of the float, as
 *  an integer value.
 *
 *  Bitcasts are only allowed between types of equal width.
 *
 *  @extends bitcast_data
 */
class bitcast2t : public bitcast_expr_methods
{
public:
  /** Primary constructor.
   *  @param type Type to bitcast to
   *  @param from Expression to cast from.
   */
  bitcast2t(const type2tc &type, const expr2tc &from)
    : bitcast_expr_methods(type, bitcast_id, from)
  {
    try
    {
      assert(type->get_width() == from->type->get_width());
    }
    catch (const type2t::symbolic_type_excp &)
    {
      /* ignore */
    }
  }

  bitcast2t(const bitcast2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** If-then-else expression.
 *  Represents a ternary operation, (cond) ? truevalue : falsevalue.
 *  @extends if_data
 */
class if2t : public if_expr_methods
{
public:
  /** Primary constructor
   *  @param type Type this expression evaluates to.
   *  @param cond Condition to evaulate which side of ternary operator is used.
   *  @param trueval Value to use if cond evaluates to true.
   *  @param falseval Value to use if cond evaluates to false.
   */
  if2t(
    const type2tc &type,
    const expr2tc &cond,
    const expr2tc &trueval,
    const expr2tc &falseval)
    : if_expr_methods(type, if_id, cond, trueval, falseval)
  {
    assert(type->type_id == trueval->type->type_id);
    assert(type->type_id == falseval->type->type_id);
  }
  if2t(const if2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Equality expression. Evaluate whether two exprs are the same. Always has
 *  boolean type. @extends relation_data */
class equality2t : public equality_expr_methods
{
public:
  equality2t(const expr2tc &v1, const expr2tc &v2)
    : equality_expr_methods(get_bool_type(), equality_id, v1, v2)
  {
  }
  equality2t(const equality2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Inequality expression. Evaluate whether two exprs are different. Always has
 *  boolean type. @extends relation_data */
class notequal2t : public notequal_expr_methods
{
public:
  notequal2t(const expr2tc &v1, const expr2tc &v2)
    : notequal_expr_methods(get_bool_type(), notequal_id, v1, v2)
  {
  }
  notequal2t(const notequal2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Lessthan relation. Evaluate whether expression is less than another. Always
 *  has boolean type. @extends relation_data */
class lessthan2t : public lessthan_expr_methods
{
public:
  lessthan2t(const expr2tc &v1, const expr2tc &v2)
    : lessthan_expr_methods(get_bool_type(), lessthan_id, v1, v2)
  {
  }
  lessthan2t(const lessthan2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Greaterthan relation. Evaluate whether expression is greater than another.
 * Always has boolean type. @extends relation_data */
class greaterthan2t : public greaterthan_expr_methods
{
public:
  greaterthan2t(const expr2tc &v1, const expr2tc &v2)
    : greaterthan_expr_methods(get_bool_type(), greaterthan_id, v1, v2)
  {
  }
  greaterthan2t(const greaterthan2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Lessthanequal relation. Evaluate whether expression is less-than or
 * equal to another. Always has boolean type. @extends relation_data */
class lessthanequal2t : public lessthanequal_expr_methods
{
public:
  lessthanequal2t(const expr2tc &v1, const expr2tc &v2)
    : lessthanequal_expr_methods(get_bool_type(), lessthanequal_id, v1, v2)
  {
  }
  lessthanequal2t(const lessthanequal2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Greaterthanequal relation. Evaluate whether expression is greater-than or
 * equal to another. Always has boolean type. @extends relation_data */
class greaterthanequal2t : public greaterthanequal_expr_methods
{
public:
  greaterthanequal2t(const expr2tc &v1, const expr2tc &v2)
    : greaterthanequal_expr_methods(
        get_bool_type(),
        greaterthanequal_id,
        v1,
        v2)
  {
  }
  greaterthanequal2t(const greaterthanequal2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Not operation. Inverts boolean operand. Always has boolean type.
 *  @extends bool_1op */
class not2t : public not_expr_methods
{
public:
  /** Primary constructor. @param val Boolean typed operand to invert. */
  not2t(const expr2tc &val) : not_expr_methods(get_bool_type(), not_id, val)
  {
  }
  not2t(const not2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** And operation. Computes boolean value of (side_1 & side_2). Always results
 *  in boolean type. @extends logic_2ops */
class and2t : public and_expr_methods
{
public:
  /** Primary constructor. @param s1 Operand 1. @param s2 Operand 2. */
  and2t(const expr2tc &s1, const expr2tc &s2)
    : and_expr_methods(get_bool_type(), and_id, s1, s2)
  {
  }
  and2t(const and2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Or operation. Computes boolean value of (side_1 | side_2). Always results
 *  in boolean type. @extends logic_2ops */
class or2t : public or_expr_methods
{
public:
  /** Primary constructor. @param s1 Operand 1. @param s2 Operand 2. */
  or2t(const expr2tc &s1, const expr2tc &s2)
    : or_expr_methods(get_bool_type(), or_id, s1, s2)
  {
  }
  or2t(const or2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Xor operation. Computes boolean value of (side_1 ^ side_2). Always results
 *  in boolean type. @extends logic_2ops */
class xor2t : public xor_expr_methods
{
public:
  /** Primary constructor. @param s1 Operand 1. @param s2 Operand 2. */
  xor2t(const expr2tc &s1, const expr2tc &s2)
    : xor_expr_methods(get_bool_type(), xor_id, s1, s2)
  {
  }
  xor2t(const xor2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Implies operation. Computes boolean value of (side_1 -> side_2). Always
 *  results in boolean type. @extends logic_2ops */
class implies2t : public implies_expr_methods
{
public:
  /** Primary constructor. @param s1 Operand 1. @param s2 Operand 2. */
  implies2t(const expr2tc &s1, const expr2tc &s2)
    : implies_expr_methods(get_bool_type(), implies_id, s1, s2)
  {
  }
  implies2t(const implies2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Bit and operation. Perform bit and between two bitvector operands. Types of
 *  this expr and both operands must match. @extends bit_2ops */
class bitand2t : public bitand_expr_methods
{
public:
  /** Primary constructor.
   *  @param t Type of this expr.
   *  @param s1 Operand 1.
   *  @param s2 Operand 2. */
  bitand2t(const type2tc &t, const expr2tc &s1, const expr2tc &s2)
    : bitand_expr_methods(t, bitand_id, s1, s2)
  {
  }
  bitand2t(const bitand2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Bit or operation. Perform bit or between two bitvector operands. Types of
 *  this expr and both operands must match. @extends bit_2ops */
class bitor2t : public bitor_expr_methods
{
public:
  /** Primary constructor.
   *  @param t Type of this expr.
   *  @param s1 Operand 1.
   *  @param s2 Operand 2. */
  bitor2t(const type2tc &t, const expr2tc &s1, const expr2tc &s2)
    : bitor_expr_methods(t, bitor_id, s1, s2)
  {
  }
  bitor2t(const bitor2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Bit xor operation. Perform bit xor between two bitvector operands. Types of
 *  this expr and both operands must match. @extends bit_2ops */
class bitxor2t : public bitxor_expr_methods
{
public:
  /** Primary constructor.
   *  @param t Type of this expr.
   *  @param s1 Operand 1.
   *  @param s2 Operand 2. */
  bitxor2t(const type2tc &t, const expr2tc &s1, const expr2tc &s2)
    : bitxor_expr_methods(t, bitxor_id, s1, s2)
  {
  }
  bitxor2t(const bitxor2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Bit nand operation. Perform bit nand between two bitvector operands. Types of
 *  this expr and both operands must match. @extends bit_2ops */
class bitnand2t : public bitnand_expr_methods
{
public:
  /** Primary constructor.
   *  @param t Type of this expr.
   *  @param s1 Operand 1.
   *  @param s2 Operand 2. */
  bitnand2t(const type2tc &t, const expr2tc &s1, const expr2tc &s2)
    : bitnand_expr_methods(t, bitnand_id, s1, s2)
  {
  }
  bitnand2t(const bitnand2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Bit nor operation. Perform bit nor between two bitvector operands. Types of
 *  this expr and both operands must match. @extends bit_2ops */
class bitnor2t : public bitnor_expr_methods
{
public:
  /** Primary constructor.
   *  @param t Type of this expr.
   *  @param s1 Operand 1.
   *  @param s2 Operand 2. */
  bitnor2t(const type2tc &t, const expr2tc &s1, const expr2tc &s2)
    : bitnor_expr_methods(t, bitnor_id, s1, s2)
  {
  }
  bitnor2t(const bitnor2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Bit nxor operation. Perform bit nxor between two bitvector operands. Types of
 *  this expr and both operands must match. @extends bit_2ops */
class bitnxor2t : public bitnxor_expr_methods
{
public:
  /** Primary constructor.
   *  @param t Type of this expr.
   *  @param s1 Operand 1.
   *  @param s2 Operand 2. */
  bitnxor2t(const type2tc &t, const expr2tc &s1, const expr2tc &s2)
    : bitnxor_expr_methods(t, bitnxor_id, s1, s2)
  {
  }
  bitnxor2t(const bitnxor2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Bit not operation. Invert bits in bitvector operand. Operand must have the
 *  same type as this expr. @extends bitnot_data */
class bitnot2t : public bitnot_expr_methods
{
public:
  /** Primary constructor.
   *  @param type Type of this expr.
   *  @param v Value to invert */
  bitnot2t(const type2tc &type, const expr2tc &v)
    : bitnot_expr_methods(type, bitnot_id, v)
  {
  }
  bitnot2t(const type2tc &type, const expr2tc &v, const expr2tc &)
    : bitnot_expr_methods(type, bitnot_id, v)
  {
  }
  bitnot2t(const bitnot2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Logical shift right. Shifts operand 1 to the right by the number of bits in
 *  operand 2, with zeros shifted into empty spaces. All types must be integers,
 *  will probably find that the shifted value type must match the expr type.
 *  @extends bit_2ops */
class lshr2t : public lshr_expr_methods
{
public:
  /** Primary constructor.
   *  @param t Type of this expression.
   *  @param s1 Value to be shifted.
   *  @param s2 Number of bits to shift by, potentially nondeterministic. */
  lshr2t(const type2tc &t, const expr2tc &s1, const expr2tc &s2)
    : lshr_expr_methods(t, lshr_id, s1, s2)
  {
  }
  lshr2t(const lshr2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Arithmetic negation. Negate the operand, which must be a number type. Operand
 *  type must match expr type. @extends arith_1op */
class neg2t : public neg_expr_methods
{
public:
  /** Primary constructor.
   *  @param type Type of this expr.
   *  @param val Value to negate. */
  neg2t(const type2tc &type, const expr2tc &val)
    : neg_expr_methods(type, neg_id, val)
  {
  }
  neg2t(const neg2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Arithmetic abs. Take absolute value of the operand, which must be a number
 *  type. Operand type must match expr type. @extends arith_1op */
class abs2t : public abs_expr_methods
{
public:
  /** Primary constructor.
   *  @param type Type of this expr.
   *  @param val Value to abs. */
  abs2t(const type2tc &type, const expr2tc &val)
    : abs_expr_methods(type, abs_id, val)
  {
  }
  abs2t(const abs2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Addition operation. Adds two operands together. Must both be numeric types.
 *  Types of both operands and expr type should match.
 *
 *  @extends arith_2ops */
class add2t : public add_expr_methods
{
public:
  /** Primary constructor.
   *  @param type Type of this expr.
   *  @param v1 First operand.
   *  @param v2 Second operand. */
  add2t(const type2tc &type, const expr2tc &v1, const expr2tc &v2)
    : add_expr_methods(type, add_id, v1, v2)
  {
  }
  add2t(const add2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Subtraction operation. Subtracts second operand from first operand. Must both
 *  be numeric types. Types of both operands and expr type should match.
 *  @extends arith_2ops */
class sub2t : public sub_expr_methods
{
public:
  /** Primary constructor.
   *  @param type Type of this expr.
   *  @param v1 First operand.
   *  @param v2 Second operand. */
  sub2t(const type2tc &type, const expr2tc &v1, const expr2tc &v2)
    : sub_expr_methods(type, sub_id, v1, v2)
  {
  }
  sub2t(const sub2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Multiplication operation. Multiplies the two operands. Must both be numeric
 *  types. Types of both operands and expr type should match.
 *  @extends arith_2ops */
class mul2t : public mul_expr_methods
{
public:
  /** Primary constructor.
   *  @param type Type of this expr.
   *  @param v1 First operand.
   *  @param v2 Second operand. */
  mul2t(const type2tc &type, const expr2tc &v1, const expr2tc &v2)
    : mul_expr_methods(type, mul_id, v1, v2)
  {
#if 0
    assert(is_signedbv_type(v1) == is_signedbv_type(v2));
    assert(is_unsignedbv_type(v1) == is_unsignedbv_type(v2));
#endif
  }
  mul2t(const mul2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Division operation. Divides first operand by second operand. Must both be
 *  numeric types. Types of both operands and expr type should match.
 *  @extends arith_2ops */
class div2t : public div_expr_methods
{
public:
  /** Primary constructor.
   *  @param type Type of this expr.
   *  @param v1 First operand.
   *  @param v2 Second operand. */
  div2t(const type2tc &type, const expr2tc &v1, const expr2tc &v2)
    : div_expr_methods(type, div_id, v1, v2)
  {
#if 0
    assert(is_signedbv_type(v1) == is_signedbv_type(v2));
    assert(is_unsignedbv_type(v1) == is_unsignedbv_type(v2));
#endif
  }
  div2t(const div2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** IEEE Addition operation. Adds two floatbvs together.
 *  Types of both operands and expr type should match. @extends ieee_arith_2ops */
class ieee_add2t : public ieee_add_expr_methods
{
public:
  /** Primary constructor.
   *  @param type Type of this expr.
   *  @param v1 First operand.
   *  @param v2 Second operand.
   *  @param rm rounding mode. */
  ieee_add2t(
    const type2tc &type,
    const expr2tc &v1,
    const expr2tc &v2,
    const expr2tc &rm)
    : ieee_add_expr_methods(type, ieee_add_id, v1, v2, rm)
  {
  }
  ieee_add2t(const ieee_add2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** IEEE subtraction operation. Subtracts second operand from first operand. Must both
 *  be floatbvs types. Types of both operands and expr type should match.
 *  @extends ieee_arith_2ops */
class ieee_sub2t : public ieee_sub_expr_methods
{
public:
  /** Primary constructor.
   *  @param type Type of this expr.
   *  @param v1 First operand.
   *  @param v2 Second operand.
   *  @param rm rounding mode. */
  ieee_sub2t(
    const type2tc &type,
    const expr2tc &v1,
    const expr2tc &v2,
    const expr2tc &rm)
    : ieee_sub_expr_methods(type, ieee_sub_id, v1, v2, rm)
  {
  }
  ieee_sub2t(const ieee_sub2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** IEEE multiplication operation. Multiplies the two operands. Must both be floatbvs
 *  types. Types of both operands and expr type should match.
 *  @extends ieee_arith_2ops */
class ieee_mul2t : public ieee_mul_expr_methods
{
public:
  /** Primary constructor.
   *  @param type Type of this expr.
   *  @param v1 First operand.
   *  @param v2 Second operand.
   *  @param rm rounding mode. */
  ieee_mul2t(
    const type2tc &type,
    const expr2tc &v1,
    const expr2tc &v2,
    const expr2tc &rm)
    : ieee_mul_expr_methods(type, ieee_mul_id, v1, v2, rm)
  {
  }
  ieee_mul2t(const ieee_mul2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** IEEE division operation. Divides first operand by second operand. Must both be
 *  floatbvs types. Types of both operands and expr type should match.
 *  @extends ieee_arith_2ops */
class ieee_div2t : public ieee_div_expr_methods
{
public:
  /** Primary constructor.
   *  @param type Type of this expr.
   *  @param v1 First operand.
   *  @param v2 Second operand.
   *  @param rm rounding mode. */
  ieee_div2t(
    const type2tc &type,
    const expr2tc &v1,
    const expr2tc &v2,
    const expr2tc &rm)
    : ieee_div_expr_methods(type, ieee_div_id, v1, v2, rm)
  {
  }
  ieee_div2t(const ieee_div2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** IEEE fused multiply-add operation. Computes (x*y) + z as if to infinite
 *  precision and rounded only once to fit the result type. Must be
 *  floatbvs types. Types of the 3 operands and expr type should match.
 *  @extends ieee_arith_2ops */
class ieee_fma2t : public ieee_fma_expr_methods
{
public:
  /** Primary constructor.
   *  @param type Type of this expr.
   *  @param v1 First operand.
   *  @param v2 Second operand.
   *  @param v3 Second operand.
   *  @param rm rounding mode. */
  ieee_fma2t(
    const type2tc &type,
    const expr2tc &v1,
    const expr2tc &v2,
    const expr2tc &v3,
    const expr2tc &rm)
    : ieee_fma_expr_methods(type, ieee_fma_id, v1, v2, v3, rm)
  {
  }
  ieee_fma2t(const ieee_fma2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** IEEE sqrt operation. Square root of the first operand. Must be a
 *  floatbv.
 *  @extends ieee_arith_2ops */
class ieee_sqrt2t : public ieee_sqrt_expr_methods
{
public:
  /** Primary constructor.
   *  @param type Type of this expr.
   *  @param v1 First operand.
   *  @param v2 Second operand.
   *  @param rm rounding mode. */
  ieee_sqrt2t(const type2tc &type, const expr2tc &v1, const expr2tc &rm)
    : ieee_sqrt_expr_methods(type, ieee_sqrt_id, v1, rm)
  {
  }
  ieee_sqrt2t(const ieee_sqrt2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Modulus operation. Takes modulus of first operand divided by 2nd operand.
 *  Should both be integer types. Types of both operands and expr type should
 *  match. @extends arith_2ops */
class modulus2t : public modulus_expr_methods
{
public:
  /** Primary constructor.
   *  @param type Type of this expr.
   *  @param v1 First operand.
   *  @param v2 Second operand. */
  modulus2t(const type2tc &type, const expr2tc &v1, const expr2tc &v2)
    : modulus_expr_methods(type, modulus_id, v1, v2)
  {
  }
  modulus2t(const modulus2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Shift left operation. Shifts contents of first operand left by number of
 *  bit positions indicated by the second operand. Both must be integers.
 * @extends bit_2ops */
class shl2t : public shl_expr_methods
{
public:
  /** Primary constructor.
   *  @param type Type of this expr.
   *  @param v1 Value to shift.
   *  @param v2 Number of bits to to shift by. */
  shl2t(const type2tc &type, const expr2tc &v1, const expr2tc &v2)
    : shl_expr_methods(type, shl_id, v1, v2)
  {
  }
  shl2t(const shl2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Arithmetic Shift right operation. Shifts contents of first operand right by
 *  number of bit positions indicated by the second operand, preserving sign of
 *  original number. Both must be integers. Types of both operands and expr type
 *  should match. @extends arith_2ops */
class ashr2t : public ashr_expr_methods
{
public:
  /** Primary constructor.
   *  @param type Type of this expr.
   *  @param v1 Value to shift.
   *  @param v2 Number of bits to to shift by. */
  ashr2t(const type2tc &type, const expr2tc &v1, const expr2tc &v2)
    : ashr_expr_methods(type, ashr_id, v1, v2)
  {
  }
  ashr2t(const ashr2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Same-object operation. Checks whether two operands with pointer type have the
 *  same pointer object or not. Always has boolean result.
 *  @extends same_object_data */
class same_object2t : public same_object_expr_methods
{
public:
  /** Primary constructor. @param v1 First object. @param v2 Second object. */
  same_object2t(const expr2tc &v1, const expr2tc &v2)
    : same_object_expr_methods(get_bool_type(), same_object_id, v1, v2)
  {
  }
  same_object2t(const same_object2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Extract pointer offset. From an expression of pointer type, produce the
 *  number of bytes difference between where this pointer points to and the start
 *  of the object it points at. @extends pointer_ops */
class pointer_offset2t : public pointer_offset_expr_methods
{
public:
  /** Primary constructor.
   *  @param type Model basic integer type.
   *  @param ptrobj Pointer object to get offset from. */
  pointer_offset2t(const type2tc &type, const expr2tc &ptrobj)
    : pointer_offset_expr_methods(type, pointer_offset_id, ptrobj)
  {
    assert(type->type_id == type2t::signedbv_id);
    assert(type->get_width() == config.ansi_c.address_width);
  }
  pointer_offset2t(const pointer_offset2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Extract pointer object. From an expression of pointer type, produce the
 *  pointer object that this pointer points into. @extends pointer_ops */
class pointer_object2t : public pointer_object_expr_methods
{
public:
  /** Primary constructor.
   *  @param type Model basic integer type.
   *  @param ptrobj Pointer object to get object from. */
  pointer_object2t(const type2tc &type, const expr2tc &ptrobj)
    : pointer_object_expr_methods(type, pointer_object_id, ptrobj)
  {
  }
  pointer_object2t(const pointer_object2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

/** @extends pointer_ops */
class pointer_capability2t : public pointer_capability_expr_methods
{
public:
  pointer_capability2t(const type2tc &type, const expr2tc &ptrobj)
    : pointer_capability_expr_methods(type, pointer_capability_id, ptrobj)
  {
  }
  pointer_capability2t(const pointer_capability2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

/** Address of operation. Takes some object as an argument - ideally a symbol
 *  renamed to level 1, unfortunately some string constants reach here. Produces
 *  pointer typed expression.
 *  @extends pointer_ops */
class address_of2t : public address_of_expr_methods
{
public:
  /** Primary constructor.
   *  @param subtype Subtype of pointer to generate. Crucially, the type of the
   *         expr is a pointer to this subtype. This is slightly unintuitive,
   *         might be changed in the future.
   *  @param ptrobj Item to take pointer to. */
  address_of2t(const type2tc &subtype, const expr2tc &ptrobj)
    : address_of_expr_methods(pointer_type2tc(subtype), address_of_id, ptrobj)
  {
    assert(ptrobj->expr_id != expr2t::constant_int_id);
    assert(ptrobj->expr_id != expr2t::address_of_id);
  }
  address_of2t(const address_of2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Extract byte from data. From a particular data structure, extracts a single
 *  byte from its byte representation, at a particular offset into the data
 *  structure. Must only evaluate to byte types.
 *  @extends byte_extract_data */
class byte_extract2t : public byte_extract_expr_methods
{
public:
  /** Primary constructor.
   *  @param type Type of this expression. May only ever be an 8 bit integer
   *  @param is_big_endian Whether or not to use big endian byte representation
   *         of source object.
   *  @param source Object to extract data from. Any type.
   *  @param offset Offset into source data object to extract from. */
  byte_extract2t(
    const type2tc &type,
    const expr2tc &source,
    const expr2tc &offset,
    bool is_big_endian)
    : byte_extract_expr_methods(
        type,
        byte_extract_id,
        source,
        offset,
        is_big_endian)
  {
  }
  byte_extract2t(const byte_extract2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Update byte. Takes a data object and updates the value of a particular
 *  byte in its byte representation, at a particular offset into the data object.
 *  Output of expression is a new copy of the source object, with the updated
 *  value. @extends byte_update_data */
class byte_update2t : public byte_update_expr_methods
{
public:
  /** Primary constructor
   *  @param type Type of resulting, updated, data object.
   *  @param is_big_endian Whether to use big endian byte representation.
   *  @param source Source object in which to update a byte.
   *  @param updateval Value of byte to  update source with. */
  byte_update2t(
    const type2tc &type,
    const expr2tc &source,
    const expr2tc &offset,
    const expr2tc &updateval,
    bool is_big_endian)
    : byte_update_expr_methods(
        type,
        byte_update_id,
        source,
        offset,
        updateval,
        is_big_endian)
  {
  }
  byte_update2t(const byte_update2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** With operation. Updates either an array or a struct/union with a new element
 *  or member. Expression value is the array or struct/union with the updated
 *  value. Ideally in the future this will become two operations, one for arrays
 *  and one for structs/unions. @extends with_data */
class with2t : public with_expr_methods
{
  void assert_consistency() const;

public:
  /** Primary constructor.
   *  @param type Type of this expression; Same as source.
   *  @param source Data object to update.
   *  @param field Field to update - a constant string naming the field if source
   *         is a struct/union, or an integer index if source is an array. */
  with2t(
    const type2tc &type,
    const expr2tc &source,
    const expr2tc &field,
    const expr2tc &value)
    : with_expr_methods(type, with_id, source, field, value)
  {
#ifndef NDEBUG /* only check consistency in non-Release builds */
    assert_consistency();
#endif
  }
  with2t(const with2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Member operation. Extracts a particular member out of a struct or union.
 *  @extends member_data */
class member2t : public member_expr_methods
{
public:
  /** Primary constructor.
   *  @param type Type of extracted member.
   *  @param source Data structure to extract from.
   *  @param memb Name of member to extract.  */
  member2t(const type2tc &type, const expr2tc &source, const irep_idt &memb)
    : member_expr_methods(type, member_id, source, memb)
  {
#ifndef NDEBUG /* only check consistency in non-Release builds */
    assert(
      source->type->type_id == type2t::struct_id ||
      source->type->type_id == type2t::union_id);
    auto *data = dynamic_cast<const struct_union_data *>(source->type.get());
    assert(data);
    /* internally asserts consistency conditions */
    data->get_component_number(memb);
#endif
  }
  member2t(const member2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Array index operation. Extracts an element from an array at a particular
 *  index. @extends index_data */
class index2t : public index_expr_methods
{
public:
  /** Primary constructor.
   *  @param type Type of element extracted.
   *  @param source Array to extract data from.
   *  @param index Element in source to extract from. */
  index2t(const type2tc &type, const expr2tc &source, const expr2tc &index)
    : index_expr_methods(type, index_id, source, index)
  {
    assert(is_array_type(source) || is_vector_type(source));
#if 0
    assert(
      is_array_type(source)
        ? type == to_array_type(source->type).subtype
        : ((is_unsignedbv_type(type) || is_signedbv_type(type)) &&
           type->get_width() == config.ansi_c.char_width));
#endif
  }
  index2t(const index2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Is operand not-a-number. Used to implement C library isnan function for
 *  float/double values. Boolean result. @extends arith_1op */
class isnan2t : public isnan_expr_methods
{
public:
  /** Primary constructor. @param value Number value to test for nan */
  isnan2t(const expr2tc &value)
    : isnan_expr_methods(get_bool_type(), isnan_id, value)
  {
  }
  isnan2t(const isnan2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Check whether operand overflows. Operand must be either add, subtract,
 *  or multiply, and have integer operands themselves. If the result of the
 *  operation doesn't fit in the bitwidth of the operands, this expr evaluates
 *  to true. XXXjmorse - in the future we should ensure the type of the
 *  operand is the expected type result of the operation. That way we can tell
 *  whether to do a signed or unsigned over/underflow test.
 *  @extends overflow_ops */
class overflow2t : public overflow_expr_methods
{
public:
  /** Primary constructor.
   *  @param operand Operation to test overflow on; either an add, subtract, or
   *         multiply. */
  overflow2t(const expr2tc &operand)
    : overflow_expr_methods(get_bool_type(), overflow_id, operand)
  {
  }
  overflow2t(const overflow2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Test if a cast overflows. Check to see whether casting the operand to a
 *  particular bitsize will cause an integer overflow. If it does, this expr
 *  evaluates to true. @extends overflow_cast_data */
class overflow_cast2t : public overflow_cast_expr_methods
{
public:
  /** Primary constructor.
   *  @param operand Value to test cast out on. Should have integer type.
   *  @param bits Number of integer bits to cast operand to.  */
  overflow_cast2t(const expr2tc &operand, unsigned int bits)
    : overflow_cast_expr_methods(
        get_bool_type(),
        overflow_cast_id,
        operand,
        bits)
  {
  }
  overflow_cast2t(const overflow_cast2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** Test for negation overflows. Check whether or not negating an operand would
 *  lead to an integer overflow - for example, there's no representation of
 *  -INT_MIN. Evaluates to true if overflow would occur. @extends overflow_ops */
class overflow_neg2t : public overflow_neg_expr_methods
{
public:
  /** Primary constructor. @param operand Integer to test negation of. */
  overflow_neg2t(const expr2tc &operand)
    : overflow_neg_expr_methods(get_bool_type(), overflow_neg_id, operand)
  {
  }
  overflow_neg2t(const overflow_neg2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

/** Record unknown data value. Exclusively for use in pointer analysis to record
 *  the fact that we point at an unknown item of data. @extends expr2t */
class unknown2t : public unknown_expr_methods
{
public:
  /** Primary constructor. @param type Type of unknown data item */
  unknown2t(const type2tc &type) : unknown_expr_methods(type, unknown_id)
  {
  }
  unknown2t(const unknown2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

/** Record invalid data value. Exclusively for use in pointer analysis to record
 *  the fact that what we point at is guarenteed to be invalid or nonexistant.
 *  @extends expr2t */
class invalid2t : public invalid_expr_methods
{
public:
  invalid2t(const type2tc &type) : invalid_expr_methods(type, invalid_id)
  {
  }
  invalid2t(const invalid2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

/** Record null pointer value. Exclusively for use in pointer analysis to record
 *  the fact that a pointer can be NULL. @extends expr2t */
class null_object2t : public null_object_expr_methods
{
public:
  null_object2t(const type2tc &type)
    : null_object_expr_methods(type, null_object_id)
  {
  }
  null_object2t(const null_object2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

/** Record a dynamicly allocated object. Exclusively for use in pointer analysis.
 *  @extends dynamic_object_data */
class dynamic_object2t : public dynamic_object_expr_methods
{
public:
  dynamic_object2t(
    const type2tc &type,
    const expr2tc &inst,
    bool inv,
    bool uknown)
    : dynamic_object_expr_methods(type, dynamic_object_id, inst, inv, uknown)
  {
  }
  dynamic_object2t(const dynamic_object2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

/** Dereference operation. Expanded by symbolic execution into an if-then-else
 *  set of cases that take the value set of what this pointer might point at,
 *  examines the pointer's pointer object, and constructs a huge if-then-else
 *  case to evaluate to the appropriate data object for this pointer.
 *  @extends dereference_data */
class dereference2t : public dereference_expr_methods
{
public:
  /** Primary constructor.
   *  @param type Type of dereferenced data.
   *  @param operand Pointer to dereference. */
  dereference2t(const type2tc &type, const expr2tc &operand)
    : dereference_expr_methods(type, dereference_id, operand)
  {
  }
  dereference2t(const dereference2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

/** Test whether ptr is valid. Expanded at symex time to look up whether or not
 *  the pointer operand is invalid (i.e., doesn't point at something and thus
 *  would be invalid to dereference). Boolean result. @extends object_ops */
class valid_object2t : public valid_object_expr_methods
{
public:
  /** Primary constructor. @param operand Pointer value to examine for validity*/
  valid_object2t(const expr2tc &operand)
    : valid_object_expr_methods(get_bool_type(), valid_object_id, operand)
  {
  }
  valid_object2t(const valid_object2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

/** Test pointer for deallocation. Check for use after free: this irep is
 *  expanded at symex time to look up whether or not the operand is a) an invalid
 *  object, and b) if it is, whether it's been marked as being deallocated.
 *  Evalutes to true if that's the case. @extends object_ops */
class deallocated_obj2t : public deallocated_obj_expr_methods
{
public:
  /** Primary constructor. @param operand Pointer to check for deallocation */
  deallocated_obj2t(const expr2tc &operand)
    : deallocated_obj_expr_methods(get_bool_type(), deallocated_obj_id, operand)
  {
  }
  deallocated_obj2t(const deallocated_obj2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

/** Retrieve dynamic size of pointer obj. For a dynamically allocated pointer
 *  object, retrieves its potentially nondeterministic object size. Expanded at
 *  symex time to access a modelling array. Not sure what happens if you feed
 *  it a nondynamic pointer, it'll probably give you a free variable.
 *  @extends object_ops */
class dynamic_size2t : public dynamic_size_expr_methods
{
public:
  /** Primary constructor. @param operand Pointer object to fetch size for. */
  dynamic_size2t(const expr2tc &operand)
    : dynamic_size_expr_methods(size_type2(), dynamic_size_id, operand)
  {
  }
  dynamic_size2t(const dynamic_size2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

/** Irep for various side effects. Stores data about various things that can
 *  cause side effects, such as memory allocations, nondeterministic value
 *  allocations (nondet_* funcs,).
 *
 *  Also allows for function-calls to be represented. This side-effect
 *  expression is how function calls inside expressions are represented during
 *  parsing, and are all flattened out prior to GOTO program creation. However,
 *  under certain circumstances irep2 needs to represent such function calls,
 *  so this facility is preserved in irep2.
 *
 *  @extends sideeffect_data */
class sideeffect2t : public sideeffect_expr_methods
{
public:
  /** Primary constructor.
   *  @param t Type this side-effect evaluates to.
   *  @param operand Not really certain. Sometimes turns up in string-irep.
   *  @param sz Size of dynamic allocation to make.
   *  @param alloct Type of piece of data to allocate.
   *  @param a Vector of arguments to function call. */
  sideeffect2t(
    const type2tc &t,
    const expr2tc &oper,
    const expr2tc &sz,
    const std::vector<expr2tc> &a,
    const type2tc &alloct,
    allockind k)
    : sideeffect_expr_methods(t, sideeffect_id, oper, sz, a, alloct, k)
  {
    if (k == allockind::alloca)
      assert(oper->type == sz->type);
  }
  sideeffect2t(const sideeffect2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

class code_block2t : public code_block_expr_methods
{
public:
  code_block2t(const std::vector<expr2tc> &operands)
    : code_block_expr_methods(get_empty_type(), code_block_id, operands)
  {
  }
  code_block2t(const code_block2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

class code_assign2t : public code_assign_expr_methods
{
public:
  code_assign2t(const expr2tc &target, const expr2tc &source)
    : code_assign_expr_methods(get_empty_type(), code_assign_id, target, source)
  {
  }
  code_assign2t(const code_assign2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

// NB: code_init2t is a specialization of code_assign2t
class code_init2t : public code_init_expr_methods
{
public:
  code_init2t(const expr2tc &target, const expr2tc &source)
    : code_init_expr_methods(get_empty_type(), code_init_id, target, source)
  {
  }
  code_init2t(const code_init2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

class code_decl2t : public code_decl_expr_methods
{
public:
  code_decl2t(const type2tc &t, const irep_idt &name)
    : code_decl_expr_methods(t, code_decl_id, name)
  {
  }
  code_decl2t(const code_decl2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

class code_dead2t : public code_dead_expr_methods
{
public:
  code_dead2t(const type2tc &t, const irep_idt &name)
    : code_dead_expr_methods(t, code_dead_id, name)
  {
  }
  code_dead2t(const code_dead2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

class code_printf2t : public code_printf_expr_methods
{
public:
  code_printf2t(const std::vector<expr2tc> &opers, const std::string &b)
    : code_printf_expr_methods(get_empty_type(), code_printf_id, opers, b)
  {
  }
  code_printf2t(const code_printf2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

class code_expression2t : public code_expression_expr_methods
{
public:
  code_expression2t(const expr2tc &oper)
    : code_expression_expr_methods(get_empty_type(), code_expression_id, oper)
  {
  }
  code_expression2t(const code_expression2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

class code_return2t : public code_return_expr_methods
{
public:
  code_return2t(const expr2tc &oper)
    : code_return_expr_methods(get_empty_type(), code_return_id, oper)
  {
  }
  code_return2t(const code_return2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

class code_skip2t : public code_skip_expr_methods
{
public:
  code_skip2t(const type2tc &type) : code_skip_expr_methods(type, code_skip_id)
  {
  }
  code_skip2t(const code_skip2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

class code_free2t : public code_free_expr_methods
{
public:
  code_free2t(const expr2tc &oper)
    : code_free_expr_methods(get_empty_type(), code_free_id, oper)
  {
  }
  code_free2t(const code_free2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

class code_goto2t : public code_goto_expr_methods
{
public:
  code_goto2t(const irep_idt &targ)
    : code_goto_expr_methods(get_empty_type(), code_goto_id, targ)
  {
  }
  code_goto2t(const code_goto2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

class object_descriptor2t : public object_descriptor_expr_methods
{
public:
  object_descriptor2t(
    const type2tc &t,
    const expr2tc &root,
    const expr2tc &offs,
    unsigned int alignment)
    : object_descriptor_expr_methods(
        t,
        object_descriptor_id,
        root,
        offs,
        alignment)
  {
  }
  object_descriptor2t(const object_descriptor2t &ref) = default;

  const expr2tc &get_root_object() const;

  static std::string field_names[esbmct::num_type_fields];
};

class code_function_call2t : public code_function_call_expr_methods
{
public:
  code_function_call2t(
    const expr2tc &r,
    const expr2tc &func,
    const std::vector<expr2tc> &args)
    : code_function_call_expr_methods(
        get_empty_type(),
        code_function_call_id,
        r,
        func,
        args)
  {
  }
  code_function_call2t(const code_function_call2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

class code_comma2t : public code_comma_expr_methods
{
public:
  code_comma2t(const type2tc &t, const expr2tc &s1, const expr2tc &s2)
    : code_comma_expr_methods(t, code_comma_id, s1, s2)
  {
  }
  code_comma2t(const code_comma2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

class invalid_pointer2t : public invalid_pointer_expr_methods
{
public:
  invalid_pointer2t(const expr2tc &obj)
    : invalid_pointer_expr_methods(get_bool_type(), invalid_pointer_id, obj)
  {
  }
  invalid_pointer2t(const invalid_pointer2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

class code_asm2t : public code_asm_expr_methods
{
public:
  code_asm2t(const type2tc &type, const irep_idt &stringref)
    : code_asm_expr_methods(type, code_asm_id, stringref)
  {
  }
  code_asm2t(const code_asm2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

class code_cpp_del_array2t : public code_cpp_del_array_expr_methods
{
public:
  code_cpp_del_array2t(const expr2tc &v)
    : code_cpp_del_array_expr_methods(
        get_empty_type(),
        code_cpp_del_array_id,
        v)
  {
  }
  code_cpp_del_array2t(const code_cpp_del_array2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

class code_cpp_delete2t : public code_cpp_delete_expr_methods
{
public:
  code_cpp_delete2t(const expr2tc &v)
    : code_cpp_delete_expr_methods(get_empty_type(), code_cpp_delete_id, v)
  {
  }
  code_cpp_delete2t(const code_cpp_delete2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

class code_cpp_catch2t : public code_cpp_catch_expr_methods
{
public:
  code_cpp_catch2t(const std::vector<irep_idt> &el)
    : code_cpp_catch_expr_methods(get_empty_type(), code_cpp_catch_id, el)
  {
  }
  code_cpp_catch2t(const code_cpp_catch2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

class code_cpp_throw2t : public code_cpp_throw_expr_methods
{
public:
  code_cpp_throw2t(const expr2tc &o, const std::vector<irep_idt> &l)
    : code_cpp_throw_expr_methods(get_empty_type(), code_cpp_throw_id, o, l)
  {
  }
  code_cpp_throw2t(const code_cpp_throw2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

class code_cpp_throw_decl2t : public code_cpp_throw_decl_expr_methods
{
public:
  code_cpp_throw_decl2t(const std::vector<irep_idt> &l)
    : code_cpp_throw_decl_expr_methods(
        get_empty_type(),
        code_cpp_throw_decl_id,
        l)
  {
  }
  code_cpp_throw_decl2t(const code_cpp_throw_decl2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

class code_cpp_throw_decl_end2t : public code_cpp_throw_decl_end_expr_methods
{
public:
  code_cpp_throw_decl_end2t(const std::vector<irep_idt> &exl)
    : code_cpp_throw_decl_end_expr_methods(
        get_empty_type(),
        code_cpp_throw_decl_end_id,
        exl)
  {
  }
  code_cpp_throw_decl_end2t(const code_cpp_throw_decl_end2t &ref) = default;

  static std::string field_names[esbmct::num_type_fields];
};

class isinf2t : public isinf_expr_methods
{
public:
  isinf2t(const expr2tc &val)
    : isinf_expr_methods(get_bool_type(), isinf_id, val)
  {
  }
  isinf2t(const isinf2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

class isnormal2t : public isnormal_expr_methods
{
public:
  isnormal2t(const expr2tc &val)
    : isnormal_expr_methods(get_bool_type(), isnormal_id, val)
  {
  }
  isnormal2t(const isnormal2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

class isfinite2t : public isfinite_expr_methods
{
public:
  isfinite2t(const expr2tc &val)
    : isfinite_expr_methods(get_bool_type(), isfinite_id, val)
  {
  }
  isfinite2t(const isfinite2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

class signbit2t : public signbit_expr_methods
{
public:
  signbit2t(const expr2tc &val)
    : signbit_expr_methods(get_int32_type(), signbit_id, val)
  {
  }
  signbit2t(const signbit2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

class bswap2t : public bswap_expr_methods
{
public:
  bswap2t(const type2tc &type, const expr2tc &val)
    : bswap_expr_methods(type, bswap_id, val)
  {
  }
  bswap2t(const bswap2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

class popcount2t : public popcount_expr_methods
{
public:
  popcount2t(const expr2tc &val)
    : popcount_expr_methods(get_int32_type(), popcount_id, val)
  {
  }
  popcount2t(const popcount2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

/** @extends bit_2ops */
class concat2t : public concat_expr_methods
{
public:
  concat2t(const type2tc &type, const expr2tc &forward, const expr2tc &aft)
    : concat_expr_methods(type, concat_id, forward, aft)
  {
    assert(is_unsignedbv_type(forward));
    assert(is_unsignedbv_type(aft));
  }
  concat2t(const concat2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

class extract2t : public extract_expr_methods
{
public:
  extract2t(
    const type2tc &type,
    const expr2tc &from,
    unsigned int upper,
    unsigned int lower)
    : extract_expr_methods(type, extract_id, from, upper, lower)
  {
  }
  extract2t(const extract2t &ref) = default;

  expr2tc do_simplify() const override;

  static std::string field_names[esbmct::num_type_fields];
};

// Same deal as for "type_macros".
#ifdef NDEBUG
#  define dynamic_cast static_cast
#endif
#define expr_macros(name)                                                      \
  inline bool is_##name##2t(const expr2tc &t)                                  \
  {                                                                            \
    return t->expr_id == expr2t::name##_id;                                    \
  }                                                                            \
  inline bool is_##name##2t(const expr2t &r)                                   \
  {                                                                            \
    return r.expr_id == expr2t::name##_id;                                     \
  }                                                                            \
  inline const name##2t & to_##name##2t(const expr2tc &t)                      \
  {                                                                            \
    return dynamic_cast<const name##2t &>(*t);                                 \
  }                                                                            \
  inline name##2t & to_##name##2t(expr2tc & t)                                 \
  {                                                                            \
    return dynamic_cast<name##2t &>(*t.get());                                 \
  }

// Boost preprocessor magic to iterate over all exprs,
#define _ESBMC_IREP2_MACROS_ENUM(r, data, elem) expr_macros(elem);
BOOST_PP_LIST_FOR_EACH(_ESBMC_IREP2_MACROS_ENUM, foo, ESBMC_LIST_OF_EXPRS)

#undef expr_macros
#ifdef dynamic_cast
#  undef dynamic_cast
#endif

#endif /* IREP2_EXPR_H_ */