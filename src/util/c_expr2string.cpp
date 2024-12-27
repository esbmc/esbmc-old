
#include <util/c_expr2string.h>
#include <util/arith_tools.h>
#include <util/c_misc.h>
#include <util/c_types.h>
#include <util/config.h>
#include <util/fixedbv.h>
#include <util/i2string.h>
#include <util/ieee_float.h>
#include <util/prefix.h>
#include <util/std_code.h>
#include <util/std_types.h>

std::string c_expr2stringt::id_shorthand(const exprt &expr) const
{
  const irep_idt &identifier = expr.identifier();
  const symbolt *symbol = ns.lookup(identifier);

  if (symbol)
    return id2string(symbol->name);

  std::string sh = id2string(identifier);

  std::string::size_type pos = sh.rfind("@");
  if (pos != std::string::npos)
    sh.erase(0, pos + 1);

  return sh;
}

void c_expr2stringt::get_symbols(const exprt &expr)
{
  if (expr.id() == "symbol")
    symbols.insert(expr);

  forall_operands (it, expr)
    get_symbols(*it);
}

void c_expr2stringt::get_shorthands(const exprt &expr)
{
  get_symbols(expr);

  for (const auto &symbol : symbols)
  {
    std::string sh = id_shorthand(symbol);

    std::pair<std::map<irep_idt, exprt>::iterator, bool> result =
      shorthands.insert(std::pair<irep_idt, exprt>(sh, symbol));

    if (!result.second)
      if (result.first->second != symbol)
      {
        ns_collision.insert(symbol.identifier());
        ns_collision.insert(result.first->second.identifier());
      }
  }
}

std::string c_expr2stringt::convert(const typet &src)
{
  return convert_rec(src, c_qualifierst(), "");
}

std::string c_expr2stringt::convert_rec(
  const typet &src,
  const c_qualifierst &qualifiers,
  const std::string &declarator)
{
  c_qualifierst new_qualifiers(qualifiers);
  new_qualifiers.read(src);

  std::string q = new_qualifiers.as_string();

  std::string d = declarator == "" ? declarator : " " + declarator;

  if (src.is_bool())
  {
    return q + "_Bool" + d;
  }
  if (src.id() == "empty")
  {
    return q + "void" + d;
  }
  else if (src.id() == "signedbv" || src.id() == "unsignedbv")
  {
    BigInt width = string2integer(src.width().as_string());

    bool is_signed = src.id() == "signedbv";
    std::string sign_str = is_signed ? "signed " : "unsigned ";

    if (width == config.ansi_c.int_width)
      return q + sign_str + "int" + d;

    if (width == config.ansi_c.long_int_width)
      return q + sign_str + "long int" + d;

    if (width == config.ansi_c.char_width)
      return q + sign_str + "char" + d;

    if (width == config.ansi_c.short_int_width)
      return q + sign_str + "short int" + d;

    if (width == config.ansi_c.long_long_int_width)
      return q + sign_str + "long long int" + d;

    return q + sign_str + "_ExtInt(" + std::to_string(width.to_uint64()) + ")" +
           d;
  }
  else if (src.id() == "floatbv" || src.id() == "fixedbv")
  {
    BigInt width = string2integer(src.width().as_string());

    if (width == config.ansi_c.single_width)
      return q + "float" + d;
    if (width == config.ansi_c.double_width)
      return q + "double" + d;
    else if (width == config.ansi_c.long_double_width)
      return q + "long double" + d;
  }
  else if (src.id() == "struct")
  {
    const struct_typet &struct_type = to_struct_type(src);

    std::string dest = q;

    const irep_idt &tag = struct_type.tag().as_string();
    if (tag != "")
      dest += " " + id2string(tag);

    dest += " {";

    for (const auto &component : struct_type.components())
    {
      dest += ' ';
      dest += convert_rec(
        component.type(), c_qualifierst(), id2string(component.get_name()));
      dest += ';';
    }

    dest += " }";
    dest += declarator;
    return dest;
  }
  else if (src.id() == "incomplete_struct")
  {
    std::string dest = q;
    const std::string &tag = src.tag().as_string();
    if (tag == "")
      dest += "struct";
    else
      dest += " " + tag;
    dest += d;
    return dest;
  }
  else if (src.id() == "union")
  {
    const union_typet &union_type = to_union_type(src);

    std::string dest = q;

    const irep_idt &tag = union_type.tag().as_string();
    if (tag != "")
      dest += " " + id2string(tag);
    dest += " {";
    for (auto const &it : union_type.components())
    {
      dest += ' ';
      dest += convert_rec(it.type(), c_qualifierst(), id2string(it.get_name()));
      dest += ';';
    }
    dest += " }";
    dest += d;
    return dest;
  }
  else if (src.id() == "c_enum" || src.id() == "incomplete_c_enum")
  {
    std::string result = q + "enum";
    if (src.name() != "")
      result += " " + src.tag().as_string();
    result += d;
    return result;
  }
  else if (src.id() == "pointer")
  {
    if (src.subtype().is_code())
    {
      const typet &return_type = (typet &)src.subtype().return_type();

      std::string dest = q + convert(return_type);

      // function "name"
      dest += " (*)";

      // arguments
      dest += "(";
      const irept &arguments = src.subtype().arguments();

      forall_irep (it, arguments.get_sub())
      {
        const typet &argument_type = ((exprt &)*it).type();

        if (it != arguments.get_sub().begin())
          dest += ", ";

        dest += convert(argument_type);
      }

      dest += ")";

      return dest;
    }

    std::string tmp = convert(src.subtype());

    if (q == "")
      return tmp + " *" + d;

    return q + " (" + tmp + " *)" + d;
  }
  else if (src.is_array())
  {
    std::string size_string =
      convert(static_cast<const exprt &>(src.size_irep()));
    return convert(src.subtype()) + " [" + size_string + "]" + d;
  }
  /** int vector [3]
   *   /          |
   * type        size
   */
  else if (src.is_vector())
  {
    std::string size_string =
      convert(static_cast<const exprt &>(src.size_irep()));
    return convert(src.subtype()) + " vector [" + size_string + "]" + d;
  }
  else if (src.id() == "incomplete_array")
  {
    return convert(src.subtype()) + " []";
  }
  else if (src.id() == "symbol")
  {
    const typet &followed = ns.follow(src);
    if (followed.id() == "struct")
    {
      std::string dest = q;
      const std::string &tag = followed.tag().as_string();
      if (tag != "")
        dest += " " + tag;
      dest += d;
      return dest;
    }

    if (followed.id() == "union")
    {
      std::string dest = q;
      const std::string &tag = followed.tag().as_string();
      if (tag != "")
        dest += " " + tag;
      dest += d;
      return dest;
    }

    return convert_rec(ns.follow(src), new_qualifiers, declarator);
  }
  else if (src.is_code())
  {
    const typet &return_type = (typet &)src.return_type();

    std::string dest = convert(return_type) + " ";

    dest += "(";
    const irept &arguments = src.arguments();

    forall_irep (it, arguments.get_sub())
    {
      const typet &argument_type = ((exprt &)*it).type();

      if (it != arguments.get_sub().begin())
        dest += ", ";

      dest += convert(argument_type);
    }

    dest += ")";
    return dest;
  }

  unsigned precedence;
  return convert_norep((exprt &)src, precedence);
}

std::string
c_expr2stringt::convert_typecast(const exprt &src, unsigned &precedence)
{
  precedence = 14;

  if (src.id() == "typecast" && src.operands().size() != 1)
    return convert_norep(src, precedence);

  // some special cases

  const typet &type = ns.follow(src.type());

  if (
    type.id() == "pointer" &&
    ns.follow(type.subtype()).id() == "empty" && // to (void *)?
    src.op0().is_zero())
    return "0";

  std::string dest;
  if (type.id() == "struct")
  {
    std::string dest;
    const std::string &tag = type.tag().as_string();
    assert(tag != "");
    dest += " " + tag;
    return dest;
  }
  if (type.id() == "union")
  {
    std::string dest;
    const std::string &tag = type.tag().as_string();
    assert(tag != "");
    dest += " " + tag;
  }
  else
    dest = "(" + convert(type) + ")";

  std::string tmp = convert(src.op0(), precedence);

  if (
    src.op0().id() == "member" || src.op0().id() == "constant" ||
    src.op0().id() == "symbol") // better fix precedence
    dest += tmp;
  else
    dest += '(' + tmp + ')';

  return dest;
}

