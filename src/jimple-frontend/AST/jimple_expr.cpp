#include <jimple-frontend/AST/jimple_expr.h>
#include <jimple-frontend/AST/jimple_globals.h>
#include <util/arith_tools.h>
#include <util/c_typecast.h>
#include <util/c_types.h>
#include <util/expr_util.h>
#include <util/std_code.h>
#include <util/std_expr.h>

void jimple_constant::from_json(const json &j)
{
  j.at("value").get_to(value);
}

exprt jimple_constant::to_exprt(
  contextt &,
  const std::string &,
  const std::string &) const
{
  auto as_number = std::stoi(value);
  return constant_exprt(
    integer2binary(as_number, 10), integer2string(as_number), int_type());
};

void jimple_symbol::from_json(const json &j)
{
  j.at("value").get_to(var_name);
}

exprt jimple_symbol::to_exprt(
  contextt &ctx,
  const std::string &class_name,
  const std::string &function_name) const
{
  // 1. Look over the local scope
  if (var_name == "null")
  {
    return gen_zero(int_type());
  }
  auto symbol_name = get_symbol_name(class_name, function_name, var_name);
  symbolt &s = *ctx.find_symbol(symbol_name);

  // TODO:
  // 2. Look over the class scope
  // 3. Look over the global scope (possibly don't need)

  return symbol_expr(s);
};

std::shared_ptr<jimple_expr> jimple_expr::get_expression(const json &j)
{
  std::string expr_type;
  if (!j.contains("expr_type"))
  {
    jimple_constant c;
    c.setValue("0");
    return std::make_shared<jimple_constant>(c);
  }

  j.at("expr_type").get_to(expr_type);

  // TODO: hashmap, the standard is not stable enough yet
  // It is still a work in progress in the parser: https://github.com/rafaelsamenezes/jimple_parser
  if (expr_type == "constant")
  {
    jimple_constant c;
    c.from_json(j);
    return std::make_shared<jimple_constant>(c);
  }

  if (expr_type == "string_constant")
  {
    jimple_constant c;
    return std::make_shared<jimple_constant>(c);
  }

  if (expr_type == "class_reference")
  {
    std::string key;
    j.at("value").get_to(key);
    std::ostringstream oss;
    // TODO: this is not ok
    oss << jimple::get_reference(key);
    jimple_constant c(oss.str());
    jimple::class_reference.push_back(key);
    return std::make_shared<jimple_constant>(c);
  }

  if (expr_type == "symbol")
  {
    jimple_symbol c;
    c.from_json(j);
    return std::make_shared<jimple_symbol>(c);
  }

  if (expr_type == "static_invoke")
  {
    jimple_expr_invoke c;
    c.from_json(j);
    return std::make_shared<jimple_expr_invoke>(c);
  }

  if (expr_type == "virtual_invoke")
  {
    jimple_virtual_invoke c;
    c.from_json(j);
    return std::make_shared<jimple_virtual_invoke>(c);
  }

  if (expr_type == "binop")
  {
    jimple_binop c;
    c.from_json(j);
    return std::make_shared<jimple_binop>(c);
  }

  if (expr_type == "cast")
  {
    jimple_cast c;
    c.from_json(j);
    return std::make_shared<jimple_cast>(c);
  }

  if (expr_type == "lengthof")
  {
    jimple_lengthof c;
    c.from_json(j);
    return std::make_shared<jimple_lengthof>(c);
  }

  if (expr_type == "newarray")
  {
    jimple_newarray c;
    c.from_json(j);
    return std::make_shared<jimple_newarray>(c);
  }

  if (expr_type == "new")
  {
    jimple_new c;
    c.from_json(j);
    return std::make_shared<jimple_new>(c);
  }

  if (expr_type == "array_index")
  {
    jimple_deref c;
    c.from_json(j);
    return std::make_shared<jimple_deref>(c);
  }

  if (expr_type == "nondet")
  {
    jimple_nondet c;
    return std::make_shared<jimple_nondet>(c);
  }

  if (expr_type == "static_member")
  {
    jimple_static_member c;
    c.from_json(j.at("signature"));
    return std::make_shared<jimple_static_member>(c);
  }

  if (expr_type == "local_member")
  {
    jimple_virtual_member c;
    c.from_json(j);
    return std::make_shared<jimple_virtual_member>(c);
  }

  abort();
}

