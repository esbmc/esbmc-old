#include <python-frontend/python_converter.h>
#include <python-frontend/json_utils.h>
#include <python_frontend_types.h>
#include <ansi-c/convert_float_literal.h>
#include <util/std_code.h>
#include <util/c_types.h>
#include <util/c_typecast.h>
#include <util/arith_tools.h>
#include <util/expr_util.h>
#include <util/message.h>
#include <util/encoding.h>

#include <fstream>
#include <regex>
#include <unordered_map>

using namespace json_utils;

static const std::unordered_map<std::string, std::string> operator_map = {
  {"Add", "+"},         {"Sub", "-"},          {"Mult", "*"},
  {"Div", "/"},         {"Mod", "mod"},        {"BitOr", "bitor"},
  {"FloorDiv", "/"},    {"BitAnd", "bitand"},  {"BitXor", "bitxor"},
  {"Invert", "bitnot"}, {"LShift", "shl"},     {"RShift", "ashr"},
  {"USub", "unary-"},   {"Eq", "="},           {"Lt", "<"},
  {"LtE", "<="},        {"NotEq", "notequal"}, {"Gt", ">"},
  {"GtE", ">="},        {"And", "and"},        {"Or", "or"},
  {"Not", "not"},
};

static const std::unordered_map<std::string, StatementType> statement_map = {
  {"AnnAssign", StatementType::VARIABLE_ASSIGN},
  {"Assign", StatementType::VARIABLE_ASSIGN},
  {"FunctionDef", StatementType::FUNC_DEFINITION},
  {"If", StatementType::IF_STATEMENT},
  {"AugAssign", StatementType::COMPOUND_ASSIGN},
  {"While", StatementType::WHILE_STATEMENT},
  {"Expr", StatementType::EXPR},
  {"Return", StatementType::RETURN},
  {"Assert", StatementType::ASSERT},
  {"ClassDef", StatementType::CLASS_DEFINITION},
  {"Pass", StatementType::PASS},
  {"Break", StatementType::BREAK},
  {"Continue", StatementType::CONTINUE},
  {"ImportFrom", StatementType::IMPORT},
  {"Import", StatementType::IMPORT}};

static bool is_relational_op(const std::string &op)
{
  return (
    op == "Eq" || op == "Lt" || op == "LtE" || op == "NotEq" || op == "Gt" ||
    op == "GtE" || op == "And" || op == "Or");
}

static StatementType get_statement_type(const nlohmann::json &element)
{
  if (!element.contains("_type"))
    return StatementType::UNKNOWN;

  auto it = statement_map.find(element["_type"]);
  return (it != statement_map.end()) ? it->second : StatementType::UNKNOWN;
}

// Convert Python/AST to irep2 operations
static std::string get_op(const std::string &op, const typet &type)
{
  if (type.is_floatbv())
  {
    if (op == "Add")
      return "ieee_add";
    if (op == "Sub")
      return "ieee_sub";
    if (op == "Mult")
      return "ieee_mul";
    if (op == "Div")
      return "ieee_div";
  }

  auto it = operator_map.find(op);
  if (it != operator_map.end())
  {
    return it->second;
  }
  return std::string();
}

static struct_typet::componentt build_component(
  const std::string &class_name,
  const std::string &comp_name,
  const typet &type)
{
  struct_typet::componentt comp(comp_name, comp_name, type);
  comp.type().set("#member_name", std::string("tag-") + class_name);
  comp.set_access("public");
  return comp;
}

// Convert Python/AST types to irep types
typet python_converter::get_typet(const std::string &ast_type, size_t type_size)
{
  if (ast_type == "float")
    return double_type();
  if (ast_type == "int")
    /* FIXME: We need to map 'int' to another irep type that provides unlimited precision
	https://docs.python.org/3/library/stdtypes.html#numeric-types-int-float-complex */
    return int_type();
  if (ast_type == "uint64" || ast_type == "Epoch" || ast_type == "Slot")
    return long_long_uint_type();
  if (ast_type == "bool")
    return bool_type();
  if (ast_type == "uint256" || ast_type == "BLSFieldElement")
    return uint256_type();
  if (ast_type == "bytes")
  {
    // TODO: Keep "bytes" as signed char instead of "int_type()", and cast to an 8-bit integer in [] operations
    // or consider modelling it with string_constantt.
    typet t = array_typet(
      int_type(),
      constant_exprt(
        integer2binary(BigInt(type_size), bv_width(size_type())),
        integer2string(BigInt(type_size)),
        size_type()));
    return t;
  }
  if (ast_type == "str")
  {
    if (type_size == 1)
    {
      typet type = char_type();
      type.set("#cpp_type", "char");
      return type;
    }

    typet t = array_typet(
      char_type(),
      constant_exprt(
        integer2binary(BigInt(type_size), bv_width(size_type())),
        integer2string(BigInt(type_size)),
        size_type()));
    return t;
  }
  if (is_class(ast_type, ast_json))
    return symbol_typet("tag-" + ast_type);
  return empty_typet();
}

typet python_converter::get_typet(const nlohmann::json &elem)
{
  if (elem.is_number_integer() || elem.is_number_unsigned())
    return int_type();
  else if (elem.is_boolean())
    return bool_type();
  else if (elem.is_number_float())
    return float_type();

  log_error("Invalid type\n");
  abort();
}

static symbolt create_symbol(
  const std::string &module,
  const std::string &name,
  const std::string &id,
  const locationt &location,
  const typet &type)
{
  symbolt symbol;
  symbol.mode = "Python";
  symbol.module = module;
  symbol.location = location;
  symbol.type = type;
  symbol.name = name;
  symbol.id = id;
  return symbol;
}

static ExpressionType get_expression_type(const nlohmann::json &element)
{
  if (!element.contains("_type"))
    return ExpressionType::UNKNOWN;

  auto type = element["_type"];

  if (type == "UnaryOp")
    return ExpressionType::UNARY_OPERATION;

  if (type == "BinOp" || type == "Compare")
    return ExpressionType::BINARY_OPERATION;

  if (type == "BoolOp")
    return ExpressionType::LOGICAL_OPERATION;

  if (type == "Constant")
    return ExpressionType::LITERAL;

  if (type == "Name" || type == "Attribute")
    return ExpressionType::VARIABLE_REF;

  if (type == "Call")
    return ExpressionType::FUNC_CALL;

  if (type == "IfExp")
    return ExpressionType::IF_EXPR;

  if (type == "Subscript")
    return ExpressionType::SUBSCRIPT;

  return ExpressionType::UNKNOWN;
}

exprt python_converter::get_logical_operator_expr(const nlohmann::json &element)
{
  std::string op(element["op"]["_type"].get<std::string>());
  exprt logical_expr(get_op(op, bool_type()), bool_type());

  // Iterate over operands of logical operations (and/or)
  for (const auto &operand : element["values"])
  {
    exprt operand_expr = get_expr(operand);
    logical_expr.copy_to_operands(operand_expr);
  }
  return logical_expr;
}