std::string
c_expr2stringt::convert_bitcast(const exprt &src, unsigned &precedence)
{
  precedence = 14;

  if (src.id() == "bitcast" && src.operands().size() != 1)
    return convert_norep(src, precedence);

  // some special cases

  const typet &type = ns.follow(src.type());

  if (
    type.id() == "pointer" &&
    ns.follow(type.subtype()).id() == "empty" && // to (void *)?
    src.op0().is_zero())
    return "0";

  std::string dest = "(" + convert(type) + ")";

  std::string tmp = convert(src.op0(), precedence);

  if (
    src.op0().id() == "member" || src.op0().id() == "constant" ||
    src.op0().id() == "symbol") // better fix precedence
    dest += tmp;
  else
    dest += '(' + tmp + ')';

  return dest;
}

std::string c_expr2stringt::convert_implicit_address_of(
  const exprt &src,
  unsigned &precedence)
{
  if (src.operands().size() != 1)
    return convert_norep(src, precedence);

  return convert(src.op0(), precedence);
}

std::string c_expr2stringt::convert_trinary(
  const exprt &src,
  const std::string &symbol1,
  const std::string &symbol2,
  unsigned precedence)
{
  if (src.operands().size() != 3)
    return convert_norep(src, precedence);

  const exprt::operandst &operands = src.operands();
  const exprt &op0 = operands.front();
  const exprt &op1 = *(++operands.begin());
  const exprt &op2 = operands.back();

  unsigned p0, p1, p2;

  std::string s_op0 = convert(op0, p0);
  std::string s_op1 = convert(op1, p1);
  std::string s_op2 = convert(op2, p2);

  std::string dest;

  if (precedence > p0)
    dest += '(';
  dest += s_op0;
  if (precedence > p0)
    dest += ')';

  dest += ' ';
  dest += symbol1;
  dest += ' ';

  if (precedence > p1)
    dest += '(';
  dest += s_op1;
  if (precedence > p1)
    dest += ')';

  dest += ' ';
  dest += symbol2;
  dest += ' ';

  if (precedence > p2)
    dest += '(';
  dest += s_op2;
  if (precedence > p2)
    dest += ')';

  return dest;
}

std::string c_expr2stringt::convert_quantifier(
  const exprt &src,
  const std::string &symbol,
  unsigned precedence)
{
  if (src.operands().size() != 3)
    return convert_norep(src, precedence);

  unsigned p0, p2;

  std::string op0 = convert(src.op0(), p0);
  std::string op2 = convert(src.op2(), p2);

  std::string dest = symbol + " ";

  if (precedence > p0)
    dest += '(';
  dest += op0;
  if (precedence > p0)
    dest += ')';

  const exprt &instantiations = src.op1();
  if (instantiations.is_not_nil())
  {
    dest += " (";
    forall_operands (it, instantiations)
    {
      unsigned p;
      std::string inst = convert(*it, p);
      if (it != instantiations.operands().begin())
        dest += ", ";
      dest += inst;
    }
    dest += ")";
  }

  dest += ':';
  dest += ' ';

  if (precedence > p2)
    dest += '(';
  dest += op2;
  if (precedence > p2)
    dest += ')';

  return dest;
}

std::string c_expr2stringt::convert_with(const exprt &src, unsigned precedence)
{
  if (src.operands().size() < 3)
    return convert_norep(src, precedence);

  unsigned p0;
  std::string op0 = convert(src.op0(), p0);

  std::string dest;

  if (precedence > p0)
    dest += '(';
  dest += op0;
  if (precedence > p0)
    dest += ')';

  dest += " WITH [";

  for (unsigned i = 1; i < src.operands().size(); i += 2)
  {
    std::string op1, op2;
    unsigned p1, p2;

    if (i != 1)
      dest += ", ";

    if (src.operands()[i].id() == "member_name")
    {
      const irep_idt &component_name = src.operands()[i].component_name();

      const typet &full_type = ns.follow(src.op0().type());

      const struct_typet &struct_type = to_struct_type(full_type);

      const exprt comp_expr = struct_type.get_component(component_name);

      assert(comp_expr.is_not_nil());

      op1 = comp_expr.pretty_name().as_string();
    }
    else
      op1 = convert(src.operands()[i], p1);

    op2 = convert(src.operands()[i + 1], p2);

    dest += op1;
    dest += ":=";
    dest += op2;
  }

  dest += "]";

  return dest;
}

std::string c_expr2stringt::convert_cond(const exprt &src, unsigned precedence)
{
  if (src.operands().size() < 2)
    return convert_norep(src, precedence);

  bool condition = true;

  std::string dest = "cond {\n";

  forall_operands (it, src)
  {
    unsigned p;
    std::string op = convert(*it, p);

    if (condition)
      dest += "  ";

    dest += op;

    if (condition)
      dest += ": ";
    else
      dest += ";\n";

    condition = !condition;
  }

  dest += "} ";

  return dest;
}

std::string c_expr2stringt::convert_binary(
  const exprt &src,
  const std::string &symbol,
  unsigned precedence,
  bool full_parentheses)
{
  if (src.operands().size() < 2)
    return convert_norep(src, precedence);

  std::string dest;
  bool first = true;

  forall_operands (it, src)
  {
    if (first)
      first = false;
    else
    {
      if (symbol != ", ")
        dest += ' ';
      dest += symbol;
      dest += ' ';
    }

    unsigned p;
    std::string op = convert(*it, p);

    if (precedence > p || (precedence == p && full_parentheses))
      dest += '(';
    dest += op;
    if (precedence > p || (precedence == p && full_parentheses))
      dest += ')';
  }

  return dest;
}

std::string c_expr2stringt::convert_unary(
  const exprt &src,
  const std::string &symbol,
  unsigned precedence)
{
  if (src.operands().size() != 1)
    return convert_norep(src, precedence);

  unsigned p;
  std::string op = convert(src.op0(), p);

  std::string dest = symbol;
  if (precedence >= p)
    dest += '(';
  dest += op;
  if (precedence >= p)
    dest += ')';

  return dest;
}

std::string c_expr2stringt::convert_pointer_object_has_type(
  const exprt &src,
  unsigned precedence)
{
  if (src.operands().size() != 1)
    return convert_norep(src, precedence);

  unsigned p0;
  std::string op0 = convert(src.op0(), p0);

  std::string dest = "POINTER_OBJECT_HAS_TYPE";
  dest += '(';
  dest += op0;
  dest += ", ";
  dest += convert(static_cast<const typet &>(src.object_type()));
  dest += ')';

  return dest;
}

std::string
c_expr2stringt::convert_alloca(const exprt &src, unsigned &precedence)
{
  if (src.operands().size() != 1)
    return convert_norep(src, precedence);

  unsigned p0;
  std::string op0 = convert(src.op0(), p0);

  std::string dest = "ALLOCA";
  dest += '(';
  dest += convert((const typet &)src.cmt_type());
  dest += ", ";
  dest += op0;
  dest += ')';

  return dest;
}

std::string
c_expr2stringt::convert_realloc(const exprt &src, unsigned &precedence)
{
  if (src.operands().size() != 1)
    return convert_norep(src, precedence);

  unsigned p0, p1;
  std::string op0 = convert(src.op0(), p0);
  std::string size = convert((const exprt &)src.cmt_size(), p1);

  std::string dest = "REALLOC";
  dest += '(';
  dest += op0;
  dest += ", ";
  dest += size;
  dest += ')';

  return dest;
}

std::string
c_expr2stringt::convert_malloc(const exprt &src, unsigned &precedence)
{
  if (src.operands().size() != 1)
    return convert_norep(src, precedence);

  unsigned p0;
  std::string op0 = convert(src.op0(), p0);

  std::string dest = "MALLOC";
  dest += '(';
  dest += convert((const typet &)src.cmt_type());
  dest += ", ";
  dest += op0;
  dest += ')';

  return dest;
}

std::string
c_expr2stringt::convert_nondet(const exprt &src, unsigned &precedence)
{
  if (src.operands().size() != 0)
    return convert_norep(src, precedence);

  return "NONDET(" + convert(src.type()) + ")";
}

