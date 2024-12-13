#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/std_types.h>
#include <jimple-frontend/AST/jimple_statement.h>
#include <jimple-frontend/AST/jimple_globals.h>
#include <util/arith_tools.h>
#include "util/c_typecast.h"

void jimple_identity::from_json(const json &j)
{
  j.at("identifier").get_to(at_identifier);
  j.at("name").get_to(local_name);
  j.at("type").get_to(type);
}

exprt jimple_identity::to_exprt(
  contextt &ctx,
  const std::string &,
  const std::string &) const
{
  // TODO: Symbol-table / Typecast
  exprt val("at_identifier");
  symbolt &added_symbol = *ctx.find_symbol(local_name);
  symbolt rhs;
  rhs.name = "@" + at_identifier;
  rhs.id = "@" + at_identifier;
  code_assignt assign(symbol_expr(added_symbol), symbol_expr(rhs));
  return assign;
}
std::string jimple_identity::to_string() const
{
  std::ostringstream oss;
  oss << "Identity:  " << this->local_name << " = @" << at_identifier << " | "
      << type.to_string();
  return oss.str();
}

exprt jimple_return::to_exprt(
  contextt &ctx,
  const std::string &class_name,
  const std::string &function_name) const
{
  // TODO: jimple return with support to other returns
  typet return_type = empty_typet();
  code_returnt ret_expr;
  if (expr)
  {
    auto return_value = expr->to_exprt(ctx, class_name, function_name);
    ret_expr.op0() = return_value;
  }
  // TODO: jimple return should support values
  return ret_expr;
}

std::string jimple_return::to_string() const
{
  return "Return: (Nothing)";
}
void jimple_return::from_json(const json &j)
{
  if (j.contains("value"))
    expr = jimple_expr::get_expression(j.at("value"));
}
std::string jimple_label::to_string() const
{
  std::ostringstream oss;
  oss << "Label: " << this->label;
  for (auto member : this->members->members)
    oss << "\n\t\t\t" << member->to_string();
  return oss.str();
}

exprt jimple_label::to_exprt(
  contextt &ctx,
  const std::string &class_name,
  const std::string &function_name) const
{
  // TODO: DRY (clang-c-converter)
  code_labelt c_label;
  c_label.set_label(label);

  code_blockt block;
  for (auto member : members->members)
  {
    block.operands().push_back(
      std::move(member->to_exprt(ctx, class_name, function_name)));
  }
  c_label.code() = to_code(block);

  return c_label;
}

void jimple_goto::from_json(const json &j)
{
  j.at("goto").get_to(label);
}

std::string jimple_goto::to_string() const
{
  std::ostringstream oss;
  oss << "Goto: " << this->label;
  return oss.str();
}

exprt jimple_goto::to_exprt(
  contextt &,
  const std::string &,
  const std::string &) const
{
  code_gotot code_goto;
  code_goto.set_destination(label);
  return code_goto;
}

void jimple_label::from_json(const json &j)
{
  j.at("label_id").get_to(label);
  jimple_full_method_body b;
  b.from_json(j.at("content"));
  members = std::make_shared<jimple_full_method_body>(b);
}

std::string jimple_assignment::to_string() const
{
  std::ostringstream oss;
  oss << "Assignment: " << lhs->to_string() << " = " << rhs->to_string();
  return oss.str();
}

void jimple_assignment::from_json(const json &j)
{
  lhs = jimple_expr::get_expression(j.at("lhs"));
  rhs = jimple_expr::get_expression(j.at("rhs"));
}

exprt jimple_assignment::to_exprt(
  contextt &ctx,
  const std::string &class_name,
  const std::string &function_name) const
{
  //TODO: Remove this hack
  if (is_skip)
  {
    code_skipt skip;
    return skip;
  }

  auto lhs_handle = lhs->to_exprt(ctx, class_name, function_name);

  auto dyn_expr = std::dynamic_pointer_cast<jimple_expr_invoke>(rhs);
  if (dyn_expr && !dyn_expr->is_nondet_call() && !dyn_expr->is_intrinsic_method)
  {
    dyn_expr->set_lhs(lhs_handle);
    return rhs->to_exprt(ctx, class_name, function_name);
  }

  auto dyn2_expr = std::dynamic_pointer_cast<jimple_virtual_invoke>(rhs);
  if (dyn2_expr && !dyn2_expr->is_nondet_call())
  {
    dyn2_expr->set_lhs(lhs_handle);
    return rhs->to_exprt(ctx, class_name, function_name);
  }

  auto from_expr = rhs->to_exprt(ctx, class_name, function_name);
  c_typecastt c_typecast(ctx);
  c_typecast.implicit_typecast(from_expr, lhs_handle.type());

  code_assignt assign(lhs_handle, from_expr);
  return assign;
}

