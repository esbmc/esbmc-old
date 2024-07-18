#include <util/location.h>
#include <util/message.h>
#include <util/symbol.h>

symbolt::symbolt()
{
  clear();
}

void symbolt::clear()
{
  value.make_nil();
  location.make_nil();
  lvalue = static_lifetime = file_local = is_extern = is_type = is_parameter =
    is_macro = false;
  id = module = name = mode = "";
}

void symbolt::swap(symbolt &b)
{
#define SYM_SWAP1(x) x.swap(b.x)

  SYM_SWAP1(type);
  SYM_SWAP1(value);
  SYM_SWAP1(id);
  SYM_SWAP1(module);
  SYM_SWAP1(name);
  SYM_SWAP1(mode);
  SYM_SWAP1(location);

#define SYM_SWAP2(x) std::swap(x, b.x)

  SYM_SWAP2(is_type);
  SYM_SWAP2(is_macro);
  SYM_SWAP2(is_parameter);
  SYM_SWAP2(lvalue);
  SYM_SWAP2(static_lifetime);
  SYM_SWAP2(file_local);
  SYM_SWAP2(is_extern);
}

void symbolt::dump() const
{
  std::ostringstream oss;
  show(oss);
  log_status("{}", oss.str());
}

void symbolt::show(std::ostream &out) const
{
  out << "Symbol......: " << id << "\n";
  out << "Base name...: " << name << "\n";
  out << "Module......: " << module << "\n";
  out << "Mode........: " << mode << " (" << mode << ")"
      << "\n";
  if (type.is_not_nil())
    out << "Type........: " << type.pretty(4) << "\n";
  if (value.is_not_nil())
    out << "Value.......: " << value.pretty(4) << "\n";

  out << "Flags.......:";

  if (lvalue)
    out << " lvalue";
  if (static_lifetime)
    out << " static_lifetime";
  if (file_local)
    out << " file_local";
  if (is_type)
    out << " type";
  if (is_extern)
    out << " extern";
  if (is_macro)
    out << " macro";

  out << "\n";
  out << "Location....: " << location << "\n";

  out << "\n";
}

std::ostream &operator<<(std::ostream &out, const symbolt &symbol)
{
  symbol.show(out);
  return out;
}

void symbolt::to_irep(irept &dest) const
{
  dest.clear();
  dest.type() = type;
  dest.symvalue(value);
  dest.location(location);
  dest.name(id);
  dest.module(module);
  dest.base_name(name);
  dest.mode(mode);

  if (is_type)
    dest.is_type(true);
  if (is_macro)
    dest.is_macro(true);
  if (is_parameter)
    dest.is_parameter(true);
  if (lvalue)
    dest.lvalue(true);
  if (static_lifetime)
    dest.static_lifetime(true);
  if (file_local)
    dest.file_local(true);
  if (is_extern)
    dest.is_extern(true);
}

void symbolt::from_irep(const irept &src)
{
  type = src.type();
  value = static_cast<const exprt &>(src.symvalue());
  location = static_cast<const locationt &>(src.location());

  id = src.name();
  module = src.module();
  name = src.base_name();
  mode = src.mode();

  is_type = src.is_type();
  is_macro = src.is_macro();
  is_parameter = src.is_parameter();
  lvalue = src.lvalue();
  static_lifetime = src.static_lifetime();
  file_local = src.file_local();
  is_extern = src.is_extern();
}

irep_idt symbolt::get_function_name() const
{
  irep_idt func_name = location.get_function();
  if (!func_name.empty())
    return func_name;

  const std::string &symbol_id = id.as_string();

  // Find the position of "F@"
  size_t posF = symbol_id.find("F@");

  if (posF == std::string::npos)
    return ""; // Return an empty string if "F@" is not found

  posF += 2; // Advance beyond "F@"

  // Find the position of the last '@'
  size_t posLastAt = symbol_id.rfind('@');

  // Check if there is an '@' after the function name (e.g.: c:string.c@1290@F@strcmp@c1)
  if (posLastAt != std::string::npos && posLastAt > posF)
    return symbol_id.substr(
      posF,
      posLastAt - posF); // Extract the content between "F@" and the last '@'

  return symbol_id.substr(
    posF); // If there is no '@' after the function name (e.g: c:string.c@1290@F@strcmp), return from "F@"
}