std::string c_expr2stringt::convert_statement_expression(
  const exprt &src,
  unsigned &precedence)
{
  if (
    src.operands().size() != 1 || to_code(src.op0()).get_statement() != "block")
    return convert_norep(src, precedence);

  return "(" + convert_code(to_code_block(to_code(src.op0())), 0) + ")";
}

std::string c_expr2stringt::convert_function(
  const exprt &src,
  const std::string &name,
  unsigned)
{
  std::string dest = name;
  dest += '(';

  forall_operands (it, src)
  {
    unsigned p;
    std::string op = convert(*it, p);

    if (it != src.operands().begin())
      dest += ", ";

    dest += op;
  }

  dest += ')';

  return dest;
}

std::optional<std::string> c_expr2stringt::is_recursively_zero(const exprt &op)
{
  if (~flags & SHORT_ZERO_COMPOUNDS)
    return {};

  if (op.id() == exprt::arrayof)
  {
    if (auto init = is_recursively_zero(op.op0()))
      return "{ " + *init + " }";
    return {};
  }

  const typet &ty = ns.follow(op.type());
  if (op.is_zero())
  {
    unsigned precedence;
    return convert_constant(op, precedence);
  }

  if (ty.incomplete())
    return {};

  if (
    (op.is_constant() && is_array_like(ty)) || op.id() == typet::t_array ||
    op.id() == typet::t_vector)
  {
    /* fixed size array */
    const exprt &sz = static_cast<const exprt &>(ty.find(typet::a_size));
    if (!sz.is_constant())
      return {};
    BigInt N = binary2integer(sz.value().as_string(), sz.type().is_signedbv());
    if (!N.is_uint64() || N.is_negative())
      return {};
    uint64_t n = N.to_uint64();
    if (size_t k = op.operands().size(); k < n)
      n = k;
    std::string first_init;
    for (unsigned i = 0; i < n; i++)
      if (auto init = is_recursively_zero(op.operands()[i]); !init)
        return {};
      else if (!i)
        first_init = std::move(*init);
    if (n)
      return "{ " + first_init + " }";
    return "{}";
  }

  if (op.id() == typet::t_struct || op.id() == typet::t_union)
  {
    /* constant struct or union */
    const struct_union_typet::componentst &cc =
      to_struct_union_type(ty).components();
    size_t n = cc.size();
    if (n != op.operands().size())
      return {};
    if (n == 0)
      return "{}";
    std::optional<std::string> first;
    for (size_t i = 0; i < n; i++)
    {
      if (cc[i].type().is_code())
        return {};
      const exprt &e = op.operands()[i];
      if (cc[i].get_is_padding())
      {
        if (e.is_zero())
          continue;
        return {};
      }
      if (auto init = is_recursively_zero(e); !init)
        return {};
      else if (!first)
        first = "{ ." + cc[i].pretty_name().as_string() + " = " + *init + " }";
    }
    return first;
  }

  return {};
}

std::string
c_expr2stringt::convert_array_of(const exprt &src, unsigned precedence)
{
  if (src.operands().size() != 1)
    return convert_norep(src, precedence);

  /* Maybe there's no need to copy the initializer N times, where N is the
   * array size? */
  if (auto init = is_recursively_zero(src))
    return *init;

  std::string init = convert(src.op0());

  const typet &ty = ns.follow(src.type());
  const exprt &sz = static_cast<const exprt &>(ty.find(typet::a_size));
  if (sz.is_constant())
  {
    /* It appears that we need to duplicate the init terms... */
    BigInt N = binary2integer(sz.value().as_string(), sz.type().is_signedbv());
    if (N.is_uint64() && !N.is_negative())
    {
      uint64_t n = N.to_uint64();
      std::ostringstream oss;
      oss << "{ ";
      for (size_t i = 0; i < n; i++)
      {
        if (i)
          oss << ", ";
        oss << init;
      }
      oss << " }";
      return oss.str();
    }
  }

  return "ARRAY_OF(" + init + ')';
}

std::string
c_expr2stringt::convert_byte_extract(const exprt &src, unsigned precedence)
{
  if (src.operands().size() != 2)
    return convert_norep(src, precedence);

  unsigned p0;
  std::string op0 = convert(src.op0(), p0);

  unsigned p1;
  std::string op1 = convert(src.op1(), p1);

  std::string dest = src.id_string();
  dest += '(';
  dest += op0;
  dest += ", ";
  dest += op1;
  dest += ')';

  return dest;
}

std::string
c_expr2stringt::convert_byte_update(const exprt &src, unsigned precedence)
{
  if (src.operands().size() != 3)
    return convert_norep(src, precedence);

  unsigned p0;
  std::string op0 = convert(src.op0(), p0);

  unsigned p1;
  std::string op1 = convert(src.op1(), p1);

  unsigned p2;
  std::string op2 = convert(src.op2(), p2);

  std::string dest = src.id_string();
  dest += '(';
  dest += op0;
  dest += ", ";
  dest += op1;
  dest += ", ";
  dest += op2;
  dest += ')';

  return dest;
}

std::string c_expr2stringt::convert_unary_post(
  const exprt &src,
  const std::string &symbol,
  unsigned precedence)
{
  if (src.operands().size() != 1)
    return convert_norep(src, precedence);

  unsigned p;
  std::string op = convert(src.op0(), p);

  std::string dest;
  if (precedence > p)
    dest += '(';
  dest += op;
  if (precedence > p)
    dest += ')';
  dest += symbol;

  return dest;
}

std::string c_expr2stringt::convert_index(const exprt &src, unsigned precedence)
{
  if (src.operands().size() != 2)
    return convert_norep(src, precedence);

  unsigned p;
  std::string op = convert(src.op0(), p);

  std::string dest;
  if (precedence > p)
    dest += '(';
  dest += op;
  if (precedence > p)
    dest += ')';

  dest += '[';
  dest += convert(src.op1());
  dest += ']';

  return dest;
}

std::string
c_expr2stringt::convert_member(const exprt &src, unsigned precedence)
{
  if (src.operands().size() != 1)
    return convert_norep(src, precedence);

  unsigned p;
  std::string dest;

  if (src.op0().id() == "dereference" && src.operands().size() == 1)
  {
    std::string op = convert(src.op0().op0(), p);

    if (precedence > p)
      dest += '(';
    dest += op;
    if (precedence > p)
      dest += ')';

    dest += "->";
  }
  else
  {
    std::string op = convert(src.op0(), p);

    if (precedence > p)
      dest += '(';
    dest += op;
    if (precedence > p)
      dest += ')';

    dest += '.';
  }

  const typet &full_type = ns.follow(src.op0().type());

  // It might be an flattened union
  // This will look very odd when printing, but it's better then
  // the norep output
  if (full_type.id() == "array")
    return convert_array(src, precedence);

  if (full_type.id() != "struct" && full_type.id() != "union")
    return convert_norep(src, precedence);

  const struct_typet &struct_type = to_struct_type(full_type);

  const exprt comp_expr = struct_type.get_component(src.component_name());

  if (comp_expr.is_nil())
    return convert_norep(src, precedence);

  dest += comp_expr.pretty_name().as_string();

  return dest;
}

std::string c_expr2stringt::convert_array_member_value(
  const exprt &src,
  unsigned precedence)
{
  if (src.operands().size() != 1)
    return convert_norep(src, precedence);

  return "[]=" + convert(src.op0());
}

std::string c_expr2stringt::convert_struct_member_value(
  const exprt &src,
  unsigned precedence)
{
  if (src.operands().size() != 1)
    return convert_norep(src, precedence);

  return "." + src.name().as_string() + "=" + convert(src.op0());
}

std::string c_expr2stringt::convert_norep(const exprt &src, unsigned &)
{
  return src.pretty(0);
}

std::string c_expr2stringt::convert_symbol(const exprt &src, unsigned &)
{
  const irep_idt &id = src.identifier();
  std::string dest;

  if (ns_collision.find(id) == ns_collision.end())
    dest = id_shorthand(src);
  else
    dest = id2string(id);

  if (src.id() == "next_symbol")
    dest = "NEXT(" + dest + ")";

  if (dest.find("__ESBMC_") == 0)
    dest.erase(0, 8); // Remove __ESBMC_

  return dest;
}

std::string c_expr2stringt::convert_nondet_symbol(const exprt &src, unsigned &)
{
  const std::string &id = src.identifier().as_string();
  return "nondet_symbol(" + id + ")";
}