void python_converter::adjust_statement_types(exprt &lhs, exprt &rhs) const
{
  typet &lhs_type = lhs.type();
  typet &rhs_type = rhs.type();

  auto update_symbol = [&](exprt &expr) {
    std::string id = create_symbol_id() + "@" + expr.name().c_str();
    symbolt *s = context.find_symbol(id);
    if (s != nullptr)
    {
      s->type = expr.type();
      s->value.type() = expr.type();

      if (
        s->value.is_constant() ||
        (s->value.is_signedbv() || s->value.is_unsignedbv()))
      {
        exprt new_value =
          from_integer(std::stoi(s->value.value().c_str()), expr.type());
        s->value = new_value;
      }
    }
  };

  if (lhs_type.width() != rhs_type.width())
  {
    int lhs_type_width = std::stoi(lhs_type.width().c_str());
    int rhs_type_width = std::stoi(rhs_type.width().c_str());

    if (lhs_type_width > rhs_type_width)
    {
      // Update rhs symbol value to match with new type
      rhs_type = lhs_type;
      update_symbol(rhs);
    }
    else
    {
      // Update lhs symbol value to match with new type
      lhs_type = rhs_type;
      update_symbol(lhs);
    }
  }
}

std::string
python_converter::create_symbol_id(const std::string &filename) const
{
  std::stringstream symbol_id;
  symbol_id << "py:" << filename;

  if (!current_class_name.empty())
    symbol_id << "@C@" << current_class_name;

  if (!current_func_name.empty())
    symbol_id << "@F@" << current_func_name;

  return symbol_id.str();
}

std::string python_converter::create_symbol_id() const
{
  return create_symbol_id(python_filename);
}

// Get the type of an operand in binary operations
std::string python_converter::get_operand_type(const nlohmann::json &element)
{
  // Operand is a variable
  if (element["_type"] == "Name")
    return get_var_type(element["id"]);

  // Operand is a literal
  if (element["_type"] == "Constant")
  {
    const auto &value = element["value"];
    if (value.is_string())
      return "str";
    if (value.is_number_integer() || value.is_number_unsigned())
      return "int";
    else if (value.is_boolean())
      return "bool";
    else if (value.is_number_float())
      return "float";
  }
  return std::string();
}

exprt python_converter::get_binary_operator_expr(const nlohmann::json &element)
{
  auto left = (element.contains("left")) ? element["left"] : element["target"];

  decltype(left) right;
  if (element.contains("right"))
    right = element["right"];
  else if (element.contains("comparators"))
    right = element["comparators"][0];
  else if (element.contains("value"))
    right = element["value"];

  exprt lhs = get_expr(left);
  exprt rhs = get_expr(right);

  auto to_side_effect_call = [](exprt &expr) {
    side_effect_expr_function_callt side_effect;
    code_function_callt &code = static_cast<code_function_callt &>(expr);
    side_effect.function() = code.function();
    side_effect.location() = code.location();
    side_effect.type() = code.type();
    side_effect.arguments() = code.arguments();
    expr = side_effect;
  };

  // Function calls in expressions like "fib(n-1) + fib(n-2)" need to be converted to side effects
  if (lhs.is_function_call())
    to_side_effect_call(lhs);
  if (rhs.is_function_call())
    to_side_effect_call(rhs);

  std::string op;

  if (element.contains("op"))
    op = element["op"]["_type"].get<std::string>();
  else if (element.contains("ops"))
    op = element["ops"][0]["_type"].get<std::string>();

  assert(!op.empty());

  // Get LHS and RHS types from variable annotation
  std::string lhs_type = get_operand_type(left);
  std::string rhs_type = get_operand_type(right);

  // If RHS is a string literal, like x = "foo", then we determine the type from the JSON value
  if (
    rhs_type.empty() && element.contains("comparators") &&
    element["comparators"][0].contains("value") &&
    element["comparators"][0]["value"].is_string())
  {
    rhs_type = "str";
  }

  if (lhs_type == "str" && rhs_type == "str")
  {
    // Strings comparison
    if (op == "Eq")
    {
      if (rhs.type() != lhs.type())
        return gen_boolean(false);

      array_typet &arr_type = static_cast<array_typet &>(lhs.type());
      BigInt str_size =
        binary2integer(arr_type.size().value().as_string(), false);

      // call strncmp to compare strings
      symbolt *strncmp = context.find_symbol("c:@F@strncmp");
      assert(strncmp);
      side_effect_expr_function_callt sideeffect;
      sideeffect.function() = symbol_expr(*strncmp);
      sideeffect.arguments().push_back(lhs); // passing lhs to strncmp
      sideeffect.arguments().push_back(rhs); // passing rhs to strncmp
      sideeffect.arguments().push_back(
        from_integer(str_size, long_uint_type())); // passing n to strncmp
      sideeffect.location() = get_location_from_decl(element);
      sideeffect.type() = int_type();

      lhs = sideeffect;
      rhs = gen_zero(int_type());
    }
    // Strings concatenation
    else if (op == "Add")
    {
      array_typet lhs_str_type = static_cast<array_typet &>(lhs.type());
      BigInt lhs_str_size =
        binary2integer(lhs_str_type.size().value().c_str(), true);

      array_typet rhs_str_type = static_cast<array_typet &>(rhs.type());
      BigInt rhs_str_size =
        binary2integer(rhs_str_type.size().value().c_str(), true);

      BigInt concat_str_size = lhs_str_size + rhs_str_size;

      typet t = get_typet("str", concat_str_size.to_uint64());
      exprt expr = gen_zero(t);

      unsigned int i = 0;

      auto get_value_from_symbol = [&](const std::string &symbol_id, exprt &e) {
        symbolt *symbol = context.find_symbol(symbol_id);
        assert(symbol);
        // Copy symbol value
        for (const exprt &ch : symbol->value.operands())
          e.operands().at(i++) = ch;
      };

      auto get_value_from_json = [&](const nlohmann::json &elem, exprt &e) {
        const std::string &value = elem["value"].get<std::string>();
        std::vector<uint8_t> string_literal =
          std::vector<uint8_t>(std::begin(value), std::end(value));

        typet &char_type = t.subtype();

        // Copy JSON value
        for (uint8_t &ch : string_literal)
        {
          exprt char_value = constant_exprt(
            integer2binary(BigInt(ch), bv_width(char_type)),
            integer2string(BigInt(ch)),
            char_type);

          e.operands().at(i++) = char_value;
        }
      };

      // If LHS is a variable
      if (left["_type"] == "Name")
        get_value_from_symbol(lhs.identifier().as_string(), expr);
      // If LHS is a literal
      else if (left["_type"] == "Constant")
        get_value_from_json(left, expr);

      // If RHS is a variable
      if (right["_type"] == "Name")
        get_value_from_symbol(rhs.identifier().as_string(), expr);
      // If RHS is a literal
      else if (right["_type"] == "Constant")
        get_value_from_json(right, expr);

      return expr;
    }
  }

  adjust_statement_types(lhs, rhs);

  assert(lhs.type() == rhs.type());

  // Replace ** operation with the resultant constant.
  if (op == "Pow")
  {
    BigInt base(
      binary2integer(lhs.value().as_string(), lhs.type().is_signedbv()));
    BigInt exp(
      binary2integer(rhs.value().as_string(), rhs.type().is_signedbv()));
    constant_exprt pow_expr(power(base, exp), lhs.type());
    return pow_expr;
  }

  typet type = (is_relational_op(op)) ? bool_type() : lhs.type();
  exprt bin_expr(get_op(op, type), type);
  bin_expr.copy_to_operands(lhs, rhs);

  // floor division (//) operation corresponds to an int division with floor rounding
  // So we need to emulate this behaviour here:
  // int result = (num/div) - (num%div != 0 && ((num < 0) ^ (den<0)) ? 1 : 0)
  // e.g.: -5//2 equals to -3, and 5//2 equals to 2
  if (op == "FloorDiv")
  {
    typet div_type = bin_expr.type();
    // remainder = num%den;
    exprt remainder("mod", div_type);
    remainder.copy_to_operands(lhs, rhs);

    // Get num signal
    exprt is_num_neg("<", bool_type());
    is_num_neg.copy_to_operands(lhs, gen_zero(div_type));
    // Get den signal
    exprt is_den_neg("<", bool_type());
    is_den_neg.copy_to_operands(rhs, gen_zero(div_type));

    // remainder != 0
    exprt pos_remainder("notequal", bool_type());
    pos_remainder.copy_to_operands(remainder, gen_zero(div_type));

    // diff_signals = is_num_neg ^ is_den_neg;
    exprt diff_signals("bitxor", bool_type());
    diff_signals.copy_to_operands(is_num_neg, is_den_neg);

    exprt cond("and", bool_type());
    cond.copy_to_operands(pos_remainder, diff_signals);
    exprt if_expr("if", div_type);
    if_expr.copy_to_operands(cond, gen_one(div_type), gen_zero(div_type));

    // floor_div = (lhs / rhs) - (1 if (lhs % rhs != 0) and (lhs < 0) ^ (rhs < 0) else 0)
    exprt floor_div("-", div_type);
    floor_div.copy_to_operands(bin_expr, if_expr); //bin_expr contains lhs/rhs

    return floor_div;
  }

  return bin_expr;
}

