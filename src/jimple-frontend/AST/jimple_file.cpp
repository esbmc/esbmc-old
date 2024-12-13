#include <map>
#include <fstream>
#include <jimple-frontend/AST/jimple_file.h>

#include <util/std_code.h>
#include <util/expr_util.h>

// (De)-Serialization helpers (from jimple_ast.h)
void to_json(json &, const jimple_ast &)
{
  // Don't care
}

void from_json(const json &j, jimple_ast &p)
{
  p.from_json(j);
}

std::string jimple_file::to_string() const
{
  std::ostringstream oss;
  oss << "Jimple File:\n"
      << "\t"
      << "Name: " << this->class_name << "\n\t"
      << "Mode: " << to_string(this->mode) << "\n\t"
      << "Extends: " << this->extends << "\n\t"
      << "Implements: " << this->implements << "\n\t"
      << this->modifiers.to_string();

  oss << "\n\n";
  for (auto &x : body)
  {
    oss << x->to_string();
    oss << "\n\n";
  }

  return oss.str();
}

void jimple_file::from_json(const json &j)
{
  // Get ClassName
  j.at("name").get_to(this->class_name);

  std::string t;
  j.at("object").get_to(t);
  this->mode = from_string(t);

  if (j.contains("implements"))
    j.at("implements").get_to(this->implements);
  else
    this->implements = "(No implements)";

  if (j.contains("extends"))
    j.at("extends").get_to(this->extends);
  else
    this->implements = "(No extends)";

  modifiers = j.at("modifiers").get<jimple_modifiers>();

  auto filebody = j.at("content");
  for (auto &x : filebody)
  {
    // TODO: Here is where to add support for signatures
    auto content_type = x.at("object").get<std::string>();
    std::shared_ptr<jimple_class_member> to_add;
    if (content_type == "Method")
    {
      jimple_method m;
      x.get_to(m);
      to_add = std::make_shared<jimple_method>(m);
    }
    else if (content_type == "Field")
    {
      jimple_class_field m;
      x.get_to(m);
      to_add = std::make_shared<jimple_class_field>(m);
    }
    else
    {
      log_error("Unsupported object: {}", content_type);
      abort();
    }
    body.push_back(to_add);
  }
}

inline jimple_file::file_type
jimple_file::from_string(const std::string &name) const
{
  return from_map.at(name);
}

inline std::string
jimple_file::to_string(const jimple_file::file_type &ft) const
{
  return to_map.at(ft);
}
void jimple_file::load_file(const std::string &path)
{
  std::ifstream i(path);
  json j;
  i >> j;

  from_json(j);
}

exprt jimple_file::to_exprt(contextt &ctx) const
{
  /*
   * A class is just a type, this method will just register
   * its type and return a code_skipt.
   *
   * However, the list of symbols are going to be updated with
   * the static functions and variables, and constructor */

  exprt e = code_skipt();

  std::string id, name;
  id = "tag-" + this->class_name;
  name = this->class_name;

  // Check if class already exists
  if (ctx.find_symbol(id) != nullptr)
    throw "Duplicated class name";

  struct_typet t;
  t.tag(name);

  auto symbol = create_jimple_symbolt(t, name, name, id);
  std::string symbol_name = symbol.id.as_string();

  // A class/interface is a type
  symbol.is_type = true;

  // Add symbol into the context
  ctx.move_symbol_to_context(symbol);
  symbolt *added_symbol = ctx.find_symbol(symbol_name);

  // Add class/interface members
  auto total_size = 0;
  for (auto const &field : body)
  {
    if (std::dynamic_pointer_cast<jimple_class_field>(field))
    {
      struct_typet::componentt comp;
      exprt &tmp = comp;
      tmp = field->to_exprt(ctx, name, name);
      comp.swap(tmp);
      t.components().push_back(comp);
      total_size += std::stoi(comp.type().width().as_string());
    }
  }

  // Here is where we add the inherited fields

  // Finally, the structure is ready. Lets add it
  t.set("width", total_size);
  added_symbol->type = t;

  // TODO: This is the most horrible hack that i've ever done
  // Add the methods and definitions
  for (auto const &field : body)
  {
    if (!std::dynamic_pointer_cast<jimple_class_field>(field))
    {
      auto func = std::dynamic_pointer_cast<jimple_method>(field);
      if (func->name.find("onCreate") != std::string::npos)
        field->to_exprt(ctx, name, name);
    }
  }

  // Add the methods and definitions
  for (auto const &field : body)
  {
    if (!std::dynamic_pointer_cast<jimple_class_field>(field))
    {
      auto func = std::dynamic_pointer_cast<jimple_method>(field);
      if (func->name.find("onCreate") == std::string::npos)
        field->to_exprt(ctx, name, name);
    }
  }
  return e;
}