std::string
c_expr2stringt::convert_predicate_symbol(const exprt &src, unsigned &)
{
  const std::string &id = src.identifier().as_string();
  return "ps(" + id + ")";
}

std::string
c_expr2stringt::convert_predicate_next_symbol(const exprt &src, unsigned &)
{
  const std::string &id = src.identifier().as_string();
  return "pns(" + id + ")";
}

std::string
c_expr2stringt::convert_quantified_symbol(const exprt &src, unsigned &)
{
  const std::string &id = src.identifier().as_string();
  return id;
}

std::string c_expr2stringt::convert_nondet_bool(const exprt &, unsigned &)
{
  return "nondet_bool()";
}

std::string c_expr2stringt::convert_object_descriptor(
  const exprt &src,
  unsigned &precedence)
{
  if (src.operands().size() != 2)
    return convert_norep(src, precedence);

  std::string result = "<";

  result += convert(src.op0());
  result += ", ";
  result += convert(src.op1());
  result += ", ";
  result += convert(src.type());

  result += ">";

  return result;
}

std::string
c_expr2stringt::convert_constant(const exprt &src, unsigned &precedence)
{
  const typet &type = ns.follow(src.type());
  const std::string &cformat = src.cformat().as_string();
  const std::string &value = src.value().as_string();
  std::string dest;

  if (cformat != "")
    dest = cformat;
  else if (src.id() == "string-constant")
  {
    dest = '"';
    MetaString(dest, value);
    dest += '"';
  }
  else if (type.id() == "c_enum" || type.id() == "incomplete_c_enum")
  {
    BigInt int_value = string2integer(value);
    BigInt i = 0;
    const irept &body = type.body();

    forall_irep (it, body.get_sub())
    {
      if (i == int_value)
      {
        dest = it->name().as_string();
        return dest;
      }

      ++i;
    }

    // failed...
    dest = "enum(" + value + ")";

    return dest;
  }
  else if (type.id() == "bv")
    dest = value;
  else if (type.is_bool())
  {
    dest = src.is_true() ? "1" : "0";
  }
  else if (type.id() == "unsignedbv" || type.id() == "signedbv")
  {
    BigInt int_value = binary2integer(value, type.id() == "signedbv");
    BigInt llong_ub = BigInt::power2(config.ansi_c.long_long_int_width - 1);
    if (int_value == -llong_ub)
    {
      dest = integer2string(int_value + 1);
      dest += " - 1";
    }
    else if (int_value >= llong_ub)
    {
      dest = "0x" + integer2string(int_value, 16);
    }
    else
      dest = integer2string(int_value);
  }
  else if (type.id() == "floatbv")
  {
    format_spect format;
    ieee_floatt flt(to_constant_expr(src));
    if (flags & UNIQUE_FLOAT_REPR)
    {
      /* XXX: Use some algorithm to compute the minimum precision, Grisu3 or
       * similar. While that's not there, use the maximum defined by the type */
      format.precision = ceil(flt.spec.f / 3.32); /* 3.32 < log_2(10) */
    }
    dest = flt.format(format);

    if (dest != "" && isdigit(dest[dest.size() - 1]))
    {
      if (src.type() == float_type())
        dest += "f";
      else if (src.type() == long_double_type())
        dest += "l";
    }
  }
  else if (type.id() == "fixedbv")
  {
    dest = fixedbvt(to_constant_expr(src)).to_ansi_c_string();

    if (dest != "" && isdigit(dest[dest.size() - 1]))
    {
      if (src.type() == float_type())
        dest += "f";
      else if (src.type() == long_double_type())
        dest += "l";
    }
  }
  else if (is_array_like(type))
  {
    dest = "{ ";

    forall_operands (it, src)
    {
      std::string tmp = convert(*it);

      if ((it + 1) != src.operands().end())
      {
        tmp += ", ";
        if (tmp.size() > 40)
          tmp += "\n    ";
      }

      dest += tmp;
    }

    dest += " }";
  }
  else if (type.id() == "pointer")
  {
    if (value == "NULL")
      dest = "0";
    else if (value == "INVALID" || std::string(value, 0, 8) == "INVALID-")
      dest = value;
    else
      return convert_norep(src, precedence);
  }
  else
    return convert_norep(src, precedence);

  return dest;
}

std::string c_expr2stringt::convert_struct_union_body(
  const exprt &src,
  const exprt::operandst &operands,
  const struct_union_typet::componentst &components)
{
  size_t n = components.size();
  assert(n == operands.size());

  if (auto init = is_recursively_zero(src))
    return *init;

  std::string dest = "{ ";

  bool first = true;
  bool newline = false;
  unsigned last_size = 0;

  for (size_t i = 0; i < n; i++)
  {
    const auto &operand = operands[i];
    const auto &component = components[i];

    if (component.type().is_code())
      continue;

    if (component.get_is_padding())
      continue;

    if (first)
      first = false;
    else
    {
      dest += ",";

      if (newline)
        dest += "\n    ";
      else
        dest += " ";
    }

    std::string tmp = convert(operand);

    if (last_size + 40 < dest.size())
    {
      newline = true;
      last_size = dest.size();
    }
    else
      newline = false;

    dest += ".";
    dest += component.pretty_name().as_string();
    dest += "=";
    dest += tmp;
  }

  dest += " }";

  return dest;
}

std::string
c_expr2stringt::convert_struct(const exprt &src, unsigned &precedence)
{
  const typet full_type = ns.follow(src.type());

  if (full_type.id() != "struct")
    return convert_norep(src, precedence);

  const struct_typet &struct_type = to_struct_type(full_type);
  const struct_union_typet::componentst &components = struct_type.components();

  if (components.size() != src.operands().size())
    return convert_norep(src, precedence);

  return convert_struct_union_body(src, src.operands(), components);
}

std::string
c_expr2stringt::convert_union(const exprt &src, unsigned &precedence)
{
  const typet full_type = ns.follow(src.type());

  if (full_type.id() != "union")
    return convert_norep(src, precedence);

  const exprt::operandst &operands = src.operands();
  const irep_idt &init = src.component_name();

  if (operands.size() == 1 && !init.empty())
  {
    std::string dest = "{ ";

    std::string tmp = convert(src.op0());

    dest += ".";
    dest += init.as_string();
    dest += "=";
    dest += tmp;

    dest += " }";

    return dest;
  }
  else
  {
    /* Initializer unknown, expect operands assigned to each member and convert
     * all of them */
    assert(init.empty());
    return convert_struct_union_body(
      src, operands, to_union_type(full_type).components());
  }
}

std::string c_expr2stringt::convert_array(const exprt &src, unsigned &)
{
  if (auto init = is_recursively_zero(src))
    return *init;

  std::string dest = "{ ";

  forall_operands (it, src)
  {
    std::string tmp;

    if (it->is_not_nil())
      tmp = convert(*it);

    if ((it + 1) != src.operands().end())
    {
      tmp += ", ";
      if (tmp.size() > 40)
        tmp += "\n    ";
    }

    dest += tmp;
  }

  dest += " }";

  return dest;
}

std::string
c_expr2stringt::convert_array_list(const exprt &src, unsigned &precedence)
{
  std::string dest = "{ ";

  if ((src.operands().size() % 2) != 0)
    return convert_norep(src, precedence);

  forall_operands (it, src)
  {
    std::string tmp1 = convert(*it);

    it++;

    std::string tmp2 = convert(*it);

    std::string tmp = "[" + tmp1 + "]=" + tmp2;

    if ((it + 1) != src.operands().end())
    {
      tmp += ", ";
      if (tmp.size() > 40)
        tmp += "\n    ";
    }

    dest += tmp;
  }

  dest += " }";

  return dest;
}

std::string c_expr2stringt::convert_function_call(const exprt &src, unsigned &)
{
  if (src.operands().size() != 2)
  {
    unsigned precedence;
    return convert_norep(src, precedence);
  }

  std::string dest;

  {
    unsigned p;
    std::string function_str = convert(src.op0(), p);
    dest += function_str;
  }

  dest += "(";

  unsigned i = 0;

  forall_operands (it, src.op1())
  {
    unsigned p;
    std::string arg_str = convert(*it, p);

    if (i > 0)
      dest += ", ";
    // TODO: [add] brackets, if necessary, depending on p
    dest += arg_str;

    i++;
  }

  dest += ")";

  return dest;
}

