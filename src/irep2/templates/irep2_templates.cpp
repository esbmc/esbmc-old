#include <irep2/irep2_templates.h>

std::string indent_str_irep2(unsigned int indent)
{
  return std::string(indent, ' ');
}

std::string bool_type2t::field_names[esbmct::num_type_fields] =
  {"", "", "", "", ""};
std::string empty_type2t::field_names[esbmct::num_type_fields] =
  {"", "", "", "", ""};
std::string symbol_type2t::field_names[esbmct::num_type_fields] =
  {"symbol_name", "", "", "", ""};
std::string struct_type2t::field_names[esbmct::num_type_fields] =
  {"members", "member_names", "member_pretty_names", "typename", "packed", ""};
std::string union_type2t::field_names[esbmct::num_type_fields] =
  {"members", "member_names", "member_pretty_names", "typename", "packed", ""};
std::string unsignedbv_type2t::field_names[esbmct::num_type_fields] =
  {"width", "", "", "", ""};
std::string signedbv_type2t::field_names[esbmct::num_type_fields] =
  {"width", "", "", "", ""};
std::string code_type2t::field_names[esbmct::num_type_fields] =
  {"arguments", "ret_type", "argument_names", "ellipsis", ""};
std::string array_type2t::field_names[esbmct::num_type_fields] =
  {"subtype", "array_size", "size_is_infinite", "", ""};
std::string vector_type2t::field_names[esbmct::num_type_fields] =
  {"subtype", "array_size", "size_is_infinite", "", ""};
std::string pointer_type2t::field_names[esbmct::num_type_fields] =
  {"subtype", "", "", "", ""};
std::string fixedbv_type2t::field_names[esbmct::num_type_fields] =
  {"width", "integer_bits", "", "", ""};
std::string floatbv_type2t::field_names[esbmct::num_type_fields] =
  {"fraction", "exponent", "", "", ""};
std::string cpp_name_type2t::field_names[esbmct::num_type_fields] =
  {"name", "template args", "", "", ""};

// Exprs

std::string constant_int2t::field_names[esbmct::num_type_fields] =
  {"value", "", "", "", ""};
std::string constant_fixedbv2t::field_names[esbmct::num_type_fields] =
  {"value", "", "", "", ""};
std::string constant_floatbv2t::field_names[esbmct::num_type_fields] =
  {"value", "", "", "", ""};
std::string constant_struct2t::field_names[esbmct::num_type_fields] =
  {"members", "", "", "", ""};
std::string constant_union2t::field_names[esbmct::num_type_fields] =
  {"members", "init_field", "", "", ""};
std::string constant_bool2t::field_names[esbmct::num_type_fields] =
  {"value", "", "", "", ""};
std::string constant_array2t::field_names[esbmct::num_type_fields] =
  {"members", "", "", "", ""};
std::string constant_array_of2t::field_names[esbmct::num_type_fields] =
  {"initializer", "", "", "", ""};
std::string constant_string2t::field_names[esbmct::num_type_fields] =
  {"value", "kind", "", "", ""};
std::string constant_vector2t::field_names[esbmct::num_type_fields] =
  {"members", "", "", "", ""};
std::string symbol2t::field_names[esbmct::num_type_fields] =
  {"name", "renamelev", "level1_num", "level2_num", "thread_num", "node_num"};
std::string typecast2t::field_names[esbmct::num_type_fields] =
  {"from", "rounding_mode", "", "", "", ""};
std::string bitcast2t::field_names[esbmct::num_type_fields] =
  {"from", "", "", "", ""};
std::string nearbyint2t::field_names[esbmct::num_type_fields] =
  {"from", "rounding_mode", "", "", "", ""};
std::string if2t::field_names[esbmct::num_type_fields] =
  {"cond", "true_value", "false_value", "", ""};
std::string equality2t::field_names[esbmct::num_type_fields] =
  {"side_1", "side_2", "", "", ""};
std::string notequal2t::field_names[esbmct::num_type_fields] =
  {"side_1", "side_2", "", "", ""};
std::string lessthan2t::field_names[esbmct::num_type_fields] =
  {"side_1", "side_2", "", "", ""};
std::string greaterthan2t::field_names[esbmct::num_type_fields] =
  {"side_1", "side_2", "", "", ""};
std::string lessthanequal2t::field_names[esbmct::num_type_fields] =
  {"side_1", "side_2", "", "", ""};
std::string greaterthanequal2t::field_names[esbmct::num_type_fields] =
  {"side_1", "side_2", "", "", ""};
std::string not2t::field_names[esbmct::num_type_fields] =
  {"value", "", "", "", ""};