exprt python_converter::get_unary_operator_expr(const nlohmann::json &element)
{
  typet type = current_element_type;
  if (element["operand"].contains("value"))
    type = get_typet(element["operand"]["value"]);

  exprt unary_expr(
    get_op(element["op"]["_type"].get<std::string>(), type), type);

  // get subexpr
  exprt unary_sub = get_expr(element["operand"]);
  unary_expr.operands().push_back(unary_sub);

  return unary_expr;
}

const nlohmann::json python_converter::find_var_decl(
  const std::string &var_name,
  const nlohmann::json &json) const
{
  for (auto &element : json["body"])
  {
    if (
      element["_type"] == "AnnAssign" && element["target"].contains("id") &&
      element["target"]["id"] == var_name)
      return element;
  }
  return nlohmann::json();
}

locationt
python_converter::get_location_from_decl(const nlohmann::json &ast_node)
{
  locationt location;
  if (ast_node.contains("lineno"))
    location.set_line(ast_node["lineno"].get<int>());

  if (ast_node.contains("col_offset"))
    location.set_column(ast_node["col_offset"].get<int>());

  location.set_file(python_filename.c_str());
  location.set_function(current_func_name);
  return location;
}

symbolt *python_converter::find_function_in_base_classes(
  const std::string &class_name,
  const std::string &symbol_id,
  std::string method_name,
  bool is_ctor) const
{
  symbolt *func = nullptr;

  // Find class node in the AST
  auto class_node = json_utils::find_class(ast_json["body"], class_name);

  if (class_node != nlohmann::json())
  {
    std::string current_class = class_name;
    std::string current_func_name = (is_ctor) ? class_name : method_name;
    std::string sym_id = symbol_id;
    // Search for method in all bases classes
    for (const auto &base_class_node : class_node["bases"])
    {
      const std::string &base_class = base_class_node["id"].get<std::string>();
      if (is_ctor)
        method_name = base_class;

      std::size_t pos = sym_id.rfind("@C@" + current_class);

      sym_id.replace(
        pos,
        std::string("@C@" + current_class + "@F@" + current_func_name).length(),
        std::string("@C@" + base_class + "@F@" + method_name));

      if ((func = context.find_symbol(sym_id.c_str())))
        return func;

      current_class = base_class;
    }
  }

  return func;
}

symbolt *python_converter::find_function_in_imported_modules(
  const std::string &symbol_id) const
{
  for (const auto &obj : ast_json["body"])
  {
    if (obj["_type"] == "ImportFrom")
    {
      std::regex pattern("py:(.*?)@");
      std::string imported_symbol = std::regex_replace(
        symbol_id, pattern, "py:" + obj["full_path"].get<std::string>() + "@");

      if (symbolt *func_symbol = context.find_symbol(imported_symbol.c_str()))
        return func_symbol;
    }
  }
  return nullptr;
}

symbolt *
python_converter::find_symbol_in_global_scope(std::string &symbol_id) const
{
  std::size_t class_start_pos = symbol_id.find("@C@");
  std::size_t func_start_pos = symbol_id.find("@F@");

  // Remove class name from symbol
  if (class_start_pos != std::string::npos)
    symbol_id.erase(class_start_pos, func_start_pos - class_start_pos);

  func_start_pos = symbol_id.find("@F@");
  std::size_t func_end_pos = symbol_id.rfind("@");

  // Remove function name from symbol
  if (func_start_pos != std::string::npos)
    symbol_id.erase(func_start_pos, func_end_pos - func_start_pos);

  return context.find_symbol(symbol_id);
}

std::string python_converter::get_classname_from_symbol_id(
  const std::string &symbol_id) const
{
  // This function might return "Base" for a symbol_id as: py:main.py@C@Base@F@foo@self

  std::string class_name;
  size_t class_pos = symbol_id.find("@C@");
  size_t func_pos = symbol_id.find("@F@");

  if (class_pos != std::string::npos && func_pos != std::string::npos)
  {
    size_t length = func_pos - (class_pos + 3); // "+3" to ignore "@C@"
    // Extract substring between "@C@" and "@F@"
    class_name = symbol_id.substr(class_pos + 3, length);
  }
  return class_name;
}

function_id python_converter::build_function_id(const nlohmann::json &element)
{
  const std::string __ESBMC_get_object_size = "__ESBMC_get_object_size";
  const std::string __ESBMC_assume = "__ESBMC_assume";
  const std::string __VERIFIER_assume = "__VERIFIER_assume";

  bool is_member_function_call = false;
  const nlohmann::json &func_json = element["func"];
  const std::string &func_type = func_json["_type"];
  std::string func_name, obj_name, class_name;
  std::string func_symbol_id = create_symbol_id();

  if (func_type == "Name")
    func_name = func_json["id"];
  else if (func_type == "Attribute") // Handling obj_name.func_name() calls
  {
    is_member_function_call = true;
    func_name = func_json["attr"];
    if (func_json["value"]["_type"] == "Attribute")
      obj_name = func_json["value"]["attr"];
    else
      obj_name = func_json["value"]["id"];

    if (
      !is_class(obj_name, ast_json) &&
      json_utils::is_module(obj_name, ast_json))
    {
      func_symbol_id = create_symbol_id(imported_modules[obj_name]);
      is_member_function_call = false;
    }
  }

  // build symbol_id
  if (func_name == "len")
  {
    func_name = __ESBMC_get_object_size;
    func_symbol_id = "c:@F@" + __ESBMC_get_object_size;
  }
  else if (is_builtin_type(obj_name))
  {
    class_name = obj_name;
    std::stringstream ss;
    ss << "py:" + python_filename + "@C@" + class_name + "@F@" + func_name;
    func_symbol_id = ss.str();
  }
  else if (func_name == __ESBMC_assume || func_name == __VERIFIER_assume)
    func_symbol_id = func_name;
  else if (func_symbol_id.find("@F@") == std::string::npos)
    func_symbol_id += "@F@" + func_name;

  bool is_ctor_call = is_constructor_call(element);

  // Insert class name in the symbol id
  if (is_ctor_call || is_member_function_call)
  {
    std::size_t pos = func_symbol_id.rfind("@F@");
    if (pos != std::string::npos)
    {
      if (is_ctor_call)
        class_name = func_name;
      else if (is_member_function_call)
      {
        assert(!obj_name.empty());
        if (is_builtin_type(obj_name) || is_class(obj_name, ast_json))
          class_name = obj_name;
        else
        {
          auto obj_node = find_var_decl(obj_name, ast_json);
          if (obj_node.empty())
            abort();

          class_name = obj_node["annotation"]["id"].get<std::string>();
        }
      }
      func_symbol_id.insert(pos, "@C@" + class_name);
    }
  }

  return {func_name, func_symbol_id, class_name};
}