std::string
c_expr2stringt::convert_overflow(const exprt &src, unsigned &precedence)
{
  precedence = 16;

  std::string dest = "overflow(\"";
  dest += src.id().c_str() + 9;
  dest += "\"";

  forall_operands (it, src)
  {
    unsigned p;
    std::string arg_str = convert(*it, p);

    dest += ", ";
    // TODO: [add] brackets, if necessary, depending on p
    dest += arg_str;
  }

  dest += ")";

  return dest;
}

std::string c_expr2stringt::indent_str(unsigned indent)
{
  std::string dest;
  for (unsigned j = 0; j < indent; j++)
    dest += ' ';
  return dest;
}

std::string c_expr2stringt::convert_code_asm(const codet &, unsigned indent)
{
  std::string dest = indent_str(indent);
  dest += "asm();\n";
  return dest;
}

std::string
c_expr2stringt::convert_code_while(const codet &src, unsigned indent)
{
  if (src.operands().size() != 2)
  {
    unsigned precedence;
    return convert_norep(src, precedence);
  }

  std::string dest = indent_str(indent);
  dest += "while(" + convert(src.op0());

  if (src.op1().is_nil())
    dest += ");\n";
  else
  {
    dest += ")\n";
    dest += convert_code(to_code(src.op1()), indent + 2);
  }

  dest += "\n";

  return dest;
}

std::string
c_expr2stringt::convert_code_dowhile(const codet &src, unsigned indent)
{
  if (src.operands().size() != 2)
  {
    unsigned precedence;
    return convert_norep(src, precedence);
  }

  std::string dest = indent_str(indent);

  if (src.op1().is_nil())
    dest += "do; ";
  else
  {
    dest += "do\n";
    dest += convert_code(to_code(src.op1()), indent + 2);
    dest += indent_str(indent);
  }

  dest += "while(" + convert(src.op0()) + ");\n";

  dest += "\n";

  return dest;
}

std::string
c_expr2stringt::convert_code_ifthenelse(const codet &src, unsigned indent)
{
  if (src.operands().size() != 3 && src.operands().size() != 2)
  {
    unsigned precedence;
    return convert_norep(src, precedence);
  }

  std::string dest = indent_str(indent);
  dest += "if(" + convert(src.op0()) + ")\n";

  if (src.op1().is_nil())
  {
    dest += indent_str(indent + 2);
    dest += ";\n";
  }
  else
    dest += convert_code(to_code(src.op1()), indent + 2);

  if (src.operands().size() == 3 && !src.operands().back().is_nil())
  {
    dest += indent_str(indent);
    dest += "else\n";
    dest += convert_code(to_code(src.operands().back()), indent + 2);
  }

  dest += "\n";

  return dest;
}

std::string
c_expr2stringt::convert_code_return(const codet &src, unsigned indent)
{
  if (src.operands().size() != 0 && src.operands().size() != 1)
  {
    unsigned precedence;
    return convert_norep(src, precedence);
  }

  std::string dest = indent_str(indent);
  dest += "return";

  if (to_code_return(src).has_return_value())
    dest += " " + convert(src.op0());

  dest += ";\n";

  return dest;
}

std::string c_expr2stringt::convert_code_goto(const codet &src, unsigned indent)
{
  std::string dest = indent_str(indent);
  dest += "goto ";
  dest += src.destination().as_string();
  dest += ";\n";

  return dest;
}

std::string
c_expr2stringt::convert_code_gcc_goto(const codet &src, unsigned indent)
{
  std::string dest = indent_str(indent);
  dest += "goto ";
  dest += convert(src.op0(), indent);
  dest += ";\n";

  return dest;
}

std::string c_expr2stringt::convert_code_break(const codet &, unsigned indent)
{
  std::string dest = indent_str(indent);
  dest += "break";
  dest += ";\n";

  return dest;
}

std::string
c_expr2stringt::convert_code_switch(const codet &src, unsigned indent)
{
  if (src.operands().size() < 1)
  {
    unsigned precedence;
    return convert_norep(src, precedence);
  }

  std::string dest = indent_str(indent);
  dest += "switch(";
  dest += convert(src.op0());
  dest += ")\n";

  dest += indent_str(indent);
  dest += "{\n";

  for (unsigned i = 1; i < src.operands().size(); i++)
  {
    const exprt &op = src.operands()[i];

    if (op.statement() != "block")
    {
      unsigned precedence;
      dest += convert_norep(op, precedence);
    }
    else
    {
      forall_operands (it, op)
        dest += convert_code(to_code(*it), indent + 2);
    }
  }

  dest += "\n";
  dest += indent_str(indent);
  dest += '}';

  return dest;
}

std::string
c_expr2stringt::convert_code_continue(const codet &, unsigned indent)
{
  std::string dest = indent_str(indent);
  dest += "continue";
  dest += ";\n";

  return dest;
}

std::string
c_expr2stringt::convert_code_decl_block(const codet &src, unsigned indent)
{
  std::string dest = indent_str(indent);

  forall_operands (it, src)
  {
    dest += convert_code(to_code(*it), indent);
    dest += "\n";
  }

  return dest;
}

std::string c_expr2stringt::convert_code_dead(const codet &src, unsigned indent)
{
  // initializer to go away
  if (src.operands().size() != 1)
  {
    unsigned precedence;
    return convert_norep(src, precedence);
  }

  return indent_str(indent) + "dead " + convert(src.op0()) + ";";
}

std::string c_expr2stringt::convert_code_decl(const codet &src, unsigned indent)
{
  if (src.operands().size() != 1 && src.operands().size() != 2)
  {
    unsigned precedence;
    return convert_norep(src, precedence);
  }

  std::string declarator = convert(src.op0());

  std::string dest = indent_str(indent);

  const symbolt *symbol = ns.lookup(to_symbol_expr(src.op0()).get_identifier());
  if (symbol)
  {
    if (
      symbol->file_local &&
      (src.op0().type().is_code() || symbol->static_lifetime))
      dest += "static ";
    else if (symbol->is_extern)
      dest += "extern ";

    if (symbol->type.is_code() && to_code_type(symbol->type).get_inlined())
      dest += "inline ";
  }

  const typet &followed = ns.follow(src.op0().type());
  if (followed.id() == "struct")
  {
    const std::string &tag = followed.tag().as_string();
    if (tag != "")
      dest += tag + " ";
    dest += declarator;
  }
  else if (followed.id() == "union")
  {
    const std::string &tag = followed.tag().as_string();
    if (tag != "")
      dest += tag + " ";
    dest += declarator;
  }
  else
    dest += convert_rec(src.op0().type(), c_qualifierst(), declarator);

  if (src.operands().size() == 2)
    dest += "=" + convert(src.op1());

  dest += ';';

  return dest;
}

std::string c_expr2stringt::convert_code_for(const codet &src, unsigned indent)
{
  if (src.operands().size() != 4)
  {
    unsigned precedence;
    return convert_norep(src, precedence);
  }

  std::string dest = indent_str(indent);
  dest += "for(";

  unsigned i;
  for (i = 0; i <= 2; i++)
  {
    if (!src.operands()[i].is_nil())
    {
      if (i != 0)
        dest += " ";
      dest += convert(src.operands()[i]);
    }

    if (i != 2)
      dest += ";";
  }

  if (src.op3().is_nil())
    dest += ");\n";
  else
  {
    dest += ")\n";
    dest += convert_code(to_code(src.op3()), indent + 2);
  }

  dest += "\n";

  return dest;
}

std::string
c_expr2stringt::convert_code_block(const codet &src, unsigned indent)
{
  std::string dest = indent_str(indent);
  dest += "\n{\n";

  forall_operands (it, src)
  {
    if (it->statement() == "block")
      dest += convert_code_block(to_code(*it), indent + 2);
    else
      dest += convert_code(to_code(*it), indent);
    dest += "\n";
  }

  dest += indent_str(indent);
  dest += "}";

  return dest;
}

std::string
c_expr2stringt::convert_code_expression(const codet &src, unsigned indent)
{
  std::string dest = indent_str(indent);

  std::string expr_str;
  if (src.operands().size() == 1)
    expr_str = convert(src.op0());
  else
  {
    unsigned precedence;
    expr_str = convert_norep(src, precedence);
  }

  dest += expr_str + ";";

  dest += "\n";
  return dest;
}