std::string and2t::field_names[esbmct::num_type_fields] =
  {"side_1", "side_2", "", "", ""};
std::string or2t::field_names[esbmct::num_type_fields] =
  {"side_1", "side_2", "", "", ""};
std::string xor2t::field_names[esbmct::num_type_fields] =
  {"side_1", "side_2", "", "", ""};
std::string implies2t::field_names[esbmct::num_type_fields] =
  {"side_1", "side_2", "", "", ""};
std::string bitand2t::field_names[esbmct::num_type_fields] =
  {"side_1", "side_2", "", "", ""};
std::string bitor2t::field_names[esbmct::num_type_fields] =
  {"side_1", "side_2", "", "", ""};
std::string bitxor2t::field_names[esbmct::num_type_fields] =
  {"side_1", "side_2", "", "", ""};
std::string bitnand2t::field_names[esbmct::num_type_fields] =
  {"side_1", "side_2", "", "", ""};
std::string bitnor2t::field_names[esbmct::num_type_fields] =
  {"side_1", "side_2", "", "", ""};
std::string bitnxor2t::field_names[esbmct::num_type_fields] =
  {"side_1", "side_2", "", "", ""};
std::string lshr2t::field_names[esbmct::num_type_fields] =
  {"side_1", "side_2", "", "", ""};
std::string bitnot2t::field_names[esbmct::num_type_fields] =
  {"value", "", "", "", ""};
std::string neg2t::field_names[esbmct::num_type_fields] =
  {"value", "", "", "", ""};
std::string abs2t::field_names[esbmct::num_type_fields] =
  {"value", "", "", "", ""};
std::string add2t::field_names[esbmct::num_type_fields] =
  {"side_1", "side_2", "", "", ""};
std::string sub2t::field_names[esbmct::num_type_fields] =
  {"side_1", "side_2", "", "", ""};
std::string mul2t::field_names[esbmct::num_type_fields] =
  {"side_1", "side_2", "", "", ""};
std::string div2t::field_names[esbmct::num_type_fields] =
  {"side_1", "side_2", "", "", ""};
std::string ieee_add2t::field_names[esbmct::num_type_fields] =
  {"rounding_mode", "side_1", "side_2", "", "", ""};
std::string ieee_sub2t::field_names[esbmct::num_type_fields] =
  {"rounding_mode", "side_1", "side_2", "", "", ""};
std::string ieee_mul2t::field_names[esbmct::num_type_fields] =
  {"rounding_mode", "side_1", "side_2", "", "", ""};
std::string ieee_div2t::field_names[esbmct::num_type_fields] =
  {"rounding_mode", "side_1", "side_2", "", "", ""};
std::string ieee_fma2t::field_names[esbmct::num_type_fields] =
  {"rounding_mode", "value_1", "value_2", "value_3", "", ""};
std::string ieee_sqrt2t::field_names[esbmct::num_type_fields] =
  {"rounding_mode", "value", "", "", ""};
std::string modulus2t::field_names[esbmct::num_type_fields] =
  {"side_1", "side_2", "", "", ""};
std::string shl2t::field_names[esbmct::num_type_fields] =
  {"side_1", "side_2", "", "", ""};
std::string ashr2t::field_names[esbmct::num_type_fields] =
  {"side_1", "side_2", "", "", ""};
std::string same_object2t::field_names[esbmct::num_type_fields] =
  {"side_1", "side_2", "", "", ""};
std::string pointer_offset2t::field_names[esbmct::num_type_fields] =
  {"pointer_obj", "", "", "", ""};
std::string pointer_object2t::field_names[esbmct::num_type_fields] =
  {"pointer_obj", "", "", "", ""};
std::string pointer_capability2t::field_names[esbmct::num_type_fields] =
  {"pointer_obj", "", "", "", ""};
std::string address_of2t::field_names[esbmct::num_type_fields] =
  {"pointer_obj", "", "", "", ""};
std::string byte_extract2t::field_names[esbmct::num_type_fields] =
  {"source_value", "source_offset", "big_endian", "", ""};
std::string byte_update2t::field_names[esbmct::num_type_fields] =
  {"source_value", "source_offset", "update_value", "big_endian", ""};
std::string with2t::field_names[esbmct::num_type_fields] =
  {"source_value", "update_field", "update_value", "", ""};
std::string member2t::field_names[esbmct::num_type_fields] =
  {"source_value", "member_name", "", "", ""};
std::string index2t::field_names[esbmct::num_type_fields] =
  {"source_value", "index", "", "", ""};