void jimple_binop::from_json(const json &j)
{
  j.at("operator").get_to(binop);
  // TODO, make hashmap for each operator
  if (binop == "==")
    binop = "=";
  lhs = get_expression(j.at("lhs"));
  rhs = get_expression(j.at("rhs"));
}

exprt jimple_binop::to_exprt(
  contextt &ctx,
  const std::string &class_name,
  const std::string &function_name) const
{
  auto lhs_expr = lhs->to_exprt(ctx, class_name, function_name);
  return gen_binary(
    binop,
    lhs_expr.type(),
    lhs_expr,
    rhs->to_exprt(ctx, class_name, function_name));
};

void jimple_cast::from_json(const json &j)
{
  jimple_type type;
  j.at("to").get_to(type);
  to = std::make_shared<jimple_type>(type);
  from = get_expression(j.at("from"));
}

exprt jimple_cast::to_exprt(
  contextt &ctx,
  const std::string &class_name,
  const std::string &function_name) const
{
  auto from_expr = from->to_exprt(ctx, class_name, function_name);
  c_typecastt c_typecast(ctx);

  c_typecast.implicit_typecast(from_expr, to->to_typet(ctx));
  return from_expr;
};

void jimple_lengthof::from_json(const json &j)
{
  from = get_expression(j.at("expression"));
}

exprt jimple_lengthof::to_exprt(
  contextt &ctx,
  const std::string &class_name,
  const std::string &function_name) const
{
  auto expr = from->to_exprt(ctx, class_name, function_name);
  auto tmp_symbol = get_temp_symbol(uint_type(), class_name, function_name);
  symbolt &tmp_added_symbol = *ctx.move_symbol_to_context(tmp_symbol);

  // Create a function call for allocation
  code_function_callt call;
  auto alloca_symbol = get_lengthof_function();

  symbolt &added_symbol = *ctx.move_symbol_to_context(alloca_symbol);

  call.function() = symbol_expr(added_symbol);

  call.arguments().push_back(expr);

  // Create a sideffect call to represent the allocation
  side_effect_expr_function_callt sideeffect;
  sideeffect.function() = call.function();
  sideeffect.arguments() = call.arguments();
  sideeffect.location() = call.location();
  sideeffect.type() =
    static_cast<const typet &>(call.function().type().return_type());
  return sideeffect;
};

void jimple_newarray::from_json(const json &j)
{
  size = get_expression(j.at("size"));
  jimple_type t;
  j.at("type").get_to(t);
  type = std::make_shared<jimple_type>(t);
}

void jimple_new::from_json(const json &j)
{
  size = std::make_shared<jimple_constant>("1");
  jimple_type t;
  j.at("type").get_to(t);
  type = std::make_shared<jimple_type>(t);
}

void jimple_expr_invoke::from_json(const json &j)
{
  lhs = nil_exprt();
  j.at("base_class").get_to(base_class);
  j.at("method").get_to(method);
  for (auto x : j.at("parameters"))
  {
    parameters.push_back(std::move(jimple_expr::get_expression(x)));
  }
  method += "_" + get_hash_name();

  // TODO: Move intrinsics to backend
  if (base_class == "java.lang.Integer" && method == "valueOf_1")
  {
    log_debug("jimple", "Got an intrinsic call to valueOf int");
    is_intrinsic_method = true;
  }
}