std::string c_expr2stringt::convert_code(const codet &src, unsigned indent)
{
  const irep_idt &statement = src.statement();

  if (statement == "expression")
    return convert_code_expression(src, indent);

  if (statement == "block")
    return convert_code_block(src, indent);

  if (statement == "switch")
    return convert_code_switch(src, indent);

  if (statement == "for")
    return convert_code_for(src, indent);

  if (statement == "while")
    return convert_code_while(src, indent);

  if (statement == "asm")
    return convert_code_asm(src, indent);

  if (statement == "skip")
    return indent_str(indent) + ";\n";

  if (statement == "dowhile")
    return convert_code_dowhile(src, indent);

  if (statement == "ifthenelse")
    return convert_code_ifthenelse(src, indent);

  if (statement == "return")
    return convert_code_return(src, indent);

  if (statement == "goto")
    return convert_code_goto(src, indent);

  if (statement == "gcc_goto")
    return convert_code_gcc_goto(src, indent);

  if (statement == "printf")
    return convert_code_printf(src, indent);

  if (statement == "assume")
    return convert_code_assume(src, indent);

  if (statement == "assert")
    return convert_code_assert(src, indent);

  if (statement == "break")
    return convert_code_break(src, indent);

  if (statement == "continue")
    return convert_code_continue(src, indent);

  if (statement == "decl")
    return convert_code_decl(src, indent);

  if (statement == "decl-block")
    return convert_code_decl_block(src, indent);

  if (statement == "dead")
    return convert_code_dead(src, indent);

  if (statement == "assign")
    return convert_code_assign(src, indent);

  if (statement == "init")
    return convert_code_init(src, indent);

  if (statement == "lock")
    return convert_code_lock(src, indent);

  if (statement == "unlock")
    return convert_code_unlock(src, indent);

  if (statement == "function_call")
    return convert_code_function_call(to_code_function_call(src), indent);

  if (statement == "label")
    return convert_code_label(to_code_label(src), indent);

  if (statement == "switch_case")
    return convert_code_switch_case(to_code_switch_case(src), indent);

  if (statement == "free")
    return convert_code_free(src, indent);

  unsigned precedence;
  return convert_norep(src, precedence);
}

std::string
c_expr2stringt::convert_code_assign(const codet &src, unsigned indent)
{
  // Union remangle: If the right hand side is a constant array, containing
  // byte extract expressions, then it's almost 100% certain to be a flattened
  // union literal. Precise identification isn't feasible right now, sadly.
  // In that case, replace with a special intrinsic indicating to the user that
  // the original code is now meaningless.
  unsigned int precedent = 15;
  std::string tmp = convert(src.op0(), precedent);
  tmp += "=";
  tmp += convert(src.op1(), precedent);

  std::string dest = indent_str(indent) + tmp + ";";

  return dest;
}

std::string c_expr2stringt::convert_code_free(const codet &src, unsigned indent)
{
  if (src.operands().size() != 1)
  {
    unsigned precedence;
    return convert_norep(src, precedence);
  }

  return indent_str(indent) + "FREE(" + convert(src.op0()) + ");";
}

std::string c_expr2stringt::convert_code_init(const codet &src, unsigned indent)
{
  std::string tmp = convert_binary(src, "=", 2, true);

  return indent_str(indent) + "INIT " + tmp + ";";
}

std::string c_expr2stringt::convert_code_lock(const codet &src, unsigned indent)
{
  if (src.operands().size() != 1)
  {
    unsigned precedence;
    return convert_norep(src, precedence);
  }

  return indent_str(indent) + "LOCK(" + convert(src.op0()) + ");";
}

std::string
c_expr2stringt::convert_code_unlock(const codet &src, unsigned indent)
{
  if (src.operands().size() != 1)
  {
    unsigned precedence;
    return convert_norep(src, precedence);
  }

  return indent_str(indent) + "UNLOCK(" + convert(src.op0()) + ");";
}

std::string c_expr2stringt::convert_code_function_call(
  const code_function_callt &src,
  unsigned)
{
  if (src.operands().size() != 3)
  {
    unsigned precedence;
    return convert_norep(src, precedence);
  }

  std::string dest;

  if (src.lhs().is_not_nil())
  {
    unsigned p;
    std::string lhs_str = convert(src.lhs(), p);

    // TODO: [add] brackets, if necessary, depending on p
    dest += lhs_str;
    dest += "=";
  }

  {
    unsigned p;
    std::string function_str = convert(src.function(), p);
    dest += function_str;
  }

  dest += "(";

  unsigned i = 0;

  const exprt::operandst &arguments = src.arguments();

  forall_expr (it, arguments)
  {
    unsigned p;
    std::string arg_str = convert(*it, p);

    if (i > 0)
      dest += ", ";
    // TODO: [add] brackets, if necessary, depending on p
    dest += arg_str;

    i++;
  }

  dest += ")";

  return dest;
}

std::string
c_expr2stringt::convert_code_printf(const codet &src, unsigned indent)
{
  std::string dest = indent_str(indent) + "PRINTF(";

  forall_operands (it, src)
  {
    unsigned p;
    std::string arg_str = convert(*it, p);

    if (it != src.operands().begin())
      dest += ", ";
    // TODO: [add] brackets, if necessary, depending on p
    dest += arg_str;
  }

  dest += ");";

  return dest;
}

std::string
c_expr2stringt::convert_code_assert(const codet &src, unsigned indent)
{
  if (src.operands().size() != 1)
  {
    unsigned precedence;
    return convert_norep(src, precedence);
  }

  return indent_str(indent) + "assert(" + convert(src.op0()) + ");";
}

std::string
c_expr2stringt::convert_code_assume(const codet &src, unsigned indent)
{
  if (src.operands().size() != 1)
  {
    unsigned precedence;
    return convert_norep(src, precedence);
  }

  return indent_str(indent) + "assume(" + convert(src.op0()) + ");";
}

std::string
c_expr2stringt::convert_code_label(const code_labelt &src, unsigned indent)
{
  std::string labels_string;

  irep_idt label = src.get_label();

  labels_string += "\n";
  labels_string += indent_str(indent);
  labels_string += name2string(label);
  labels_string += ":\n";

  std::string tmp = convert_code(src.code(), indent + 2);

  return labels_string + tmp;
}

std::string c_expr2stringt::convert_code_switch_case(
  const code_switch_caset &src,
  unsigned indent)
{
  std::string labels_string;

  if (src.is_default())
  {
    labels_string += "\n";
    labels_string += indent_str(indent);
    labels_string += "default:\n";
  }
  else
  {
    labels_string += "\n";
    labels_string += indent_str(indent);
    labels_string += "case ";
    labels_string += convert(src.case_op());
    labels_string += ":\n";
  }

  unsigned next_indent = indent;
  if (
    src.code().get_statement() != "block" &&
    src.code().get_statement() != "switch_case")
    next_indent += 2;
  std::string tmp = convert_code(src.code(), next_indent);

  return labels_string + tmp;
}

std::string c_expr2stringt::convert_code(const codet &src)
{
  return convert_code(src, 0);
}

std::string c_expr2stringt::convert_Hoare(const exprt &src)
{
  unsigned precedence;

  if (src.operands().size() != 2)
    return convert_norep(src, precedence);

  const exprt &assumption = src.op0();
  const exprt &assertion = src.op1();
  const codet &code = static_cast<const codet &>(src.code());

  std::string dest = "\n";
  dest += "{";

  if (!assumption.is_nil())
  {
    std::string assumption_str = convert(assumption);
    dest += " assume(";
    dest += assumption_str;
    dest += ");\n";
  }
  else
    dest += "\n";

  {
    std::string code_str = convert_code(code);
    dest += code_str;
  }

  if (!assertion.is_nil())
  {
    std::string assertion_str = convert(assertion);
    dest += "    assert(";
    dest += assertion_str;
    dest += ");\n";
  }

  dest += "}";

  return dest;
}

std::string
c_expr2stringt::convert_extractbit(const exprt &src, unsigned precedence)
{
  if (src.operands().size() != 2)
    return convert_norep(src, precedence);

  std::string dest = convert(src.op0(), precedence);
  dest += '[';
  dest += convert(src.op1(), precedence);
  dest += ']';

  return dest;
}

std::string c_expr2stringt::convert_sizeof(const exprt &src, unsigned)
{
  std::string dest = "sizeof(";
  dest += convert(static_cast<const typet &>(src.c_sizeof_type()));
  dest += ')';

  return dest;
}