std::string isnan2t::field_names[esbmct::num_type_fields] =
  {"value", "", "", "", ""};
std::string overflow2t::field_names[esbmct::num_type_fields] =
  {"operand", "", "", "", ""};
std::string overflow_cast2t::field_names[esbmct::num_type_fields] =
  {"operand", "bits", "", "", ""};
std::string overflow_neg2t::field_names[esbmct::num_type_fields] =
  {"operand", "", "", "", ""};
std::string unknown2t::field_names[esbmct::num_type_fields] =
  {"", "", "", "", ""};
std::string invalid2t::field_names[esbmct::num_type_fields] =
  {"", "", "", "", ""};
std::string null_object2t::field_names[esbmct::num_type_fields] =
  {"", "", "", "", ""};
std::string dynamic_object2t::field_names[esbmct::num_type_fields] =
  {"instance", "invalid", "unknown", "", ""};
std::string dereference2t::field_names[esbmct::num_type_fields] =
  {"pointer", "", "", "", ""};
std::string valid_object2t::field_names[esbmct::num_type_fields] =
  {"value", "", "", "", ""};
std::string deallocated_obj2t::field_names[esbmct::num_type_fields] =
  {"value", "", "", "", ""};
std::string dynamic_size2t::field_names[esbmct::num_type_fields] =
  {"value", "", "", "", ""};
std::string sideeffect2t::field_names[esbmct::num_type_fields] =
  {"operand", "size", "arguments", "alloctype", "kind"};
std::string code_block2t::field_names[esbmct::num_type_fields] =
  {"operands", "", "", "", ""};
std::string code_assign2t::field_names[esbmct::num_type_fields] =
  {"target", "source", "", "", ""};
std::string code_init2t::field_names[esbmct::num_type_fields] =
  {"target", "source", "", "", ""};
std::string code_decl2t::field_names[esbmct::num_type_fields] =
  {"value", "", "", "", ""};
std::string code_dead2t::field_names[esbmct::num_type_fields] =
  {"value", "", "", "", ""};
std::string code_printf2t::field_names[esbmct::num_type_fields] =
  {"operands", "", "", "", ""};
std::string code_expression2t::field_names[esbmct::num_type_fields] =
  {"operand", "", "", "", ""};
std::string code_return2t::field_names[esbmct::num_type_fields] =
  {"operand", "", "", "", ""};
std::string code_skip2t::field_names[esbmct::num_type_fields] =
  {"", "", "", "", ""};
std::string code_free2t::field_names[esbmct::num_type_fields] =
  {"operand", "", "", "", ""};
std::string code_goto2t::field_names[esbmct::num_type_fields] =
  {"target", "", "", "", ""};
std::string object_descriptor2t::field_names[esbmct::num_type_fields] =
  {"object", "offset", "alignment", "", ""};
std::string code_function_call2t::field_names[esbmct::num_type_fields] =
  {"return_sym", "function", "operands", "", ""};
std::string code_comma2t::field_names[esbmct::num_type_fields] =
  {"side_1", "side_2", "", "", ""};
std::string invalid_pointer2t::field_names[esbmct::num_type_fields] =
  {"pointer_obj", "", "", "", ""};
std::string code_asm2t::field_names[esbmct::num_type_fields] =
  {"value", "", "", "", ""};
std::string code_cpp_del_array2t::field_names[esbmct::num_type_fields] =
  {"value", "", "", "", ""};
std::string code_cpp_delete2t::field_names[esbmct::num_type_fields] =
  {"value", "", "", "", ""};
std::string code_cpp_catch2t::field_names[esbmct::num_type_fields] =
  {"exception_list", "", "", "", ""};
std::string code_cpp_throw2t::field_names[esbmct::num_type_fields] =
  {"operand", "exception_list", "", "", ""};
std::string code_cpp_throw_decl2t::field_names[esbmct::num_type_fields] =
  {"exception_list", "", "", "", ""};
std::string code_cpp_throw_decl_end2t::field_names[esbmct::num_type_fields] =
  {"exception_list", "", "", "", ""};
std::string isinf2t::field_names[esbmct::num_type_fields] =
  {"value", "", "", "", ""};
std::string isnormal2t::field_names[esbmct::num_type_fields] =
  {"value", "", "", "", ""};
std::string isfinite2t::field_names[esbmct::num_type_fields] =
  {"value", "", "", "", ""};
std::string signbit2t::field_names[esbmct::num_type_fields] =
  {"value", "", "", "", ""};
std::string popcount2t::field_names[esbmct::num_type_fields] =
  {"value", "", "", "", ""};