exprt python_converter::get_function_call(const nlohmann::json &element)
{
  // TODO: Refactor into different classes/functions
  if (element.contains("func") && element["_type"] == "Call")
  {
    function_id func_id = build_function_id(element);
    std::string func_name(func_id.function_name);

    // nondet_X() functions restricted to basic types supported in Python
    std::regex pattern(
      R"(nondet_(int|char|bool|float)|__VERIFIER_nondet_(int|char|bool|float))");

    // Handle non-det functions
    if (std::regex_match(func_name, pattern))
    {
      // Function name pattern: nondet_(type). e.g: nondet_bool(), nondet_int()
      size_t underscore_pos = func_name.rfind("_");
      std::string type = func_name.substr(underscore_pos + 1);
      exprt rhs = exprt("sideeffect", get_typet(type));
      rhs.statement("nondet");
      return rhs;
    }

    if (is_builtin_type(func_name) || is_consensus_type(func_name))
    {
      /* Calls to initialize variables using built-in type functions such as int(1), str("test"), bool(1)
       * are converted to simple variable assignments, simplifying the handling of built-in type objects.
       * For example, x = int(1) becomes x = 1. */
      size_t arg_size = 1;
      const auto &arg = element["args"][0];

      if (func_name == "str")
        arg_size = arg["value"].get<std::string>().size(); // get string length

      typet t = get_typet(func_name, arg_size);
      exprt expr = get_expr(arg);
      expr.type() = t;
      return expr;
    }

    locationt location = get_location_from_decl(element);
    const std::string func_symbol_id(func_id.symbol_id);
    assert(!func_symbol_id.empty());

    if (
      func_name == "__ESBMC_assume" || func_name == "__VERIFIER_assume" ||
      func_name == "__ESBMC_get_object_size")
    {
      if (context.find_symbol(func_symbol_id.c_str()) == nullptr)
      {
        // Create/init __ESBMC_get_object_size symbol
        code_typet code_type;
        if (func_name == "__ESBMC_get_object_size")
        {
          code_type.return_type() = int_type();
          code_type.arguments().push_back(pointer_typet(empty_typet()));
        }

        symbolt symbol = create_symbol(
          python_filename, func_name, func_symbol_id, location, code_type);
        context.add(symbol);
      }
    }

    bool is_ctor_call = is_constructor_call(element);
    bool is_instance_method_call = false;
    bool is_class_method_call = false;
    symbolt *obj_symbol = nullptr;
    std::string obj_symbol_id("");

    if (element["func"]["_type"] == "Attribute")
    {
      const auto &subelement = element["func"]["value"];
      const std::string &caller = subelement["_type"] == "Attribute"
                                    ? subelement["attr"].get<std::string>()
                                    : subelement["id"].get<std::string>();

      obj_symbol_id = create_symbol_id() + "@" + caller;
      obj_symbol = context.find_symbol(obj_symbol_id);

      // Handling a function call as a class method call when:
      // (1) The caller corresponds to a class name, for example: MyClass.foo().
      // (2) Calling methods of built-in types, such as int.from_bytes()
      //     All the calls to built-in methods are handled by class methods in operational models.
      // (3) Calling a instance method from a built-in type object, for example: x.bit_length() when x is an int
      // If the caller is a class or a built-in type, the following condition detects a class method call.
      if (
        is_class(caller, ast_json) || is_builtin_type(caller) ||
        is_builtin_type(get_var_type(caller)))
      {
        is_class_method_call = true;
      }
      else if (!json_utils::is_module(caller, ast_json))
      {
        is_instance_method_call = true;
      }
    }

    const symbolt *func_symbol = context.find_symbol(func_symbol_id.c_str());

    // Find function in imported modules
    if (!func_symbol)
      func_symbol = find_function_in_imported_modules(func_symbol_id);

    if (func_symbol == nullptr)
    {
      if (is_ctor_call || is_instance_method_call)
      {
        // Get method from a base class when it is not defined in the current class
        const std::string class_name(func_id.class_name);

        func_symbol = find_function_in_base_classes(
          class_name, func_symbol_id, func_name, is_ctor_call);

        if (is_ctor_call)
        {
          if (!func_symbol)
          {
            // If __init__() is not defined for the class and bases,
            // an assignment (x = MyClass()) is converted to a declaration (x:MyClass) in get_var_assign().
            return exprt("_init_undefined");
          }
          base_ctor_called = true;
        }
        else if (is_instance_method_call)
        {
          assert(obj_symbol);

          // Update obj attributes from self
          update_instance_from_self(
            get_classname_from_symbol_id(func_symbol->id.as_string()),
            func_name,
            obj_symbol_id);
        }
      }
      else
      {
        log_warning("Undefined function: {}", func_name.c_str());
        return exprt();
      }
    }

    code_function_callt call;
    call.location() = location;
    call.function() = symbol_expr(*func_symbol);
    const typet &return_type = to_code_type(func_symbol->type).return_type();
    call.type() = return_type;

    // Add self as first parameter
    if (is_ctor_call)
    {
      // Self is the lhs
      assert(ref_instance);
      call.arguments().push_back(gen_address_of(*ref_instance));
    }
    else if (is_instance_method_call)
    {
      assert(obj_symbol);
      // Passing object as "self" (first) parameter on instance method calls
      call.arguments().push_back(gen_address_of(symbol_expr(*obj_symbol)));
    }
    else if (is_class_method_call)
    {
      // Passing a void pointer to the "cls" argument
      typet t = pointer_typet(empty_typet());
      call.arguments().push_back(gen_zero(t));

      // All methods for the int class without parameters acts solely on the encapsulated integer value.
      // Therefore, we always pass the caller (obj) as a parameter in these functions.
      // For example, if x is an int instance, x.bit_length() call becomes bit_length(x)
      if (
        obj_symbol && get_var_type(obj_symbol->name.as_string()) == "int" &&
        element["args"].empty())
      {
        call.arguments().push_back(symbol_expr(*obj_symbol));
      }
    }

    for (const auto &arg_node : element["args"])
    {
      exprt arg = get_expr(arg_node);
      if (func_name == "__ESBMC_get_object_size")
      {
        c_typecastt c_typecast(ns);
        c_typecast.implicit_typecast(arg, pointer_typet(empty_typet()));
      }

      // All array function arguments (e.g. bytes type) are handled as pointers.
      if (arg.type().is_array())
        call.arguments().push_back(address_of_exprt(arg));
      else
        call.arguments().push_back(arg);
    }

    if (func_name == "__ESBMC_get_object_size")
    {
      side_effect_expr_function_callt sideeffect;
      sideeffect.function() = call.function();
      sideeffect.arguments() = call.arguments();
      sideeffect.location() = call.location();
      sideeffect.type() =
        static_cast<const typet &>(call.function().type().return_type());
      return sideeffect;
    }

    return call;
  }

  log_error("Invalid function call");
  abort();
}