/*
std::string jimple_assignment_deref::to_string() const
{
  std::ostringstream oss;
  oss << "Assignment: " << variable << "[" << pos->to_string()
      << "]  = " << expr->to_string();
  return oss.str();
}

void jimple_assignment_deref::from_json(const json &j)
{
  j.at("name").get_to(variable);
  expr = jimple_expr::get_expression(j.at("value"));
  pos = jimple_expr::get_expression(j.at("pos"));
}

exprt jimple_assignment_deref::to_exprt(
  contextt &ctx,
  const std::string &class_name,
  const std::string &function_name) const
{
  jimple_symbol s(variable);

  jimple_deref d(pos, std::make_shared<jimple_symbol>(s));

  code_assignt assign(
    d.to_exprt(ctx, class_name, function_name),
    expr->to_exprt(ctx, class_name, function_name));
  return assign;
}

std::string jimple_assignment_field::to_string() const
{
  std::ostringstream oss;
  oss << "Assignment: " << variable << "->" << field << " = " << expr->to_string();
  return oss.str();
}

void jimple_assignment_field::from_json(const json &j)
{
  j.at("name").get_to(variable);
  j.at("field").get_to(field);
  expr = jimple_expr::get_expression(j.at("value"));
}

exprt jimple_assignment_field::to_exprt(
  contextt &ctx,
  const std::string &class_name,
  const std::string &function_name) const
{
    // 1. Look over the local scope
  auto symbol_name = get_symbol_name(class_name, function_name, variable);
  symbolt &s = *ctx.find_symbol(symbol_name);
  member_exprt op(symbol_expr(s), "tag-" + field, s.type);
  exprt &base = op.struct_op();
  if(base.type().is_pointer())
  {
    exprt deref("dereference");
    deref.type() = base.type().subtype();
    deref.move_to_operands(base);
    base.swap(deref);
  }

  code_assignt assign(
    op,
    expr->to_exprt(ctx, class_name, function_name));
  return assign;
}
*/

std::string jimple_if::to_string() const
{
  std::ostringstream oss;
  oss << "If: " << cond->to_string() << " THEN GOTO " << label;
  return oss.str();
}

void jimple_if::from_json(const json &j)
{
  cond = jimple_expr::get_expression(j.at("expression"));
  j.at("goto").get_to(label);
}

exprt jimple_if::to_exprt(
  contextt &ctx,
  const std::string &class_name,
  const std::string &function_name) const
{
  code_gotot code_goto;
  code_goto.set_destination(label);

  auto condition = cond->to_exprt(ctx, class_name, function_name);
  codet if_expr("ifthenelse");
  if_expr.copy_to_operands(condition, code_goto);

  return if_expr;
}

std::string jimple_assertion::to_string() const
{
  std::ostringstream oss;
  oss << "Assertion: " << variable << " = " << value;
  return oss.str();
}

void jimple_assertion::from_json(const json &j)
{
  j.at("equals").at("symbol").get_to(variable);
  j.at("equals").at("value").get_to(value);
}

exprt jimple_assertion::to_exprt(
  contextt &ctx,
  const std::string &class_name,
  const std::string &function_name) const
{
  code_function_callt call;

  std::ostringstream oss;
  oss << class_name << ":" << function_name << "@" << variable;

  // TODO: move this from here
  std::string id, name;
  id = "__ESBMC_assert";
  name = "__ESBMC_assert";

  auto symbol =
    create_jimple_symbolt(code_typet(), class_name, name, id, function_name);

  symbolt &added_symbol = *ctx.move_symbol_to_context(symbol);

  call.function() = symbol_expr(added_symbol);

  symbolt &test = *ctx.find_symbol(oss.str());
  int as_number = std::stoi(value);
  exprt value_operand = from_integer(as_number, int_type());

  equality_exprt ge(symbol_expr(test), value_operand);
  not_exprt qwe(ge);
  call.arguments().push_back(qwe);

  array_of_exprt arr;
  // TODO: Create binop operation between symbol and value
  return call;
}

std::string jimple_invoke::to_string() const
{
  std::ostringstream oss;
  oss << "Invoke: " << method;
  return oss.str();
}

void jimple_invoke::from_json(const json &j)
{
  j.at("base_class").get_to(base_class);
  j.at("method").get_to(method);
  if (j.contains("variable"))
    j.at("variable").get_to(variable);
  for (auto x : j.at("parameters"))
  {
    parameters.push_back(std::move(jimple_expr::get_expression(x)));
  }
  method += "_" + get_hash_name();
}