std::string c_expr2stringt::convert_extract(const exprt &src)
{
  std::string op = convert(src.op0());
  unsigned int upper = atoi(src.get("upper").as_string().c_str());
  unsigned int lower = atoi(src.get("lower").as_string().c_str());

  return "EXTRACT(" + op + "," + std::to_string(upper) + "," +
         std::to_string(lower) + ")";
}

/* Checks whether the expression `e` is one performing pointer-arithmetic, that
 * is, addition/subtraction of an integer-typed expression to/from a
 * pointer-typed expression.
 *
 * If so, `true` is returned and `ptr` holds the address of the inner-most
 * pointer-typed expression while `idx` gets assigned a (newly constructed, in
 * case of multiple levels of pointer-typed expressions) expression that
 * corresponds to the index into `*ptr`.
 *
 * Note, just a pointer-typed symbol (or constant) is not recognized as pointer-
 * arithmetic.
 */
static bool is_pointer_arithmetic(const exprt &e, const exprt *&ptr, exprt &idx)
{
  if (e.type().id() != "pointer")
    return false;

  ptr = &e;

  /* a pointer-typed arithmetic (+ or -) expression cannot be unary in the C
   * language */
  assert(!(e.id() == "unary+" || (e.id() == "+" && e.operands().size() == 1)));
  assert(!(e.id() == "unary-" || (e.id() == "-" && e.operands().size() == 1)));

  if (e.id() == "+" || e.id() == "-")
  {
    assert(e.operands().size() == 2);
    const exprt *p = nullptr, *i = nullptr;
    auto categorize = [&p, &i](const exprt &e) {
      const irep_idt &tid = e.type().id();
      if (tid == "pointer")
        p = &e;
      else if (tid == "signedbv" || tid == "unsignedbv")
        i = &e;
    };
    categorize(e.op0());
    categorize(e.op1());
    if (p && i)
    {
      if (e.id() == "-")
        assert(i == &e.op1());
      exprt j;
      if (is_pointer_arithmetic(*p, p, j))
      {
        auto is_unsigned = [](const exprt &e) {
          return e.type().id() == "unsignedbv";
        };
        const char *type =
          is_unsigned(j) || is_unsigned(*i) ? "unsignedbv" : "signedbv";
        idx = exprt(e.id(), typet(type));
        idx.copy_to_operands(j, *i);
      }
      else if (e.id() == "-")
      {
        idx = exprt("unary-", i->type());
        idx.copy_to_operands(*i);
      }
      else
        idx = *i;
      ptr = p;
      return true;
    }
  }

  return false;
}