exprt python_converter::get_literal(const nlohmann::json &element)
{
  auto value = element["value"];

  // integer literals
  if (value.is_number_integer())
    return from_integer(value.get<int>(), int_type());

  // bool literals
  if (value.is_boolean())
    return gen_boolean(value.get<bool>());

  // float literals
  if (value.is_number_float())
  {
    exprt expr;
    convert_float_literal(value.dump(), expr);
    return expr;
  }

  // char literals
  if (value.is_string() && value.get<std::string>().size() == 1)
  {
    const std::string &str = value.get<std::string>();
    typet t = get_typet("str", str.size());
    return from_integer(str[0], t);
  }

  // Docstrings are ignored
  if (value.get<std::string>()[0] == '\n')
    return exprt();

  // bytes/string literals
  if (value.is_string())
  {
    typet t = current_element_type;
    std::vector<uint8_t> string_literal;

    // "bytes" literals
    if (element.contains("encoded_bytes"))
    {
      string_literal =
        base64_decode(element["encoded_bytes"].get<std::string>());
    }
    else // string literals
    {
      t = get_typet("str", value.get<std::string>().size());
      const std::string &value = element["value"].get<std::string>();
      string_literal = std::vector<uint8_t>(std::begin(value), std::end(value));
    }

    typet &char_type = t.subtype();
    exprt expr = gen_zero(t);

    // Initialise array
    unsigned int i = 0;
    for (uint8_t &ch : string_literal)
    {
      exprt char_value = constant_exprt(
        integer2binary(BigInt(ch), bv_width(char_type)),
        integer2string(BigInt(ch)),
        char_type);
      expr.operands().at(i++) = char_value;
    }
    return expr;
  }

  log_error("Unsupported literal: {}\n", value.get<std::string>());
  abort();
}

exprt python_converter::get_expr(const nlohmann::json &element)
{
  exprt expr;
  ExpressionType type = get_expression_type(element);

  switch (type)
  {
  case ExpressionType::UNARY_OPERATION:
  {
    expr = get_unary_operator_expr(element);
    break;
  }
  case ExpressionType::BINARY_OPERATION:
  {
    expr = get_binary_operator_expr(element);
    break;
  }
  case ExpressionType::LOGICAL_OPERATION:
  {
    expr = get_logical_operator_expr(element);
    break;
  }
  case ExpressionType::LITERAL:
  {
    expr = get_literal(element);
    break;
  }
  case ExpressionType::VARIABLE_REF:
  {
    std::string var_name;
    bool is_class_attr = false;
    if (element["_type"] == "Name")
    {
      var_name = element["id"].get<std::string>();
    }
    else if (element["_type"] == "Attribute")
    {
      var_name = element["value"]["id"].get<std::string>();
      if (is_class(var_name, ast_json))
      {
        // Found a class attribute
        var_name = "C@" + var_name;
        is_class_attr = true;
      }
    }

    assert(!var_name.empty());

    std::string symbol_id = create_symbol_id() + std::string("@") + var_name;

    if (element.contains("attr") && is_class_attr)
      symbol_id += "@" + element["attr"].get<std::string>();

    symbolt *symbol = context.find_symbol(symbol_id);
    if (!symbol)
    {
      symbol = find_symbol_in_global_scope(symbol_id);
      if (!symbol)
      {
        log_error("Symbol not found: {}\n", symbol_id.c_str());
        abort();
      }
    }

    expr = symbol_expr(*symbol);

    // Get instance attribute
    if (!is_class_attr && element["_type"] == "Attribute")
    {
      const std::string &attr_name = element["attr"].get<std::string>();

      // Get object type name from symbol. e.g.: tag-MyClass
      std::string obj_type_name;
      const typet &symbol_type =
        (symbol->type.is_pointer()) ? symbol->type.subtype() : symbol->type;
      for (const auto &it : symbol_type.get_named_sub())
      {
        if (it.first == "identifier")
          obj_type_name = it.second.id_string();
      }

      // Get class definition from symbols table
      symbolt *class_symbol = context.find_symbol(obj_type_name);
      if (!class_symbol)
      {
        log_error("Class not found: {}\n", obj_type_name);
        abort();
      }

      struct_typet &class_type =
        static_cast<struct_typet &>(class_symbol->type);

      if (is_converting_lhs)
      {
        // Add member in the class
        if (!class_type.has_component(attr_name))
        {
          struct_typet::componentt comp = build_component(
            class_type.tag().as_string(), attr_name, current_element_type);
          class_type.components().push_back(comp);
        }
        // Add instance attribute in the objects map
        instance_attr_map[symbol->id.as_string()].insert(attr_name);
      }

      auto is_instance_attr = [&]() -> bool {
        auto it = instance_attr_map.find(symbol->id.as_string());
        if (it != instance_attr_map.end())
        {
          for (const auto &attr : it->second)
          {
            if (attr == attr_name)
              return true;
          }
        }
        return false;
      };

      // Get instance attribute from class component
      if (
        class_type.has_component(attr_name) &&
        (is_instance_attr() || is_converting_rhs))
      {
        const typet &attr_type = class_type.get_component(attr_name).type();
        expr = member_exprt(
          symbol_exprt(symbol->id, symbol->type), attr_name, attr_type);
      }
      // Fallback to class attribute when instance attribute is not found
      else
      {
        // All class attributes are static symbols with ids in the format: filename@C@classname@varname
        symbol_id = "py:" + python_filename + "@C@" + obj_type_name.substr(4) +
                    "@" + attr_name;
        symbolt *class_attr_symbol = context.find_symbol(symbol_id);
        if (!class_attr_symbol)
        {
          log_error("Attribute {} not found\n", attr_name);
          abort();
        }
        expr = symbol_expr(*class_attr_symbol);
      }
    }
    break;
  }
  case ExpressionType::FUNC_CALL:
  {
    expr = get_function_call(element);
    break;
  }
  // Ternary operator
  case ExpressionType::IF_EXPR:
  {
    expr = get_conditional_stm(element);
    break;
  }
  case ExpressionType::SUBSCRIPT:
  {
    exprt array = get_expr(element["value"]);
    typet t = array.type().subtype();

    const nlohmann::json &slice = element["slice"];
    exprt pos = get_expr(slice);

    // Adjust negative indexes
    if (slice.contains("op") && slice["op"]["_type"] == "USub")
    {
      BigInt v = binary2integer(pos.op0().value().c_str(), true);
      v *= -1;
      array_typet t = static_cast<array_typet &>(array.type());
      BigInt s = binary2integer(t.size().value().c_str(), true);
      v += s;
      pos = from_integer(v, pos.type());
    }
    expr = index_exprt(array, pos, t);
    break;
  }
  default:
  {
    if (element.contains("_type"))
      log_error(
        "Unsupported expression type: {}", element["_type"].get<std::string>());
    abort();
  }
  }

  return expr;
}

bool python_converter::is_constructor_call(const nlohmann::json &json)
{
  if (
    !json.contains("_type") || json["_type"] != "Call" ||
    !json["func"].contains("id"))
    return false;

  const std::string &func_name = json["func"]["id"];

  if (is_builtin_type(func_name))
    return false;

  /* f:Foo = Foo()
   * The statement is a constructor call if the function call on the
   * rhs corresponds to the name of a class. */

  bool is_ctor_call = false;
  context.foreach_operand([&](const symbolt &s) {
    if (s.type.id() == "struct" && s.name == func_name)
    {
      is_ctor_call = true;
      return;
    }
  });
  return is_ctor_call;
}