exprt jimple_expr_invoke::to_exprt(
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

  // TODO: Move intrinsics to backend
  if (base_class == "android.content.Intent")
  {
    code_skipt skip;
    return skip;
  }

  if (method == "inflate_1")
  {
    code_skipt skip;
    return skip;
  }

  if (method == "getRoot_1")
  {
    code_skipt skip;
    return skip;
  }

  bool is_parse_int =
    (base_class == "java.lang.Integer") && (method == "parseInt_1");
  if (is_parse_int)
  {
    code_blockt block;
    exprt rhs = exprt("sideeffect", int_type());
    rhs.statement("nondet");
    code_assignt assign(lhs, rhs);
    block.operands().push_back(assign);
    return block;
  }

  if (method == "isChecked_1")
  {
    code_blockt block;
    exprt rhs = exprt("sideeffect", uint_type());
    rhs.statement("nondet");
    block.operands().push_back(rhs);
    return block;
  }

  if (is_nondet_call())
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
    log_error("Could not find symbol {}", oss.str());
    abort();
  }
  call.function() = symbol_expr(*symbol);
  if (!lhs.is_nil())
    call.lhs() = lhs;

  for (long unsigned int i = 0; i < parameters.size(); i++)
  {
    // Just adding the arguments should be enough to set the parameters
    auto parameter_expr =
      parameters[i]->to_exprt(ctx, class_name, function_name);
    call.arguments().push_back(parameter_expr);
    // Hack, manually adding parameters, this should be done at symex
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

void jimple_virtual_invoke::from_json(const json &j)
{
  lhs = nil_exprt();
  j.at("base_class").get_to(base_class);
  j.at("method").get_to(method);
  j.at("name").get_to(variable);
  for (auto x : j.at("parameters"))
  {
    parameters.push_back(std::move(jimple_expr::get_expression(x)));
  }
  method += "_" + get_hash_name();
}

exprt jimple_virtual_invoke::to_exprt(
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

  // TODO: Move intrinsics to backend
  if (base_class == "java.lang.Class")
  {
    code_skipt skip;
    return skip;
  }

  // TODO: Move intrinsics to backend
  if (base_class == "android.widget.EditText")
  {
    code_skipt skip;
    return skip;
  }

  if (base_class == "java.lang.Object")
  {
    code_skipt skip;
    return skip;
  }

  if (method == "getLayoutInflater_1")
  {
    code_skipt skip;
    return skip;
  }

  if (method == "inflate_1")
  {
    code_skipt skip;
    return skip;
  }

  if (method == "getRoot_1")
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

  if (method == "isChecked_1")
  {
    code_blockt block;
    exprt rhs = exprt("sideeffect", int_type());
    rhs.statement("nondet");
    code_assignt assign(lhs, rhs);
    block.operands().push_back(assign);
    return block;
  }

  if (is_nondet_call())
  {
    jimple_nondet nondet(method);
    return nondet.to_exprt(ctx, class_name, function_name);
  }

  code_blockt block;
  code_function_callt call;

  std::ostringstream oss;
  oss << base_class << ":" << method;

  auto symbol = ctx.find_symbol(oss.str());
  call.function() = symbol_expr(*symbol);
  if (!lhs.is_nil())
  {
    call.lhs() = lhs;
  }

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

  for (long unsigned int i = 0; i < parameters.size(); i++)
  {
    // Just adding the arguments should be enough to set the parameters
    auto parameter_expr =
      parameters[i]->to_exprt(ctx, class_name, function_name);
    call.arguments().push_back(parameter_expr);
    // Hack, manually adding parameters, this should be done at symex
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

#include <iostream>

exprt jimple_newarray::to_exprt(
  contextt &ctx,
  const std::string &class_name,
  const std::string &function_name) const
{
  auto base_type = type->to_typet(ctx);
  auto tmp_symbol =
    get_temp_symbol(pointer_typet(base_type), class_name, function_name);
  symbolt &tmp_added_symbol = *ctx.move_symbol_to_context(tmp_symbol);

  // get alloc type and size
  typet alloc_type = base_type.is_pointer() ? base_type.subtype() : base_type;
  exprt alloc_size = size->to_exprt(ctx, class_name, function_name);

  if (alloc_size.is_nil())
    alloc_size = from_integer(1, uint_type());

  if (alloc_type.is_nil())
    alloc_type = char_type();

  /*
  if(alloc_type.id() == "symbol")
    alloc_type = ns.follow(alloc_type);
*/
  if (alloc_size.type() != uint_type())
  {
    //alloc_size.make_typecast(uint_type());
    //simplify(alloc_size);
  }

  // Create a function call for allocation
  code_function_callt call;
  auto alloca_symbol = get_allocation_function();

  symbolt &added_symbol = *ctx.move_symbol_to_context(alloca_symbol);

  call.function() = symbol_expr(added_symbol);

  // LHS of call is the tmp var
  call.lhs() = symbol_expr(tmp_added_symbol);
  int type_width = 64;
  if (!(base_type.is_pointer() && base_type.subtype().is_pointer()))
  {
    auto to_convert =
      base_type.is_pointer() ? base_type.subtype().width() : base_type.width();

    type_width = std::stoi(to_convert.as_string()); // we want bytes
  }

  auto new_expr = exprt("*", uint_type());
  auto base_size = constant_exprt(
    integer2binary(type_width, 10), integer2string(type_width), uint_type());
  new_expr.move_to_operands(alloc_size, base_size);

  call.arguments().push_back(new_expr);

  // Create a sideffect call to represent the allocation
  side_effect_expr_function_callt sideeffect;
  sideeffect.function() = call.function();
  sideeffect.arguments() = call.arguments();
  sideeffect.location() = call.location();
  sideeffect.type() =
    static_cast<const typet &>(call.function().type().return_type());
  return sideeffect;
};

void jimple_deref::from_json(const json &j)
{
  base = get_expression(j.at("base"));
  index = get_expression(j.at("index"));
}

exprt jimple_deref::to_exprt(
  contextt &ctx,
  const std::string &class_name,
  const std::string &function_name) const
{
  auto arr = base->to_exprt(ctx, class_name, function_name);
  auto i = index->to_exprt(ctx, class_name, function_name);
  auto index = index_exprt(arr, i, arr.type().subtype());
  exprt &array_expr = index.op0();

  exprt addition("+", array_expr.type());
  addition.operands().swap(index.operands());

  index.move_to_operands(addition);
  index.id("dereference");
  index.type() = array_expr.type().subtype();

  return index;
};

exprt jimple_nondet::to_exprt(
  contextt &,
  const std::string &,
  const std::string &) const
{
  auto type = char_type(); // TODO: hashmap here!
  exprt rhs = exprt("sideeffect", type);
  rhs.statement("nondet");

  return rhs;
};

void jimple_static_member::from_json(const json &j)
{
  j.at("base_class").get_to(from);
  j.at("member").get_to(field);
  jimple_type t;
  j.at("type").get_to(t);
  type = std::make_shared<jimple_type>(t);
}

exprt jimple_static_member::to_exprt(
  contextt &ctx,
  const std::string &class_name,
  const std::string &function_name) const
{
  auto result = gen_zero(type->to_typet(ctx));
  // HACK: For now I will set some intrinsics directly (this should go to SYMEX)
  if (from == "kotlin._Assertions" && field == "ENABLED")
  {
    result.make_true();
    return result;
  }

  if (from == "Main" && field == "$assertionsDisabled")
  {
    result.make_false();
    return result;
  }

  // TODO: Needs OOP members

  // 1. Look over the local scope
  auto symbol_name = get_symbol_name(class_name, function_name, from);
  symbolt &s = *ctx.find_symbol(symbol_name);
  member_exprt op(symbol_expr(s), "tag-" + field, s.type);
  exprt &base = op.struct_op();
  if (base.type().is_pointer())
  {
    exprt deref("dereference");
    deref.type() = base.type().subtype();
    deref.move_to_operands(base);
    base.swap(deref);
  }
  return op;
};

void jimple_virtual_member::from_json(const json &j)
{
  j.at("variable").get_to(variable);
  j.at("signature").at("base_class").get_to(from);
  j.at("signature").at("member").get_to(field);
  jimple_type t;
  j.at("signature").at("type").get_to(t);
  type = std::make_shared<jimple_type>(t);
}
#include <iostream>
exprt jimple_virtual_member::to_exprt(
  contextt &ctx,
  const std::string &class_name,
  const std::string &function_name) const
{
  auto result = gen_zero(type->to_typet(ctx));
  // Fix this
  if (from == "com.example.jimplebmc.databinding.ActivityMainBinding")
    return result;

  if (from.find(".databinding.") != std::string::npos)
    return result;

  auto inner_symbol = ctx.find_symbol("tag-" + from);
  if (!inner_symbol)
  {
    throw fmt::format("Unsupported virtual member: {}", from);
  }
  auto struct_type = (*inner_symbol).type;

  // 1. Look over the local scope
  auto symbol_name = get_symbol_name(class_name, function_name, variable);
  symbolt &s = *ctx.find_symbol(symbol_name);
  member_exprt op(symbol_expr(s), "tag-" + field, type->to_typet(ctx));
  exprt &base = op.struct_op();
  if (base.type().is_pointer())
  {
    exprt deref("dereference");
    deref.type() = base.type().subtype();
    deref.move_to_operands(base);
    base.swap(deref);
  }

  return op;
};
