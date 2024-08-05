#include <solidity-frontend/solidity_convert.h>
#include <util/arith_tools.h>
#include <util/bitvector.h>
#include <util/c_types.h>
#include <util/expr_util.h>
#include <util/ieee_float.h>
#include <util/string_constant.h>
#include <util/std_expr.h>

// Integer literal
bool solidity_convertert::convert_integer_literal(
  const nlohmann::json &integer_literal,
  std::string the_value,
  exprt &dest)
{
  typet type;
  if (get_type_description(integer_literal, type))
    return true;

  exprt the_val;
  // extract the value: unsigned
  BigInt z_ext_value = string2integer(the_value);
  the_val = constant_exprt(
    integer2binary(z_ext_value, bv_width(type)),
    integer2string(z_ext_value),
    type);

  dest.swap(the_val);
  return false;
}

bool solidity_convertert::convert_bool_literal(
  const nlohmann::json &bool_literal,
  std::string the_value,
  exprt &dest)
{
  typet type;
  if (get_type_description(bool_literal, type))
    return true;

  assert(type.is_bool());

  if (the_value == "true")
  {
    dest = true_exprt();
    return false;
  }

  if (the_value == "false")
  {
    dest = false_exprt();
    return false;
  }

  return true;
}

// TODO: Character literal
/**
 * @brief Converts the string literal to a string_constt
 * 
 * @param the_value the value of the literal
 * @param dest return reference
 * @return true Only if the function fails
 * @return false Only if the function successfully converts the literal
 */
bool solidity_convertert::convert_string_literal(
  std::string the_value,
  exprt &dest)
{
  size_t string_size = the_value.size();
  typet type = array_typet(
    signed_char_type(),
    constant_exprt(
      integer2binary(string_size, bv_width(int_type())),
      integer2string(string_size),
      int_type()));
  // TODO: Handle null terminator byte
  string_constantt string(the_value, type, string_constantt::k_default);
  dest.swap(string);
  dest.type().set("#sol_type", "STRING");

  return false;
}

/**
 * convert hex-string to uint constant
 * @n: the bit width, default 256 (unsignedbv_typet(256))
*/
bool solidity_convertert::convert_hex_literal(
  std::string the_value,
  exprt &dest,
  const int n)
{
  // remove "0x" prefix
  if (the_value.length() >= 2)
    if (the_value.substr(0, 2) == "0x")
    {
      the_value.erase(0, 2);
    }

  typet type;
  type = unsignedbv_typet(n);

  // e.g. 0x1f9840a85d5aF5bf1D1762F925BDADdC4201F984
  BigInt hex_addr = string2integer(the_value, 16);
  exprt the_val;
  the_val = constant_exprt(
    integer2binary(hex_addr, bv_width(type)), integer2string(hex_addr), type);

  dest.swap(the_val);
  return false;
}

// TODO: Float literal.
//    - Note: Currently Solidity does NOT support floating point data types or fp arithmetic.
//      Everything is done in fixed-point arithmetic as of Solidity compiler v0.8.6.