void python_converter::update_instance_from_self(
  const std::string &class_name,
  const std::string &func_name,
  const std::string &obj_symbol_id)
{
  std::string self_id =
    create_symbol_id() + "@C@" + class_name + "@F@" + func_name + "@self";

  auto self_instance = instance_attr_map.find(self_id);
  if (self_instance != instance_attr_map.end())
  {
    std::set<std::string> &attr_list = instance_attr_map[obj_symbol_id];
    attr_list.insert(
      self_instance->second.begin(), self_instance->second.end());
  }
}

size_t get_type_size(const nlohmann::json &ast_node)
{
  size_t type_size = 0;
  if (ast_node["value"].contains("value"))
  {
    if (ast_node["annotation"]["id"] == "bytes")
    {
      const std::string &str =
        ast_node["value"]["encoded_bytes"].get<std::string>();
      std::vector<uint8_t> decoded = base64_decode(str);
      type_size = decoded.size();
    }
    else if (ast_node["value"]["value"].is_string())
      type_size = ast_node["value"]["value"].get<std::string>().size();
  }
  else if (
    ast_node["value"].contains("args") &&
    ast_node["value"]["args"].size() > 0 &&
    ast_node["value"]["args"][0].contains("value") &&
    ast_node["value"]["args"][0]["value"].is_string())
    type_size = ast_node["value"]["args"][0]["value"].get<std::string>().size();

  return type_size;
}

std::string python_converter::get_var_type(const std::string &var_name) const
{
  nlohmann::json ref;
  // Get variable from current function
  for (const auto &elem : ast_json["body"])
  {
    if (elem["_type"] == "FunctionDef" && elem["name"] == current_func_name)
      ref = find_var_decl(var_name, elem);
  }

  // Get variable from global scope
  if (ref.empty())
    ref = find_var_decl(var_name, ast_json);

  if (ref.empty())
    return std::string();

  return ref["annotation"]["id"].get<std::string>();
}

void python_converter::get_var_assign(
  const nlohmann::json &ast_node,
  codet &target_block)
{
  std::string lhs_type("");
  if (ast_node.contains("annotation"))
  {
    // Get type from annotation node
    size_t type_size = get_type_size(ast_node);
    lhs_type = ast_node["annotation"]["id"];
    current_element_type = get_typet(lhs_type, type_size);
  }
  else
  {
    // Get type from declaration node
    const std::string &var_name =
      ast_node["targets"][0]["id"].get<std::string>();
    lhs_type = get_var_type(var_name);

    if (lhs_type.empty())
    {
      log_error("Type undefined for {}", var_name);
      abort();
    }
    current_element_type = get_typet(lhs_type);
  }

  exprt lhs;
  symbolt *lhs_symbol = nullptr;
  locationt location_begin;

  if (ast_node["_type"] == "AnnAssign")
  {
    // Id and name
    std::string name, id;
    auto target = ast_node["target"];
    if (!target.is_null())
    {
      if (target["_type"] == "Name")
        name = target["id"];
      else if (target["_type"] == "Attribute")
        name = target["attr"];

      id = create_symbol_id() + "@" + name;
    }

    assert(!name.empty() && !id.empty());

    // Location
    location_begin = get_location_from_decl(ast_node["target"]);

    // Debug module name
    std::string module_name = location_begin.get_file().as_string();

    // Create/init symbol
    symbolt symbol = create_symbol(
      module_name, name, id, location_begin, current_element_type);
    symbol.lvalue = true;
    symbol.static_lifetime = false;
    symbol.file_local = true;
    symbol.is_extern = false;

    if (target["_type"] == "Attribute")
    {
      is_converting_lhs = true;
      lhs = get_expr(target); // lhs is a obj.member expression
    }
    else
      lhs = symbol_expr(symbol); // lhs is a simple variable

    lhs.location() = location_begin;
    lhs_symbol = context.move_symbol_to_context(symbol);
  }
  else if (ast_node["_type"] == "Assign")
  {
    const std::string &name = ast_node["targets"][0]["id"].get<std::string>();
    std::string symbol_id = create_symbol_id() + "@" + name;
    lhs_symbol = context.find_symbol(symbol_id);
    assert(lhs_symbol);
    lhs = symbol_expr(*lhs_symbol);
  }

  bool is_ctor_call = is_constructor_call(ast_node["value"]);

  if (is_ctor_call)
    ref_instance = &lhs;

  is_converting_lhs = false;

  // Get RHS
  exprt rhs;
  bool has_value = false;
  if (!ast_node["value"].is_null())
  {
    is_converting_rhs = true;
    rhs = get_expr(ast_node["value"]);
    has_value = true;
    is_converting_rhs = false;
  }

  if (has_value && rhs != exprt("_init_undefined"))
  {
    if (lhs_symbol)
    {
      if (lhs_type == "str")
      {
        array_typet &arr_type =
          static_cast<array_typet &>(current_element_type);

        /* When a string is assigned the result of a concatenation, we initially
         * create the LHS type as a zero-size array: "current_element_type = get_typet(lhs_type, type_size);"
         * After parsing the RHS, we need to adjust the LHS type size to match
         * the size of the resulting RHS string.*/

        // If the size of the LHS type is zero, update the LHS type with the type of the RHS.
        if (std::stoi(arr_type.size().value().as_string()) == 0)
          lhs_symbol->type = rhs.type();
      }

      lhs_symbol->value = rhs;
    }

    /* If the right-hand side (rhs) of the assignment is a function call, such as: x : int = func()
     * we need to adjust the left-hand side (lhs) of the function call to refer to the lhs of the current assignment.
     */
    if (rhs.is_function_call())
    {
      // If rhs is a constructor call so it is necessary to update lhs instance attributes with members added in self
      if (is_ctor_call)
      {
        std::string func_name = ast_node["value"]["func"]["id"];

        if (base_ctor_called)
        {
          auto class_node = json_utils::find_class(ast_json["body"], func_name);
          func_name = class_node["bases"][0]["id"].get<std::string>();
          base_ctor_called = false;
        }

        update_instance_from_self(
          func_name, func_name, lhs_symbol->id.as_string());
      }
      // op0() refers to the left-hand side (lhs) of the function call
      rhs.op0() = lhs;
      target_block.copy_to_operands(rhs);
      return;
    }

    adjust_statement_types(lhs, rhs);

    code_assignt code_assign(lhs, rhs);
    code_assign.location() = location_begin;
    target_block.copy_to_operands(code_assign);
  }
  else
  {
    lhs_symbol->value = gen_zero(current_element_type, true);
    lhs_symbol->value.zero_initializer(true);

    code_declt decl(symbol_expr(*lhs_symbol));
    decl.location() = location_begin;
    target_block.copy_to_operands(decl);
  }

  ref_instance = nullptr;
}

void python_converter::get_compound_assign(
  const nlohmann::json &ast_node,
  codet &target_block)
{
  // Get type from declaration node
  std::string var_name = ast_node["target"]["id"].get<std::string>();
  nlohmann::json ref = find_var_decl(var_name, ast_json);
  assert(!ref.empty());
  current_element_type = get_typet(ref["annotation"]["id"].get<std::string>());

  exprt lhs = get_expr(ast_node["target"]);
  exprt rhs = get_binary_operator_expr(ast_node);

  code_assignt code_assign(lhs, rhs);
  code_assign.location() = get_location_from_decl(ast_node);

  target_block.copy_to_operands(code_assign);
}