std::string c_expr2stringt::convert(const exprt &src, unsigned &precedence)
{
  precedence = 16;

  if (src.id() == "+")
    return convert_binary(src, "+", precedence = 12, false);

  if (src.id() == "-")
  {
    if (src.operands().size() == 1)
      return convert_norep(src, precedence);

    return convert_binary(src, "-", precedence = 12, true);
  }

  else if (src.id() == "unary-")
  {
    if (src.operands().size() != 1)
      return convert_norep(src, precedence);

    return convert_unary(src, "-", precedence = 15);
  }

  else if (src.id() == "unary+")
  {
    if (src.operands().size() != 1)
      return convert_norep(src, precedence);

    return convert_unary(src, "+", precedence = 15);
  }

  else if (src.id() == "invalid-pointer")
  {
    return convert_function(src, "INVALID-POINTER", precedence = 15);
  }

  else if (src.id() == "invalid-object")
  {
    return "invalid-object";
  }

  else if (src.id() == "NULL-object")
  {
    return "0";
  }

  else if (src.id() == "infinity")
  {
    return convert_function(src, "INFINITY", precedence = 15);
  }

  else if (src.id() == "builtin-function")
  {
    return src.identifier().as_string();
  }

  else if (src.id() == "pointer_object")
  {
    return convert_function(src, "POINTER_OBJECT", precedence = 15);
  }

  else if (src.id() == "object_value")
  {
    return convert_function(src, "OBJECT_VALUE", precedence = 15);
  }

  else if (src.id() == "pointer_object_has_type")
  {
    return convert_pointer_object_has_type(src, precedence = 15);
  }

  else if (src.id() == "array_of")
  {
    return convert_array_of(src, precedence = 15);
  }

  else if (src.id() == "pointer_offset")
  {
    return convert_function(src, "POINTER_OFFSET", precedence = 15);
  }

  else if (src.id() == "pointer_base")
  {
    return convert_function(src, "POINTER_BASE", precedence = 15);
  }

  else if (src.id() == "pointer_cons")
  {
    return convert_function(src, "POINTER_CONS", precedence = 15);
  }

  else if (src.id() == "same-object")
  {
    return convert_function(src, "SAME-OBJECT", precedence = 15);
  }

  else if (src.id() == "valid_object")
  {
    return convert_function(src, "VALID_OBJECT", precedence = 15);
  }

  else if (src.id() == "deallocated_object" || src.id() == "memory-leak")
  {
    return convert_function(src, "DEALLOCATED_OBJECT", precedence = 15);
  }

  else if (src.id() == "dynamic_object")
  {
    return convert_function(src, "DYNAMIC_OBJECT", precedence = 15);
  }

  else if (src.id() == "is_dynamic_object")
  {
    return convert_function(src, "IS_DYNAMIC_OBJECT", precedence = 15);
  }

  else if (src.id() == "dynamic_size")
  {
    return convert_function(src, "DYNAMIC_SIZE", precedence = 15);
  }

  else if (src.id() == "dynamic_type")
  {
    return convert_function(src, "DYNAMIC_TYPE", precedence = 15);
  }

  else if (src.id() == "pointer_offset")
  {
    return convert_function(src, "POINTER_OFFSET", precedence = 15);
  }

  else if (src.id() == "isnan")
  {
    return convert_function(src, "isnan", precedence = 15);
  }

  else if (src.id() == "isfinite")
  {
    return convert_function(src, "isfinite", precedence = 15);
  }

  else if (src.id() == "isinf")
  {
    return convert_function(src, "isinf", precedence = 15);
  }

  else if (src.id() == "isnormal")
  {
    return convert_function(src, "isnormal", precedence = 15);
  }

  else if (src.id() == "signbit")
  {
    return convert_function(src, "signbit", precedence = 15);
  }

  else if (src.id() == "nearbyint")
  {
    return convert_function(src, "nearbyint", precedence = 15);
  }

  else if (src.id() == "popcount")
  {
    return convert_function(src, "popcount", precedence = 15);
  }

  else if (src.id() == "bswap")
  {
    return convert_function(src, "bswap", precedence = 15);
  }

  else if (src.id() == "__ESMBC_va_arg")
  {
    return convert_function(src, "__ESMBC_va_arg", precedence = 15);
  }

  else if (has_prefix(src.id_string(), "byte_extract"))
  {
    return convert_byte_extract(src, precedence = 15);
  }

  else if (has_prefix(src.id_string(), "byte_update"))
  {
    return convert_byte_update(src, precedence = 15);
  }

  else if (src.is_address_of())
  {
    if (src.operands().size() != 1)
      return convert_norep(src, precedence);
    if (src.op0().id() == "label")
      return "&&" + src.op0().get_string("identifier");
    else
      return convert_unary(src, "&", precedence = 15);
  }

  else if (src.id() == "dereference")
  {
    if (src.operands().size() != 1)
      return convert_norep(src, precedence);

    /* Special case for `*(p+i)` and `*(p-i)`: these constructs could either
     * already be in the source or it was created artificially by the frontend
     * for `p[i]`, see clang_c_adjust::adjust_index() and also Github issue
     * #725. As those expressions are semantically indistinguishable and also
     * supported by verifiers, choose to print the succint form `p[i]` in all
     * cases. */
    exprt idx;
    const exprt *ptr;
    if (is_pointer_arithmetic(src.op0(), ptr, idx))
    {
      exprt subst("index", src.type());
      subst.copy_to_operands(*ptr, idx);
      return convert_index(subst, precedence = 16);
    }

    return convert_unary(src, "*", precedence = 15);
  }

  else if (src.id() == "index")
    return convert_index(src, precedence = 16);

  else if (src.id() == "member")
    return convert_member(src, precedence = 16);

  else if (src.id() == "array-member-value")
    return convert_array_member_value(src, precedence = 16);

  else if (src.id() == "struct-member-value")
    return convert_struct_member_value(src, precedence = 16);

  else if (src.id() == "sideeffect")
  {
    const irep_idt &statement = src.statement();
    if (statement == "preincrement")
      return convert_unary(src, "++", precedence = 15);
    if (statement == "predecrement")
      return convert_unary(src, "--", precedence = 15);
    else if (statement == "postincrement")
      return convert_unary_post(src, "++", precedence = 16);
    else if (statement == "postdecrement")
      return convert_unary_post(src, "--", precedence = 16);
    else if (statement == "assign+")
      return convert_binary(src, "+=", precedence = 2, true);
    else if (statement == "assign-")
      return convert_binary(src, "-=", precedence = 2, true);
    else if (statement == "assign*")
      return convert_binary(src, "*=", precedence = 2, true);
    else if (statement == "assign_div")
      return convert_binary(src, "/=", precedence = 2, true);
    else if (statement == "assign_mod")
      return convert_binary(src, "%=", precedence = 2, true);
    else if (statement == "assign_shl")
      return convert_binary(src, "<<=", precedence = 2, true);
    else if (statement == "assign_ashr")
      return convert_binary(src, ">>=", precedence = 2, true);
    else if (statement == "assign_bitand")
      return convert_binary(src, "&=", precedence = 2, true);
    else if (statement == "assign_bitxor")
      return convert_binary(src, "^=", precedence = 2, true);
    else if (statement == "assign_bitor")
      return convert_binary(src, "|=", precedence = 2, true);
    else if (statement == "assign")
      return convert_binary(src, "=", precedence = 2, true);
    else if (statement == "function_call")
      return convert_function_call(src, precedence);
    else if (statement == "malloc")
      return convert_malloc(src, precedence = 15);
    else if (statement == "realloc")
      return convert_realloc(src, precedence = 15);
    else if (statement == "alloca")
      return convert_alloca(src, precedence = 15);
    else if (statement == "printf")
      return convert_function(src, "PRINTF", precedence = 15);
    else if (statement == "nondet")
      return convert_nondet(src, precedence = 15);
    else if (statement == "statement_expression")
      return convert_statement_expression(src, precedence = 15);
    else if (statement == "va_arg")
      return convert_function(src, "va_arg", precedence = 15);
    else
      return convert_norep(src, precedence);
  }

  else if (src.id() == "not")
    return convert_unary(src, "!", precedence = 15);

  else if (src.id() == "bitnot")
    return convert_unary(src, "~", precedence = 15);

  else if (src.id() == "*")
    return convert_binary(src, src.id_string(), precedence = 13, false);

  else if (src.id() == "/")
    return convert_binary(src, src.id_string(), precedence = 13, true);

  else if (src.id() == "mod")
    return convert_binary(src, "%", precedence = 13, true);

  else if (src.id() == "shl")
    return convert_binary(src, "<<", precedence = 11, true);

  else if (src.id() == "ashr" || src.id() == "lshr")
    return convert_binary(src, ">>", precedence = 11, true);

  else if (
    src.id() == "<" || src.id() == ">" || src.id() == "<=" || src.id() == ">=")
    return convert_binary(src, src.id_string(), precedence = 10, true);

  else if (src.id() == "notequal")
    return convert_binary(src, "!=", precedence = 9, true);

  else if (src.id() == "=")
    return convert_binary(src, "==", precedence = 9, true);

  else if (src.id() == "ieee_add")
    return convert_function(src, "IEEE_ADD", precedence = 15);

  else if (src.id() == "ieee_sub")
    return convert_function(src, "IEEE_SUB", precedence = 15);

  else if (src.id() == "ieee_mul")
    return convert_function(src, "IEEE_MUL", precedence = 15);

  else if (src.id() == "ieee_div")
    return convert_function(src, "IEEE_DIV", precedence = 15);

  else if (src.id() == "width")
    return convert_function(src, "WIDTH", precedence = 15);

  else if (src.id() == "byte_update_little_endian")
    return convert_function(src, "BYTE_UPDATE_LITTLE_ENDIAN", precedence = 15);

  else if (src.id() == "byte_update_big_endian")
    return convert_function(src, "BYTE_UPDATE_BIG_ENDIAN", precedence = 15);

  else if (src.id() == "abs")
    return convert_function(src, "abs", precedence = 15);

  else if (src.id() == "bitand")
    return convert_binary(src, "&", precedence = 8, false);

  else if (src.id() == "bitxor")
    return convert_binary(src, "^", precedence = 7, false);

  else if (src.id() == "bitor")
    return convert_binary(src, "|", precedence = 6, false);

  else if (src.is_and())
    return convert_binary(src, "&&", precedence = 5, false);

  else if (src.id() == "or")
    return convert_binary(src, "||", precedence = 4, false);

  else if (src.id() == "=>")
    return convert_binary(src, "=>", precedence = 3, true);

  else if (src.id() == "if")
    return convert_trinary(src, "?", ":", precedence = 3);

  else if (src.id() == "forall")
    return convert_quantifier(src, "FORALL", precedence = 2);

  else if (src.id() == "exists")
    return convert_quantifier(src, "EXISTS", precedence = 2);

  else if (src.id() == "with")
    return convert_with(src, precedence = 2);

  else if (src.id() == "symbol")
    return convert_symbol(src, precedence);

  else if (src.id() == "next_symbol")
    return convert_symbol(src, precedence);

  else if (src.id() == "nondet_symbol")
    return convert_nondet_symbol(src, precedence);

  else if (src.id() == "predicate_symbol")
    return convert_predicate_symbol(src, precedence);

  else if (src.id() == "predicate_next_symbol")
    return convert_predicate_next_symbol(src, precedence);

  else if (src.id() == "quantified_symbol")
    return convert_quantified_symbol(src, precedence);

  else if (src.id() == "nondet_bool")
    return convert_nondet_bool(src, precedence);

  else if (src.id() == "object_descriptor")
    return convert_object_descriptor(src, precedence);

  else if (src.id() == "Hoare")
    return convert_Hoare(src);

  else if (src.is_code())
    return convert_code(to_code(src));

  else if (src.id() == "constant")
    return convert_constant(src, precedence);

  else if (src.id() == "string-constant")
    return convert_constant(src, precedence);

  else if (src.id() == "struct")
    return convert_struct(src, precedence);

  else if (src.id() == "union")
    return convert_union(src, precedence);

  else if (src.is_array())
    return convert_array(src, precedence);

  else if (src.id() == "array-list")
    return convert_array_list(src, precedence);

  else if (src.id() == "typecast")
    return convert_typecast(src, precedence);

  else if (src.id() == "bitcast")
    return convert_bitcast(src, precedence);

  else if (src.id() == "implicit_address_of")
    return convert_implicit_address_of(src, precedence);

  else if (src.id() == "implicit_dereference")
    return convert_function(src, "IMPLICIT_DEREFERENCE", precedence = 15);

  else if (src.id() == "comma")
    return convert_binary(src, ", ", precedence = 1, false);

  else if (src.id() == "cond")
    return convert_cond(src, precedence);

  else if (std::string(src.id_string(), 0, 9) == "overflow-")
    return convert_overflow(src, precedence);

  else if (src.id() == "unknown")
    return "*";

  else if (src.id() == "invalid")
    return "#";

  else if (src.id() == "extractbit")
    return convert_extractbit(src, precedence);

  else if (src.id() == "sizeof")
    return convert_sizeof(src, precedence);

  else if (src.id() == "concat")
    return convert_function(src, "CONCAT", precedence = 15);

  else if (src.id() == "extract")
    return convert_extract(src);

  // no C language expression for internal representation
  return convert_norep(src, precedence);
}

std::string c_expr2stringt::convert(const exprt &src)
{
  unsigned precedence;
  return convert(src, precedence);
}

std::string
c_expr2string(const exprt &expr, const namespacet &ns, unsigned flags)
{
  std::string code;
  c_expr2stringt c_expr2string(ns, flags);
  c_expr2string.get_shorthands(expr);
  return c_expr2string.convert(expr);
}

std::string
c_type2string(const typet &type, const namespacet &ns, unsigned flags)
{
  c_expr2stringt c_expr2string(ns, flags);
  return c_expr2string.convert(type);
}