std::string bswap2t::field_names[esbmct::num_type_fields] =
  {"value", "", "", "", ""};
std::string concat2t::field_names[esbmct::num_type_fields] =
  {"forward", "aft", "", "", ""};
std::string extract2t::field_names[esbmct::num_type_fields] =
  {"from", "upper", "lower", "", ""};

// For CRCing to actually be accurate, expr/type ids mustn't overflow out of
// a byte. If this happens then a) there are too many exprs, and b) the expr
// crcing code has to change.
static_assert(type2t::end_type_id <= 256, "Type id overflow");
static_assert(expr2t::end_expr_id <= 256, "Expr id overflow");

template <>
void do_type2string<type2t::type_ids>(
  const type2t::type_ids &,
  unsigned int,
  std::string (&)[esbmct::num_type_fields],
  list_of_memberst &,
  unsigned int)
{
  // Do nothing; this is a dummy member.
}

template <>
void do_type2string<const expr2t::expr_ids>(
  const expr2t::expr_ids &,
  unsigned int,
  std::string (&)[esbmct::num_type_fields],
  list_of_memberst &,
  unsigned int)
{
  // Do nothing; this is a dummy member.
}

template <>
bool do_get_sub_expr<expr2tc>(
  const expr2tc &item,
  unsigned int idx,
  unsigned int &it,
  const expr2tc *&ptr)
{
  if (idx == it)
  {
    ptr = &item;
    return true;
  }
  else
  {
    it++;
    return false;
  }
}

template <>
bool do_get_sub_expr<std::vector<expr2tc>>(
  const std::vector<expr2tc> &item,
  unsigned int idx,
  unsigned int &it,
  const expr2tc *&ptr)
{
  if (idx < it + item.size())
  {
    ptr = &item[idx - it];
    return true;
  }
  else
  {
    it += item.size();
    return false;
  }
}

// Non-const versions of the above.
template <>
bool do_get_sub_expr_nc<expr2tc>(
  expr2tc &item,
  unsigned int idx,
  unsigned int &it,
  expr2tc *&ptr)
{
  if (idx == it)
  {
    ptr = &item;
    return true;
  }
  else
  {
    it++;
    return false;
  }
}

template <>
bool do_get_sub_expr_nc<std::vector<expr2tc>>(
  std::vector<expr2tc> &item,
  unsigned int idx,
  unsigned int &it,
  expr2tc *&ptr)
{
  if (idx < it + item.size())
  {
    ptr = &item[idx - it];
    return true;
  }
  else
  {
    it += item.size();
    return false;
  }
}

template <>
unsigned int do_count_sub_exprs<const expr2tc>(const expr2tc &)
{
  return 1;
}

template <>
unsigned int
do_count_sub_exprs<const std::vector<expr2tc>>(const std::vector<expr2tc> &item)
{
  return item.size();
}

template <>
void call_expr_delegate<const expr2tc, expr2t::const_op_delegate>(
  const expr2tc &ref,
  expr2t::const_op_delegate &f)
{
  f(ref);
}

template <>
void call_expr_delegate<expr2tc, expr2t::op_delegate>(
  expr2tc &ref,
  expr2t::op_delegate &f)
{
  f(ref);
}

template <>
void call_expr_delegate<const std::vector<expr2tc>, expr2t::const_op_delegate>(
  const std::vector<expr2tc> &ref,
  expr2t::const_op_delegate &f)
{
  for (const expr2tc &r : ref)
    f(r);
}

template <>
void call_expr_delegate<std::vector<expr2tc>, expr2t::op_delegate>(
  std::vector<expr2tc> &ref,
  expr2t::op_delegate &f)
{
  for (expr2tc &r : ref)
    f(r);
}

template <>
void call_type_delegate<const type2tc, type2t::const_subtype_delegate>(
  const type2tc &ref,
  type2t::const_subtype_delegate &f)
{
  f(ref);
}

template <>
void call_type_delegate<type2tc, type2t::subtype_delegate>(
  type2tc &ref,
  type2t::subtype_delegate &f)
{
  f(ref);
}

template <>
void call_type_delegate<
  const std::vector<type2tc>,
  type2t::const_subtype_delegate>(
  const std::vector<type2tc> &ref,
  type2t::const_subtype_delegate &f)
{
  for (const type2tc &r : ref)
    f(r);
}

template <>
void call_type_delegate<std::vector<type2tc>, type2t::subtype_delegate>(
  std::vector<type2tc> &ref,
  type2t::subtype_delegate &f)
{
  for (type2tc &r : ref)
    f(r);
}