static codet convert_expression_to_code(exprt &expr)
{
  if (expr.is_code())
    return static_cast<codet &>(expr);

  codet code("expression");
  code.location() = expr.location();
  code.move_to_operands(expr);
  return code;
}

exprt python_converter::get_conditional_stm(const nlohmann::json &ast_node)
{
  // Copy current type
  typet t = current_element_type;
  // Change to boolean before extracting condition
  current_element_type = bool_type();

  // Extract condition from AST
  exprt cond = get_expr(ast_node["test"]);
  cond.location() = get_location_from_decl(ast_node["test"]);

  // Recover type
  current_element_type = t;
  // Extract 'then' block from AST
  exprt then;
  if (ast_node["body"].is_array())
    then = get_block(ast_node["body"]);
  else
    then = get_expr(ast_node["body"]);

  locationt location = get_location_from_decl(ast_node);
  then.location() = location;

  // Extract 'else' block from AST
  exprt else_expr;
  if (ast_node.contains("orelse") && !ast_node["orelse"].empty())
  {
    // Append 'else' block to the statement
    if (ast_node["orelse"].is_array())
      else_expr = get_block(ast_node["orelse"]);
    else
      else_expr = get_expr(ast_node["orelse"]);
  }

  auto type = ast_node["_type"];

  // ternary operator
  if (type == "IfExp")
  {
    exprt if_expr("if", current_element_type);
    if_expr.copy_to_operands(cond, then, else_expr);
    return if_expr;
  }

  // Create if or while code
  codet code;
  if (type == "If")
    code.set_statement("ifthenelse");
  else if (type == "While")
    code.set_statement("while");

  // Append "then" block
  code.copy_to_operands(cond, then);
  if (!else_expr.id_string().empty())
    code.copy_to_operands(else_expr);

  return code;
}

void python_converter::get_function_definition(
  const nlohmann::json &function_node)
{
  // Function return type
  code_typet type;
  nlohmann::json return_node = function_node["returns"];
  if (return_node.contains("id"))
  {
    type.return_type() = get_typet(return_node["id"].get<std::string>());
  }
  else if (
    return_node.is_null() ||
    (return_node.contains("value") && return_node["value"].is_null()))
  {
    type.return_type() = empty_typet();
  }
  else
  {
    log_error("Return type undefined\n");
    abort();
  }

  // Copy caller function name
  const std::string caller_func_name = current_func_name;

  // Function location
  locationt location = get_location_from_decl(function_node);

  current_element_type = type.return_type();
  current_func_name = function_node["name"].get<std::string>();

  // __init__() is renamed to Classname()
  if (current_func_name == "__init__")
  {
    current_func_name = current_class_name;
    typet ctor_type("constructor");
    type.return_type() = ctor_type;
  }

  std::string id = create_symbol_id();
  std::string module_name =
    python_filename.substr(0, python_filename.find_last_of("."));

  // Iterate over function arguments
  for (const nlohmann::json &element : function_node["args"]["args"])
  {
    // Argument name
    std::string arg_name = element["arg"].get<std::string>();
    // Argument type
    typet arg_type;
    if (arg_name == "self")
      arg_type = gen_pointer_type(get_typet(current_class_name));
    else if (arg_name == "cls")
      arg_type = pointer_typet(empty_typet());
    else
      arg_type = get_typet(element["annotation"]["id"].get<std::string>());

    if (arg_type.is_array())
      arg_type = gen_pointer_type(arg_type.subtype());

    assert(arg_type != typet());

    code_typet::argumentt arg;
    arg.type() = arg_type;

    arg.cmt_base_name(arg_name);

    // Argument id
    std::string arg_id = id + "@" + arg_name;
    arg.cmt_identifier(arg_id);

    // Location
    locationt location = get_location_from_decl(element);
    arg.location() = location;

    // Push arg
    type.arguments().push_back(arg);

    // Create and add symbol to context
    symbolt param_symbol = create_symbol(
      location.get_file().as_string(), arg_name, arg_id, location, arg_type);
    param_symbol.lvalue = true;
    param_symbol.is_parameter = true;
    param_symbol.file_local = true;
    param_symbol.static_lifetime = false;
    param_symbol.is_extern = false;
    context.add(param_symbol);
  }

  // Create symbol
  symbolt symbol =
    create_symbol(module_name, current_func_name, id, location, type);
  symbol.lvalue = true;
  symbol.is_extern = false;
  symbol.file_local = false;

  symbolt *added_symbol = context.move_symbol_to_context(symbol);

  // Function body
  exprt function_body = get_block(function_node["body"]);
  added_symbol->value = function_body;

  // Restore caller function name
  current_func_name = caller_func_name;
}

void python_converter::get_attributes_from_self(
  const nlohmann::json &method_body,
  struct_typet &clazz)
{
  for (const auto &stmt : method_body)
  {
    if (
      stmt["_type"] == "AnnAssign" && stmt["target"]["_type"] == "Attribute" &&
      stmt["target"]["value"]["id"] == "self")
    {
      std::string attr_name = stmt["target"]["attr"];
      typet type = get_typet(stmt["annotation"]["id"].get<std::string>());
      struct_typet::componentt comp =
        build_component(current_class_name, attr_name, type);

      auto &class_components = clazz.components();
      if (
        std::find(class_components.begin(), class_components.end(), comp) ==
        class_components.end())
        class_components.push_back(comp);
    }
  }
}

void python_converter::get_class_definition(
  const nlohmann::json &class_node,
  codet &target_block)
{
  struct_typet clazz;
  current_class_name = class_node["name"].get<std::string>();
  clazz.tag(current_class_name);
  std::string id = "tag-" + current_class_name;

  if (context.find_symbol(id) != nullptr)
    return;

  locationt location_begin = get_location_from_decl(class_node);
  std::string module_name = location_begin.get_file().as_string();

  // Add class to symbol table
  symbolt symbol =
    create_symbol(module_name, current_class_name, id, location_begin, clazz);
  symbol.is_type = true;

  symbolt *added_symbol = context.move_symbol_to_context(symbol);

  // Iterate over base classes
  for (auto &base_class : class_node["bases"])
  {
    const std::string &base_class_name = base_class["id"].get<std::string>();
    // Get class definition from symbols table
    symbolt *class_symbol = context.find_symbol("tag-" + base_class_name);
    if (!class_symbol)
    {
      log_error("Base class not found: {}\n", base_class_name);
      abort();
    }
    struct_typet &class_type = static_cast<struct_typet &>(class_symbol->type);
    for (const auto &component : class_type.components())
      clazz.components().emplace_back(component);
  }

  // Iterate over class members
  for (auto &class_member : class_node["body"])
  {
    // Process methods
    if (class_member["_type"] == "FunctionDef")
    {
      get_attributes_from_self(class_member["body"], clazz);
      added_symbol->type = clazz;

      std::string method_name = class_member["name"].get<std::string>();
      if (method_name == "__init__")
        method_name = current_class_name;

      current_func_name = method_name;
      get_function_definition(class_member);

      exprt added_method =
        symbol_expr(*context.find_symbol(create_symbol_id()));
      struct_typet::componentt method(added_method.name(), added_method.type());
      clazz.methods().push_back(method);
      current_func_name.clear();
    }
    // Process class attributes
    else if (class_member["_type"] == "AnnAssign")
    {
      get_var_assign(class_member, target_block);
      symbolt *class_attr_symbol = context.find_symbol(
        create_symbol_id() + "@" +
        class_member["target"]["id"].get<std::string>());
      if (!class_attr_symbol)
        abort();
      class_attr_symbol->static_lifetime = true;
    }
  }
  added_symbol->type = clazz;
  current_class_name.clear();
}