exprt jimple_invoke::to_exprt(
  contextt &ctx,
  const std::string &class_name,
  const std::string &function_name) const
{
  // TODO: Move intrinsics to backend
  if (base_class == "kotlin.jvm.internal.Intrinsics")
  {
    code_skipt skip;
    return skip;
  }

  // TODO: Move intrinsics to backend
  if (base_class == "java.lang.Runtime")
  {
    code_skipt skip;
    return skip;
  }

  // Don't care for the default object constructor
  if (base_class == "java.lang.Object")
  {
    code_skipt skip;
    return skip;
  }

  // Don't care for Random
  if (base_class == "java.util.Random")
  {
    code_skipt skip;
    return skip;
  }

  // Don't care for Random
  if (base_class == "java.lang.String")
  {
    code_skipt skip;
    return skip;
  }

  if (base_class == "java.lang.AssertionError")
  {
    code_skipt skip;
    return skip;
  }

  if (base_class == "java.lang.Exception")
  {
    code_skipt skip;
    return skip;
  }

  if (base_class == "java.security.InvalidParameterException")
  {
    code_skipt skip;
    return skip;
  }

  if (base_class == "android.widget.Button")
  {
    code_skipt skip;
    return skip;
  }

  if (base_class == "android.content.Intent" && method == "<init>_3")
  {
    // TODO: Fix this!
    jimple_symbol s(variable);
    code_assignt asd(
      s.to_exprt(ctx, class_name, function_name),
      parameters[1]->to_exprt(ctx, base_class, function_name));
    //code_expressiont asd(parameters[1]->to_exprt(ctx, base_class, function_name));

    return asd;
  }

  if (method == "startActivity_2")
  {
    code_function_callt call;

    std::ostringstream oss;
    oss << class_name << ":" << function_name << "@" << variable;

    auto target = config.options.get_option("target");
    int i = jimple::get_reference(target);
    auto class_obj = parameters[0]->to_exprt(ctx, class_name, function_name);
    //abort();

    // TODO: move this from here
    std::string id, name;
    id = "__ESBMC_assert";
    name = "__ESBMC_assert";

    auto symbol =
      create_jimple_symbolt(code_typet(), class_name, name, id, function_name);

    symbolt &added_symbol = *ctx.move_symbol_to_context(symbol);

    call.function() = symbol_expr(added_symbol);
    exprt value_operand = from_integer(i, int_type());

    equality_exprt check(class_obj, value_operand);
    call.arguments().push_back(gen_not(check));
    return call;
  }

  if (base_class == "androidx.appcompat.app.AppCompatActivity")
  {
    code_skipt skip;
    return skip;
  }

  if (
    base_class ==
    "com.example.jimplebmc.MainActivity$$ExternalSyntheticLambda0")
  {
    code_skipt skip;
    return skip;
  }

  if (base_class.find("$$ExternalSyntheticLambda") != std::string::npos)
  {
    code_skipt skip;
    return skip;
  }

  if (method == "getLayoutInflater_1")
  {
    code_skipt skip;
    return skip;
  }

  if (method == "setContentView_2")
  {
    code_skipt skip;
    return skip;
  }

  if (method == "findViewById_2")
  {
    code_skipt skip;
    return skip;
  }

  if (method == "setSupportActionBar_2")
  {
    code_skipt skip;
    return skip;
  }

  code_blockt block;
  code_function_callt call;

  std::ostringstream oss;
  oss << base_class << ":" << method;
  auto symbol = ctx.find_symbol(oss.str());

  if (!symbol)
  {
    log_error("Could not find Method {} from Class {}", method, base_class);
    abort();
  }
  call.function() = symbol_expr(*symbol);

  if (variable != "")
  {
    // Let's add @THIS
    auto this_expression =
      jimple_symbol(variable).to_exprt(ctx, class_name, function_name);
    call.arguments().push_back(this_expression);
    auto temp = get_symbol_name(base_class, method, "@this");
    symbolt &added_symbol = *ctx.find_symbol(temp);
    code_assignt assign(symbol_expr(added_symbol), this_expression);
    block.operands().push_back(assign);
  }

  for (unsigned long int i = 0; i < parameters.size(); i++)
  {
    // Just adding the arguments should be enough to set the parameters
    auto parameter_expr =
      parameters[i]->to_exprt(ctx, class_name, function_name);
    call.arguments().push_back(parameter_expr);
    // Hack, manually adding parameters
    std::ostringstream oss;
    oss << "@parameter" << i;
    auto temp = get_symbol_name(base_class, method, oss.str());
    symbolt &added_symbol = *ctx.find_symbol(temp);
    code_assignt assign(symbol_expr(added_symbol), parameter_expr);
    block.operands().push_back(assign);
  }
  block.operands().push_back(call);
  return block;
}

std::string jimple_throw::to_string() const
{
  std::ostringstream oss;
  oss << "Throw: " << expr->to_string();
  return oss.str();
}

void jimple_throw::from_json(const json &j)
{
  expr = jimple_expr::get_expression(j.at("expr"));
}

exprt jimple_throw::to_exprt(
  contextt &,
  const std::string &,
  const std::string &) const
{
  codet p = codet("cpp-throw");
  // TODO: throw
  // Since the implementation of Throw isn't complete,
  // the expression shouldn't be used.

  // auto to_add = expr->to_exprt(ctx, class_name, function_name);
  // p.move_to_operands(to_add);
  return p;
}