void python_converter::get_return_statements(
  const nlohmann::json &ast_node,
  codet &target_block)
{
  code_returnt return_code;
  return_code.return_value() = get_expr(ast_node["value"]);
  return_code.location() = get_location_from_decl(ast_node);
  target_block.copy_to_operands(return_code);
}

exprt python_converter::get_block(const nlohmann::json &ast_block)
{
  code_blockt block;

  // Iterate over block statements
  for (auto &element : ast_block)
  {
    StatementType type = get_statement_type(element);

    switch (type)
    {
    case StatementType::VARIABLE_ASSIGN:
    {
      // Add an assignment to the block
      get_var_assign(element, block);
      break;
    }
    case StatementType::IF_STATEMENT:
    case StatementType::WHILE_STATEMENT:
    {
      exprt cond = get_conditional_stm(element);
      block.copy_to_operands(cond);
      break;
    }
    case StatementType::COMPOUND_ASSIGN:
    {
      get_compound_assign(element, block);
      break;
    }
    case StatementType::FUNC_DEFINITION:
    {
      get_function_definition(element);
      break;
    }
    case StatementType::RETURN:
    {
      get_return_statements(element, block);
      break;
    }
    case StatementType::ASSERT:
    {
      current_element_type = bool_type();
      exprt test = get_expr(element["test"]);
      code_assertt assert_code;
      assert_code.assertion() = test;
      block.move_to_operands(assert_code);
      break;
    }
    case StatementType::EXPR:
    {
      // Function calls are handled here
      exprt empty;
      exprt expr = get_expr(element["value"]);
      if (expr != empty)
        block.move_to_operands(expr);

      break;
    }
    case StatementType::CLASS_DEFINITION:
    {
      get_class_definition(element, block);
      break;
    }
    case StatementType::BREAK:
    {
      code_breakt break_expr;
      block.move_to_operands(break_expr);
      break;
    }
    case StatementType::CONTINUE:
    {
      code_continuet continue_expr;
      block.move_to_operands(continue_expr);
      break;
    }
    /* "https://docs.python.org/3/tutorial/controlflow.html: "The pass statement does nothing.
     *  It can be used when a statement is required syntactically but the program requires no action." */
    case StatementType::PASS:
      break;
    // Imports are handled by parser.py so we can just ignore here.
    case StatementType::IMPORT:
      break;
    case StatementType::UNKNOWN:
    default:
      log_error(
        "Unsupported statement: {}", element["_type"].get<std::string>());
      abort();
    }
  }

  return block;
}

python_converter::python_converter(
  contextt &_context,
  const nlohmann::json &ast)
  : context(_context),
    ns(_context),
    ast_json(ast),
    current_func_name(""),
    current_class_name(""),
    ref_instance(nullptr)
{
}

bool python_converter::convert()
{
  code_typet main_type;
  main_type.return_type() = empty_typet();

  symbolt main_symbol;
  main_symbol.id = "__ESBMC_main";
  main_symbol.name = "__ESBMC_main";
  main_symbol.type.swap(main_type);
  main_symbol.lvalue = true;
  main_symbol.is_extern = false;
  main_symbol.file_local = false;

  python_filename = ast_json["filename"].get<std::string>();

  if (!config.options.get_bool_option("no-library"))
  {
    // Load operational models -----
    const std::string &ast_output_dir =
      ast_json["ast_output_dir"].get<std::string>();
    std::list<std::string> model_files = {"range", "int"};

    for (const auto &file : model_files)
    {
      log_progress("Loading model: {}", file + ".py");

      std::stringstream model_path;
      model_path << ast_output_dir << "/" << file << ".json";

      std::ifstream model_file(model_path.str());
      nlohmann::json model_json;
      model_file >> model_json;
      model_file.close();

      exprt model_code = get_block(model_json["body"]);
      convert_expression_to_code(model_code);

      // Add imported code to main symbol
      main_symbol.value.swap(model_code);
    }
  }

  // Handle --function option
  const std::string function = config.options.get_option("function");
  if (!function.empty())
  {
    /* If the user passes --function, we add only a call to the
     * respective function in __ESBMC_main instead of entire Python program
     */

    nlohmann::json function_node;
    // Find function node in AST
    for (const auto &element : ast_json["body"])
    {
      if (element["_type"] == "FunctionDef" && element["name"] == function)
      {
        function_node = element;
        break;
      }
    }

    if (function_node.empty())
    {
      log_error("Function \"{}\" not found\n", function);
      return true;
    }

    // Convert all variables from global scope and class definitions
    code_blockt block;
    for (const auto &elem : ast_json["body"])
    {
      StatementType type = get_statement_type(elem);
      if (type == StatementType::VARIABLE_ASSIGN)
        get_var_assign(elem, block);
      else if (type == StatementType::CLASS_DEFINITION)
        get_class_definition(elem, block);
    }

    // Convert a single function
    get_function_definition(function_node);

    // Get function symbol
    std::string symbol_id = create_symbol_id() + "@F@" + function;
    symbolt *symbol = context.find_symbol(symbol_id);
    if (!symbol)
    {
      log_error("Symbol \"{}\" not found\n", symbol_id.c_str());
      return true;
    }

    // Create function call
    code_function_callt call;
    call.location() = symbol->location;
    call.function() = symbol_expr(*symbol);

    const code_typet::argumentst &arguments =
      to_code_type(symbol->type).arguments();

    // Function args are nondet values
    for (const code_typet::argumentt &arg : arguments)
    {
      exprt arg_value = exprt("sideeffect", arg.type());
      arg_value.statement("nondet");
      call.arguments().push_back(arg_value);
    }

    convert_expression_to_code(call);
    main_symbol.value.swap(call);
  }
  else
  {
    // Convert imported modules
    for (const auto &elem : ast_json["body"])
    {
      if (elem["_type"] == "ImportFrom" || elem["_type"] == "Import")
      {
        const std::string &module_name = (elem["_type"] == "ImportFrom")
                                           ? elem["module"]
                                           : elem["names"][0]["name"];
        std::stringstream module_path;
        module_path << ast_json["ast_output_dir"].get<std::string>() << "/"
                    << module_name << ".json";
        std::ifstream imported_file(module_path.str());
        nlohmann::json imported_module_json;
        imported_file >> imported_module_json;

        python_filename = imported_module_json["filename"].get<std::string>();
        imported_modules.emplace(module_name, python_filename);

        exprt imported_code = get_block(imported_module_json["body"]);
        convert_expression_to_code(imported_code);

        // Add imported code to main symbol
        main_symbol.value.swap(imported_code);
      }
    }

    // Convert main statements
    exprt main_block = get_block(ast_json["body"]);
    codet main_code = convert_expression_to_code(main_block);

    if (main_symbol.value.is_code())
      main_symbol.value.copy_to_operands(main_code);
    else
      main_symbol.value.swap(main_code);
  }

  if (context.move(main_symbol))
  {
    log_error("main already defined by another language module");
    return true;
  }

  return false;
}
