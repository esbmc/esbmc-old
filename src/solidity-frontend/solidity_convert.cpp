#include <solidity-frontend/solidity_convert.h>
#include <solidity-frontend/typecast.h>
#include <util/arith_tools.h>
#include <util/bitvector.h>
#include <util/c_types.h>
#include <util/expr_util.h>
#include <util/i2string.h>
#include <util/mp_arith.h>
#include <util/std_expr.h>
#include <util/message.h>
#include <regex>

#include <fstream>

solidity_convertert::solidity_convertert(
  contextt &_context,
  nlohmann::json &_ast_json,
  const std::string &_sol_func,
  const std::string &_contract_path)
  : context(_context),
    ns(context),
    src_ast_json(_ast_json),
    sol_func(_sol_func),
    contract_path(_contract_path),
    global_scope_id(0),
    current_scope_var_num(1),
    current_functionDecl(nullptr),
    current_forStmt(nullptr),
    current_functionName(""),
    current_contractName(""),
    scope_map({}),
    tgt_func(config.options.get_option("function")),
    tgt_cnt(config.options.get_option("contract"))
{
  std::ifstream in(_contract_path);
  contract_contents.assign(
    (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool solidity_convertert::convert()
{
  // This function consists of two parts:
  //  1. First, we perform pattern-based verificaiton
  //  2. Then we populate the context with symbols annotated based on the each AST node, and hence prepare for the GOTO conversion.

  if (!src_ast_json.contains(
        "nodes")) // check json file contains AST nodes as Solidity might change
    assert(!"JSON file does not contain any AST nodes");

  if (
    !src_ast_json.contains(
      "absolutePath")) // check json file contains AST nodes as Solidity might change
    assert(!"JSON file does not contain absolutePath");

  absolute_path = src_ast_json["absolutePath"].get<std::string>();

  // By now the context should have the symbols of all ESBMC's intrinsics and the dummy main
  // We need to convert Solidity AST nodes to the equivalent symbols and add them to the context
  nlohmann::json &nodes = src_ast_json["nodes"];

  bool found_contract_def = false;
  size_t index = 0;
  for (nlohmann::json::iterator itr = nodes.begin(); itr != nodes.end();
       ++itr, ++index)
  {
    // ignore the meta information and locate nodes in ContractDefinition
    std::string node_type = (*itr)["nodeType"].get<std::string>();
    if (node_type == "ContractDefinition") // contains AST nodes we need
    {
      global_scope_id = (*itr)["id"];
      found_contract_def = true;

      assert(itr->contains("nodes"));
      auto pattern_check =
        std::make_unique<pattern_checker>((*itr)["nodes"], sol_func);
      if (pattern_check->do_pattern_check())
        return true; // 'true' indicates something goes wrong.
    }
  }
  assert(found_contract_def && "No contracts were found in the program.");

  // reasoning-based verification

  // populate exportedSymbolsList
  // e..g
  //  "exportedSymbols": {
  //       "Base": [      --> Contract Name
  //           8
  //       ],
  //       "tt": [        --> Error Name
  //           7
  //       ]
  //   }
  for (const auto &itr : src_ast_json["exportedSymbols"].items())
  {
    //! Assume it has only one id
    int c_id = itr.value()[0].get<int>();
    std::string c_name = itr.key();
    exportedSymbolsList.insert(std::pair<int, std::string>(c_id, c_name));
  }

  // first round: handle definitions that can be outside of the contract
  // including struct, enum, interface, event, error, library...
  // noted that some can also be inside the contract, e.g. struct, enum...
  index = 0;
  for (nlohmann::json::iterator itr = nodes.begin(); itr != nodes.end();
       ++itr, ++index)
  {
    if (get_noncontract_defition(*itr))
      return true;
  }

  // second round: populate linearizedBaseList
  // this is to obtain the contract name list
  index = 0;
  for (nlohmann::json::iterator itr = nodes.begin(); itr != nodes.end();
       ++itr, ++index)
  {
    std::string node_type = (*itr)["nodeType"].get<std::string>();

    if (node_type == "ContractDefinition") // rule source-unit
    {
      current_contractName = (*itr)["name"].get<std::string>();

      // poplulate linearizedBaseList
      // this is esstinally the calling order of the constructor
      for (const auto &id : (*itr)["linearizedBaseContracts"].items())
        linearizedBaseList[current_contractName].push_back(
          id.value().get<int>());
      assert(!linearizedBaseList[current_contractName].empty());
    }
  }

  // third round: handle contract definition
  // single contract verification: where the option "--contract" is set.
  // multiple contracts verification: essentially verify the whole file.
  index = 0;
  for (nlohmann::json::iterator itr = nodes.begin(); itr != nodes.end();
       ++itr, ++index)
  {
    std::string node_type = (*itr)["nodeType"].get<std::string>();

    if (node_type == "ContractDefinition") // rule source-unit
    {
      current_contractName = (*itr)["name"].get<std::string>();

      nlohmann::json &ast_nodes = (*itr)["nodes"];
      for (nlohmann::json::iterator ittr = ast_nodes.begin();
           ittr != ast_nodes.end();
           ++ittr)
      {
        if (get_noncontract_defition(*ittr))
          return true;
      }

      // add a struct symbol for each contract
      // e.g. contract Base => struct Base
      if (get_struct_class(*itr))
        return true;

      if (convert_ast_nodes(*itr))
        return true; // 'true' indicates something goes wrong.

      // add implicit construcor function
      if (add_implicit_constructor())
        return true;

      // handling mapping_init
      if (move_mapping_to_ctor())
        return true;
    }

    // reset
    current_contractName = "";
    current_functionName = "";
    current_functionDecl = nullptr;
    current_forStmt = nullptr;
    global_scope_id = 0;
    map_init_block.clear();
  }

  // Do Verification
  // single contract
  if (!tgt_cnt.empty() && tgt_func.empty())
  {
    // perform multi-transaction verification
    // by adding symbols to the "sol_main()" entry function
    if (multi_transaction_verification(tgt_cnt))
      return true;
  }
  // multiple contract
  if (tgt_func.empty() && tgt_cnt.empty())
  {
    if (multi_contract_verification())
      return true;
  }

  return false; // 'false' indicates successful completion.
}

bool solidity_convertert::convert_ast_nodes(const nlohmann::json &contract_def)
{
  size_t index = 0;
  nlohmann::json ast_nodes = contract_def["nodes"];
  for (nlohmann::json::iterator itr = ast_nodes.begin(); itr != ast_nodes.end();
       ++itr, ++index)
  {
    nlohmann::json ast_node = *itr;
    std::string node_name = ast_node["name"].get<std::string>();
    std::string node_type = ast_node["nodeType"].get<std::string>();
    log_debug(
      "solidity",
      "@@ Converting node[{}]: name={}, nodeType={} ...",
      index,
      node_name.c_str(),
      node_type.c_str());

    // we first handle non-functional declaration,
    // due to that the vars/struct might be mentioned in the constructor
    exprt dummy_decl;
    if (get_non_function_decl(ast_node, dummy_decl))
      return true;

    // then we handle function definition
    if (get_function_decl(ast_node))
      return true;
  }

  // After converting all AST nodes, current_functionDecl should be restored to nullptr.
  assert(current_functionDecl == nullptr);

  return false;
}

bool solidity_convertert::get_non_function_decl(
  const nlohmann::json &ast_node,
  exprt &new_expr)
{
  new_expr = code_skipt();

  if (!ast_node.contains("nodeType"))
    assert(!"Missing \'nodeType\' filed in ast_node");

  SolidityGrammar::ContractBodyElementT type =
    SolidityGrammar::get_contract_body_element_t(ast_node);

  // based on each element as in Solidty grammar "rule contract-body-element"
  switch (type)
  {
  case SolidityGrammar::ContractBodyElementT::VarDecl:
  {
    return get_var_decl(ast_node, new_expr); // rule state-variable-declaration
  }
  case SolidityGrammar::ContractBodyElementT::StructDef:
  {
    return get_struct_class(ast_node); // rule enum-definition
  }
  case SolidityGrammar::ContractBodyElementT::FunctionDef:
  case SolidityGrammar::ContractBodyElementT::EnumDef:
  case SolidityGrammar::ContractBodyElementT::ErrorDef:
  case SolidityGrammar::ContractBodyElementT::EventDef:
  {
    break;
  }
  default:
  {
    log_error("Unimplemented type in rule contract-body-element");
    return true;
  }
  }
  return false;
}

bool solidity_convertert::get_function_decl(const nlohmann::json &ast_node)
{
  if (!ast_node.contains("nodeType"))
    assert(!"Missing \'nodeType\' filed in ast_node");

  SolidityGrammar::ContractBodyElementT type =
    SolidityGrammar::get_contract_body_element_t(ast_node);

  // based on each element as in Solidty grammar "rule contract-body-element"
  switch (type)
  {
  case SolidityGrammar::ContractBodyElementT::FunctionDef:
  {
    return get_function_definition(ast_node); // rule function-definition
  }
  case SolidityGrammar::ContractBodyElementT::VarDecl:
  case SolidityGrammar::ContractBodyElementT::StructDef:
  case SolidityGrammar::ContractBodyElementT::EnumDef:
  case SolidityGrammar::ContractBodyElementT::ErrorDef:
  case SolidityGrammar::ContractBodyElementT::EventDef:
  {
    break;
  }
  default:
  {
    log_error("Unimplemented type in rule contract-body-element");
    return true;
  }
  }
  return false;
}

bool solidity_convertert::get_var_decl_stmt(
  const nlohmann::json &ast_node,
  exprt &new_expr)
{
  // For rule variable-declaration-statement
  new_expr = code_skipt();

  if (!ast_node.contains("nodeType"))
    assert(!"Missing \'nodeType\' filed in ast_node");

  SolidityGrammar::VarDeclStmtT type =
    SolidityGrammar::get_var_decl_stmt_t(ast_node);
  log_debug(
    "solidity",
    "	@@@ got Variable-declaration-statement: "
    "SolidityGrammar::VarDeclStmtT::{}",
    SolidityGrammar::var_decl_statement_to_str(type));

  switch (type)
  {
  case SolidityGrammar::VarDeclStmtT::VariableDecl:
  {
    return get_var_decl(ast_node, new_expr); // rule variable-declaration
  }
  default:
  {
    assert(!"Unimplemented type in rule variable-declaration-statement");
    return true;
  }
  }

  return false;
}

// rule state-variable-declaration
// rule variable-declaration-statement
bool solidity_convertert::get_var_decl(
  const nlohmann::json &ast_node,
  exprt &new_expr)
{
  // For Solidity rule state-variable-declaration:
  // 1. populate typet
  typet t;
  // VariableDeclaration node contains both "typeName" and "typeDescriptions".
  // However, ExpressionStatement node just contains "typeDescriptions".
  // For consistensy, we use ["typeName"]["typeDescriptions"] as in state-variable-declaration
  // to improve the re-usability of get_type* function, when dealing with non-array var decls.
  // For array, do NOT use ["typeName"]. Otherwise, it will cause problem
  // when populating typet in get_cast
  bool dyn_array = is_dyn_array(ast_node);
  bool mapping = is_mapping(ast_node);
  if (dyn_array)
  {
    if (ast_node.contains("initialValue"))
    {
      // append size expr in typeDescription JSON object
      const nlohmann::json &type_descriptor =
        add_dyn_array_size_expr(ast_node["typeDescriptions"], ast_node);
      if (get_type_description(type_descriptor, t))
        return true;
    }
    else
    {
      if (get_type_description(ast_node["typeDescriptions"], t))
        return true;
    }
  }
  else if (mapping)
  {
    // the mapping should not handled in var decl, instead
    // it should be an expression inside the function.

    exprt dump;
    // 1. get the expr
    if (get_expr(ast_node, dump))
      return true;

    // 2. move it to a function.
    // Mappings cannot be created dynamically
    // which means it should not be declared inside a function
    assert(!current_functionDecl);

    // map_init_int(&m)
    map_init_block.operands().push_back(dump.op1());

    // map_int_t m
    new_expr = dump.op0().op0();
    return false;
  }
  else
  {
    if (get_type_description(ast_node["typeName"]["typeDescriptions"], t))
      return true;
  }

  bool is_state_var = ast_node["stateVariable"] == true;

  // 2. populate id and name
  std::string name, id;

  //TODO: Omitted variable
  if (ast_node["name"].get<std::string>().empty())
  {
    log_error("Omitted names are not supported.");
    return true;
  }

  if (is_state_var)
    get_state_var_decl_name(ast_node, name, id);
  else if (current_functionDecl)
  {
    assert(current_functionName != "");
    get_var_decl_name(ast_node, name, id);
  }
  else
  {
    log_error("ESBMC could not find the parent scope for this local variable");
    return true;
  }

  // 3. populate location
  locationt location_begin;
  get_location_from_decl(ast_node, location_begin);

  // 4. populate debug module name
  std::string debug_modulename =
    get_modulename_from_path(location_begin.file().as_string());

  // 5. set symbol attributes
  symbolt symbol;
  get_default_symbol(symbol, debug_modulename, t, name, id, location_begin);

  symbol.lvalue = true;
  symbol.static_lifetime = is_state_var;
  symbol.file_local = !is_state_var;
  symbol.is_extern = false;

  // For state var decl, we look for "value".
  // For local var decl, we look for "initialValue"
  bool has_init =
    (ast_node.contains("value") || ast_node.contains("initialValue"));
  if (symbol.static_lifetime && !symbol.is_extern && !has_init)
  {
    // set default value as zero
    symbol.value = gen_zero(t, true);
    symbol.value.zero_initializer(true);
  }

  // 6. add symbol into the context
  // just like clang-c-frontend, we have to add the symbol before converting the initial assignment
  symbolt &added_symbol = *move_symbol_to_context(symbol);

  // 7. populate init value if there is any
  code_declt decl(symbol_expr(added_symbol));

  if (has_init)
  {
    nlohmann::json init_value =
      is_state_var ? ast_node["value"] : ast_node["initialValue"];
    nlohmann::json literal_type = ast_node["typeDescriptions"];

    assert(literal_type != nullptr);
    exprt val;
    if (get_expr(init_value, literal_type, val))
      return true;

    solidity_gen_typecast(ns, val, t);

    added_symbol.value = val;
    decl.operands().push_back(val);
  }

  // special handle for contract type
  // e.g.
  //  Base x ==> Base x = new Base();
  else if (
    SolidityGrammar::get_type_name_t(
      ast_node["typeName"]["typeDescriptions"]) ==
    SolidityGrammar::ContractTypeName)
  {
    // 1. get constract name
    assert(
      ast_node["typeName"]["nodeType"].get<std::string>() ==
      "UserDefinedTypeName");
    const std::string contract_name =
      ast_node["typeName"]["pathNode"]["name"].get<std::string>();

    // 2. since the contract type variable has no initial value, i.e. explicit constructor call,
    // we construct an implicit constructor expression
    exprt val;
    if (get_implicit_ctor_ref(val, contract_name))
      return true;

    // 3. make it to a temporary object
    side_effect_exprt tmp_obj("temporary_object", val.type());
    codet code_expr("expression");
    code_expr.operands().push_back(val);
    tmp_obj.initializer(code_expr);
    tmp_obj.location() = val.location();
    val.swap(tmp_obj);

    // 4. generate typecast for Solidity contract
    solidity_gen_typecast(ns, val, t);

    // 5. add constructor call to declaration operands
    added_symbol.value = val;
    decl.operands().push_back(val);
  }

  decl.location() = location_begin;
  new_expr = decl;

  return false;
}

// This function handles both contract and struct
// The contract can be regarded as the class in C++, converting to a struct
bool solidity_convertert::get_struct_class(const nlohmann::json &struct_def)
{
  // 1. populate name, id
  std::string id, name;
  struct_typet t = struct_typet();

  if (struct_def["nodeType"].get<std::string>() == "ContractDefinition")
  {
    name = struct_def["name"].get<std::string>();
    id = prefix + name;
    t.tag(name);
  }
  else if (struct_def["nodeType"].get<std::string>() == "StructDefinition")
  {
    // ""tag-struct Struct_Name"
    name = struct_def["name"].get<std::string>();
    id = prefix + "struct " + struct_def["canonicalName"].get<std::string>();
    t.tag("struct " + name);
  }
  else
  {
    log_error(
      "Got nodeType={}. Unsupported struct type",
      struct_def["nodeType"].get<std::string>());
    return true;
  }

  // 2. Check if the symbol is already added to the context, do nothing if it is
  // already in the context.
  if (context.find_symbol(id) != nullptr)
    return false;

  // 3. populate location
  locationt location_begin;
  get_location_from_decl(struct_def, location_begin);

  // 4. populate debug module name
  std::string debug_modulename =
    get_modulename_from_path(location_begin.file().as_string());
  current_fileName = debug_modulename;

  symbolt symbol;
  get_default_symbol(symbol, debug_modulename, t, name, id, location_begin);

  symbol.is_type = true;
  symbolt &added_symbol = *move_symbol_to_context(symbol);

  // populate the scope_map
  // this map is used to find reference when there is no decl_ref_id provided in the nodes
  // or replace the find_decl_ref in order to speed up
  int scp = struct_def["id"].get<int>();
  scope_map.insert(std::pair<int, std::string>(scp, name));

  // 5. populate fields(state var) and method(function)
  // We have to add fields before methods as the fields are likely to be used
  // in the methods
  nlohmann::json ast_nodes;
  if (struct_def.contains("nodes"))
    ast_nodes = struct_def["nodes"];
  else if (struct_def.contains("members"))
    ast_nodes = struct_def["members"];
  else
  {
    // Defining empty structs is disallowed.
    // Contracts can be empty
    log_warning("Empty contract.");
  }

  for (nlohmann::json::iterator itr = ast_nodes.begin(); itr != ast_nodes.end();
       ++itr)
  {
    SolidityGrammar::ContractBodyElementT type =
      SolidityGrammar::get_contract_body_element_t(*itr);

    switch (type)
    {
    case SolidityGrammar::ContractBodyElementT::VarDecl:
    {
      // this can be both state and non-state variable
      if (get_struct_class_fields(*itr, t))
        return true;
      break;
    }
    case SolidityGrammar::ContractBodyElementT::FunctionDef:
    {
      if (get_struct_class_method(*itr, t))
        return true;
      break;
    }
    case SolidityGrammar::ContractBodyElementT::StructDef:
    case SolidityGrammar::ContractBodyElementT::EnumDef:
    case SolidityGrammar::ContractBodyElementT::ErrorDef:
    case SolidityGrammar::ContractBodyElementT::EventDef:
    {
      // skip
      break;
    }
    default:
    {
      log_error("Unimplemented type in rule contract-body-element");
      return true;
    }
    }
  }

  t.location() = location_begin;
  added_symbol.type = t;

  return false;
}

bool solidity_convertert::get_struct_class_fields(
  const nlohmann::json &ast_node,
  struct_typet &type)
{
  struct_typet::componentt comp;

  if (get_var_decl_ref(ast_node, comp))
    return true;

  comp.id("component");
  comp.type().set("#member_name", type.tag());

  if (get_access_from_decl(ast_node, comp))
    return true;
  type.components().push_back(comp);

  return false;
}

bool solidity_convertert::get_struct_class_method(
  const nlohmann::json &ast_node,
  struct_typet &type)
{
  struct_typet::componentt comp;
  if (get_func_decl_ref(ast_node, comp))
    return true;

  if (comp.is_code() && to_code(comp).statement() == "skip")
    return false;

  if (get_access_from_decl(ast_node, comp))
    return true;

  type.methods().push_back(comp);
  return false;
}

bool solidity_convertert::get_noncontract_defition(nlohmann::json &ast_node)
{
  std::string node_type = (ast_node)["nodeType"].get<std::string>();

  if (node_type == "StructDefinition")
  {
    if (get_struct_class(ast_node))
      return true;
  }
  else if (node_type == "EnumDefinition")
    // set the ["Value"] for each member inside enum
    add_enum_member_val(ast_node);
  else if (node_type == "ErrorDefinition")
  {
    if (get_error_definition(ast_node))
      return true;
  }
  else if (node_type == "EventDefinition")
  {
    if (get_function_definition(ast_node))
      return true;
  }

  return false;
}

void solidity_convertert::add_enum_member_val(nlohmann::json &ast_node)
{
  /*
  "nodeType": "EnumDefinition",
  "members": 
    [
      {
          "id": 2,
          "name": "SMALL",
          "nameLocation": "66:5:0",
          "nodeType": "EnumValue",
          "src": "66:5:0",
          "Value": 0 => new added object
      },
      {
          "id": 3,
          "name": "MEDIUM",
          "nameLocation": "73:6:0",
          "nodeType": "EnumValue",
          "src": "73:6:0",
          "Value": 1  => new added object
      },
    ] */

  assert(ast_node["nodeType"] == "EnumDefinition");
  int idx = 0;
  nlohmann::json &members = ast_node["members"];
  for (nlohmann::json::iterator itr = members.begin(); itr != members.end();
       ++itr, ++idx)
  {
    (*itr).push_back(
      nlohmann::json::object_t::value_type("Value", std::to_string(idx)));
  }
}

// covert the error_definition to a function
bool solidity_convertert::get_error_definition(const nlohmann::json &ast_node)
{
  // e.g.
  // error errmsg(int num1, uint num2, uint[2] addrs);
  //   to
  // function 'tag-erro errmsg@12'() { __ESBMC_assume(false);}

  const nlohmann::json *old_functionDecl = current_functionDecl;
  const std::string old_functionName = current_functionName;

  // e.g. name: errmsg; id: sol:@errmsg#12
  const int id_num = ast_node["id"].get<int>();
  std::string name, id;
  name = ast_node["name"].get<std::string>();
  id = "sol:@" + name + "#" + std::to_string(id_num);

  // update scope map
  scope_map.insert(std::pair<int, std::string>(id_num, name));

  // just to pass the internal assertions
  current_functionName = name;
  current_functionDecl = &ast_node;

  // no return value
  code_typet type;
  typet e_type = empty_typet();
  e_type.set("cpp_type", "void");
  type.return_type() = e_type;

  locationt location_begin;
  get_location_from_decl(ast_node, location_begin);
  std::string debug_modulename =
    get_modulename_from_path(location_begin.file().as_string());

  symbolt symbol;
  get_default_symbol(symbol, debug_modulename, type, name, id, location_begin);
  symbol.lvalue = true;

  symbolt &added_symbol = *move_symbol_to_context(symbol);

  // populate the params
  SolidityGrammar::ParameterListT params =
    SolidityGrammar::get_parameter_list_t(ast_node["parameters"]);
  if (params == SolidityGrammar::ParameterListT::EMPTY)
    type.make_ellipsis();
  else
  {
    for (const auto &decl : ast_node["parameters"]["parameters"].items())
    {
      const nlohmann::json &func_param_decl = decl.value();

      code_typet::argumentt param;
      if (get_function_params(func_param_decl, param))
        return true;

      type.arguments().push_back(param);
    }
  }
  added_symbol.type = type;

  // construct a "__ESBMC_assume(false)" statement
  typet return_type = bool_type();
  locationt loc;
  side_effect_expr_function_callt call;
  get_library_function_call(
    "__ESBMC_assume", "__ESBMC_assume", return_type, loc, call);

  exprt arg = false_exprt();
  call.arguments().push_back(arg);
  convert_expression_to_code(call);

  // insert it to the body
  code_blockt body;
  body.operands().push_back(call);
  added_symbol.value = body;

  // restore
  current_functionDecl = old_functionDecl;
  current_functionName = old_functionName;

  return false;
}

bool solidity_convertert::add_implicit_constructor()
{
  std::string name, id;
  name = current_contractName;

  id = get_ctor_call_id(current_contractName);

  // ctor is already in the symbol table
  if (context.find_symbol(id) != nullptr)
    return false;

  // an implicit constructor is an void empty function
  return get_default_function(name, id);
}

bool solidity_convertert::get_access_from_decl(
  const nlohmann::json &ast_node,
  struct_typet::componentt &comp)
{
  if (
    SolidityGrammar::get_access_t(ast_node) ==
    SolidityGrammar::VisibilityT::UnknownT)
    return true;

  std::string access = ast_node["visibility"].get<std::string>();
  comp.set_access(access);

  return false;
}

bool solidity_convertert::get_function_definition(
  const nlohmann::json &ast_node)
{
  // For Solidity rule function-definition:
  // Order matters! do not change!
  // 1. Check fd.isImplicit() --- skipped since it's not applicable to Solidity
  // 2. Check fd.isDefined() and fd.isThisDeclarationADefinition()
  if (
    ast_node.contains("implemented") &&
    !ast_node
      ["implemented"]) // TODO: for interface function, it's just a definition. Add something like "&& isInterface_JustDefinition()"
    return false;

  // Check intrinsic functions
  if (check_intrinsic_function(ast_node))
    return false;

  // 3. Set current_scope_var_num, current_functionDecl and old_functionDecl
  current_scope_var_num = 1;
  const nlohmann::json *old_functionDecl = current_functionDecl;
  const std::string old_functionName = current_functionName;

  current_functionDecl = &ast_node;
  bool is_ctor = false;
  if (
    (*current_functionDecl)["name"].get<std::string>() == "" &&
    (*current_functionDecl).contains("kind") &&
    (*current_functionDecl)["kind"] == "constructor")
  {
    // for construcotr
    is_ctor = true;
    if (get_current_contract_name(*current_functionDecl, current_functionName))
      return true;
  }
  else
    current_functionName = (*current_functionDecl)["name"].get<std::string>();

  // 4. Return type
  code_typet type;
  if (ast_node.contains("returnParameters"))
  {
    if (get_type_description(ast_node["returnParameters"], type.return_type()))
      return true;
  }
  else
    type.return_type() = empty_typet();

  // special handling for tuple:
  // construct a tuple type and a tuple instance
  if (type.return_type().get("#sol_type") == "TUPLE")
  {
    exprt dump;
    if (get_tuple_definition(*current_functionDecl))
      return true;
    if (get_tuple_instance(*current_functionDecl, dump))
      return true;
    type.return_type().set("#sol_tuple_id", dump.identifier().as_string());
  }

  // 5. Check fd.isVariadic(), fd.isInlined()
  //  Skipped since Solidity does not support variadic (optional args) or inline function.
  //  Actually "inline" doesn not make sense in Solidity

  // 6. Populate "locationt location_begin"
  locationt location_begin;
  get_location_from_decl(ast_node, location_begin);

  // 7. Populate "std::string id, name"
  std::string name, id;
  get_function_definition_name(ast_node, name, id);

  if (name == "func_dynamic")
    printf("@@ found func_dynamic\n");

  // 8. populate "std::string debug_modulename"
  std::string debug_modulename =
    get_modulename_from_path(location_begin.file().as_string());

  // 9. Populate "symbol.static_lifetime", "symbol.is_extern" and "symbol.file_local"
  symbolt symbol;
  get_default_symbol(symbol, debug_modulename, type, name, id, location_begin);

  symbol.lvalue = true;
  symbol.is_extern =
    false; // TODO: hard coded for now, may need to change later
  symbol.file_local = false;

  // 10. Add symbol into the context
  symbolt &added_symbol = *move_symbol_to_context(symbol);

  // 11. Convert parameters, if no parameter, assume ellipis
  //  - Convert params before body as they may get referred by the statement in the body
  if (is_ctor)
  {
    /* need (type *) as first parameter, this is equivalent to the 'this'
     * pointer in C++ */
    code_typet::argumentt param(pointer_typet(type.return_type()));
    type.arguments().push_back(param);
  }

  SolidityGrammar::ParameterListT params =
    SolidityGrammar::get_parameter_list_t(ast_node["parameters"]);
  if (params != SolidityGrammar::ParameterListT::EMPTY)
  {
    // convert parameters if the function has them
    // update the typet, since typet contains parameter annotations
    for (const auto &decl : ast_node["parameters"]["parameters"].items())
    {
      const nlohmann::json &func_param_decl = decl.value();

      code_typet::argumentt param;
      if (get_function_params(func_param_decl, param))
        return true;

      type.arguments().push_back(param);
    }
  }

  if (type.arguments().empty())
  {
    // assume ellipsis if the function has no parameters
    type.make_ellipsis();
  }

  added_symbol.type = type;

  // 12. Convert body and embed the body into the same symbol
  if (ast_node.contains("body"))
  {
    exprt body_exprt;
    if (get_block(ast_node["body"], body_exprt))
      return true;

    added_symbol.value = body_exprt;
  }
  else
  {
    //! assume it's event-definition
    if (ast_node["nodeType"] != "EventDefinition")
    {
      log_error(
        "Expect nodeType=EventDefinition, got {}",
        ast_node["nodeType"].get<std::string>());
      return true;
    }

    // construct an empty body
    code_blockt _block;
    added_symbol.value = _block;
  }

  //assert(!"done - finished all expr stmt in function?");

  // 13. Restore current_functionDecl
  current_functionDecl =
    old_functionDecl; // for __ESBMC_assume, old_functionDecl == null
  current_functionName = old_functionName;

  return false;
}

bool solidity_convertert::get_function_params(
  const nlohmann::json &pd,
  exprt &param)
{
  // 1. get parameter type
  typet param_type;
  if (get_type_description(pd["typeDescriptions"], param_type))
    return true;

  // 2a. get id and name
  std::string id, name;
  assert(current_functionName != ""); // we are converting a function param now
  assert(current_functionDecl);
  get_var_decl_name(pd, name, id);

  // 2b. handle Omitted Names in Function Definitions
  if (name == "")
  {
    // Items with omitted names will still be present on the stack, but they are inaccessible by name.
    // e.g. ~omitted1, ~omitted2. which is a invalid name for solidity.
    // Therefore it won't conflict with other arg names.
    //log_error("Omitted params are not supported");
    // return true;
    ;
  }

  param = code_typet::argumentt();
  param.type() = param_type;
  param.cmt_base_name(name);

  // 3. get location
  locationt location_begin;
  get_location_from_decl(pd, location_begin);

  param.cmt_identifier(id);
  param.location() = location_begin;

  // 4. get symbol
  std::string debug_modulename =
    get_modulename_from_path(location_begin.file().as_string());
  symbolt param_symbol;
  get_default_symbol(
    param_symbol, debug_modulename, param_type, name, id, location_begin);

  // 5. set symbol's lvalue, is_parameter and file local
  param_symbol.lvalue = true;
  param_symbol.is_parameter = true;
  param_symbol.file_local = true;

  // 6. add symbol to the context
  move_symbol_to_context(param_symbol);

  return false;
}

bool solidity_convertert::get_block(
  const nlohmann::json &block,
  exprt &new_expr)
{
  // For rule block
  locationt location;
  get_start_location_from_stmt(block, location);

  SolidityGrammar::BlockT type = SolidityGrammar::get_block_t(block);
  log_debug(
    "solidity",
    "	@@@ got Block: SolidityGrammar::BlockT::{}",
    SolidityGrammar::block_to_str(type));

  switch (type)
  {
  // equivalent to clang::Stmt::CompoundStmtClass
  // deal with a block of statements
  case SolidityGrammar::BlockT::Statement:
  {
    const nlohmann::json &stmts = block["statements"];

    code_blockt _block;
    unsigned ctr = 0;
    // items() returns a key-value pair with key being the index
    for (auto const &stmt_kv : stmts.items())
    {
      exprt statement;
      if (get_statement(stmt_kv.value(), statement))
        return true;

      convert_expression_to_code(statement);
      _block.operands().push_back(statement);
      ++ctr;
    }
    log_debug("solidity", " \t@@@ CompoundStmt has {} statements", ctr);

    locationt location_end;
    get_final_location_from_stmt(block, location_end);

    _block.end_location(location_end);
    new_expr = _block;
    break;
  }
  case SolidityGrammar::BlockT::BlockForStatement:
  case SolidityGrammar::BlockT::BlockIfStatement:
  case SolidityGrammar::BlockT::BlockWhileStatement:
  {
    // this means only one statement in the block
    exprt statement;

    // pass directly to get_statement()
    if (get_statement(block, statement))
      return true;
    convert_expression_to_code(statement);
    new_expr = statement;
    break;
  }
  case SolidityGrammar::BlockT::BlockExpressionStatement:
  {
    get_expr(block["expression"], new_expr);
    break;
  }
  default:
  {
    assert(!"Unimplemented type in rule block");
    return true;
  }
  }

  new_expr.location() = location;
  return false;
}

bool solidity_convertert::get_statement(
  const nlohmann::json &stmt,
  exprt &new_expr)
{
  // For rule statement
  // Since this is an additional layer of grammar rules compared to clang C, we do NOT set location here.
  // Just pass the new_expr reference to the next layer.

  SolidityGrammar::StatementT type = SolidityGrammar::get_statement_t(stmt);
  log_debug(
    "solidity",
    "	@@@ got Stmt: SolidityGrammar::StatementT::{}",
    SolidityGrammar::statement_to_str(type));

  switch (type)
  {
  case SolidityGrammar::StatementT::Block:
  {
    if (get_block(stmt, new_expr))
      return true;
    break;
  }
  case SolidityGrammar::StatementT::ExpressionStatement:
  {
    if (get_expr(stmt["expression"], new_expr))
      return true;
    break;
  }
  case SolidityGrammar::StatementT::VariableDeclStatement:
  {
    const nlohmann::json &declgroup = stmt["declarations"];

    codet decls("decl-block");
    unsigned ctr = 0;
    // N.B. Although Solidity AST JSON uses "declarations": [],
    // the size of this array is alway 1!
    // A second declaration will become another stmt in "statements" array
    // e.g. "statements" : [
    //  {"declarations": [], "id": 1}
    //  {"declarations": [], "id": 2}
    //  {"declarations": [], "id": 3}
    // ]
    for (const auto &it : declgroup.items())
    {
      // deal with local var decl with init value
      nlohmann::json decl = it.value();
      if (stmt.contains("initialValue"))
      {
        // Need to combine init value with the decl JSON object
        decl["initialValue"] = stmt["initialValue"];
      }

      exprt single_decl;
      if (get_var_decl_stmt(decl, single_decl))
        return true;

      decls.operands().push_back(single_decl);
      ++ctr;
    }
    log_debug("solidity", " \t@@@ DeclStmt group has {} decls", ctr);

    new_expr = decls;
    break;
  }
  case SolidityGrammar::StatementT::ReturnStatement:
  {
    if (!current_functionDecl)
    {
      log_error(
        "Error: ESBMC could not find the parent scope for this "
        "ReturnStatement");
      return true;
    }

    // 1. get return type
    // TODO: Fix me! Assumptions:
    //  a). It's "return <expr>;" not "return;"
    //  b). <expr> is pointing to a DeclRefExpr, we need to wrap it in an ImplicitCastExpr as a subexpr
    //  c). For multiple return type, the return statement represented as a tuple expression using a components field.
    //      Besides, tuple can only be declared literally. https://docs.soliditylang.org/en/latest/control-structures.html#assignment
    //      e.g. return (false, 123)
    assert(stmt.contains("expression"));
    assert(stmt["expression"].contains("nodeType"));

    // get_type_description
    typet return_exrp_type;
    if (get_type_description(
          stmt["expression"]["typeDescriptions"], return_exrp_type))
      return true;

    if (return_exrp_type.get("#sol_type") == "TUPLE")
    {
      if (
        stmt["expression"]["nodeType"].get<std::string>() !=
          "TupleExpression" &&
        stmt["expression"]["nodeType"].get<std::string>() != "FunctionCall")
      {
        log_error("Unexpected tuple");
        return true;
      }

      code_blockt _block;

      // get tuple instance
      std::string tname, tid;
      if (get_tuple_instance_name(*current_functionDecl, tname, tid))
        return true;
      if (context.find_symbol(tid) == nullptr)
        return true;

      // get lhs
      exprt lhs = symbol_expr(*context.find_symbol(tid));

      if (
        stmt["expression"]["nodeType"].get<std::string>() == "TupleExpression")
      {
        // return (x,y) ==>
        // tuple.mem0 = x; tuple.mem1 = y; return ;

        // get rhs
        exprt rhs;
        if (get_expr(stmt["expression"], rhs))
          return true;

        size_t ls = to_struct_type(lhs.type()).components().size();
        size_t rs = rhs.operands().size();
        assert(ls == rs);

        for (size_t i = 0; i < ls; i++)
        {
          // lop: struct member call (e.g. tuple.men0)
          exprt lop;
          if (get_tuple_member_call(
                lhs.identifier(),
                to_struct_type(lhs.type()).components().at(i),
                lop))
            return true;

          // rop: constant/symbol
          exprt rop = rhs.operands().at(i);

          // do assignment
          get_tuple_assignment(_block, lop, rop);
        }
      }
      else
      {
        // return func(); ==>
        // tuple1.mem0 = tuple0.mem0; return;

        // get rhs
        exprt rhs;
        if (get_tuple_function_ref(stmt["expression"]["expression"], rhs))
          return true;

        // add function call
        exprt func_call;
        if (get_expr(
              stmt["expression"],
              stmt["expression"]["typeDescriptions"],
              func_call))
          return true;
        get_tuple_function_call(_block, func_call);

        size_t ls = to_struct_type(lhs.type()).components().size();
        size_t rs = to_struct_type(rhs.type()).components().size();
        assert(ls == rs);

        for (size_t i = 0; i < ls; i++)
        {
          // lop: struct member call (e.g. tupleA.men0)
          exprt lop;
          if (get_tuple_member_call(
                lhs.identifier(),
                to_struct_type(lhs.type()).components().at(i),
                lop))
            return true;

          // rop: struct member call (e.g. tupleB.men0)
          exprt rop;
          if (get_tuple_member_call(
                rhs.identifier(),
                to_struct_type(rhs.type()).components().at(i),
                rop))
            return true;

          // do assignment
          get_tuple_assignment(_block, lop, rop);
        }
      }
      // do return in the end
      exprt return_expr = code_returnt();
      _block.move_to_operands(return_expr);

      if (_block.operands().size() == 0)
        new_expr = code_skipt();
      else
        new_expr = _block;
      break;
    }

    typet return_type;
    if ((*current_functionDecl).contains("returnParameters"))
    {
      assert(
        (*current_functionDecl)["returnParameters"]["id"]
          .get<std::uint16_t>() ==
        stmt["functionReturnParameters"].get<std::uint16_t>());
      if (get_type_description(
            (*current_functionDecl)["returnParameters"], return_type))
        return true;
    }
    else
      return true;

    nlohmann::json literal_type = nullptr;

    auto expr_type = SolidityGrammar::get_expression_t(stmt["expression"]);
    bool expr_is_literal = expr_type == SolidityGrammar::Literal;
    if (expr_is_literal)
      literal_type = make_return_type_from_typet(return_type);

    // 2. get return value
    code_returnt ret_expr;
    const nlohmann::json &rtn_expr = stmt["expression"];
    // wrap it in an ImplicitCastExpr to convert LValue to RValue
    nlohmann::json implicit_cast_expr =
      make_implicit_cast_expr(rtn_expr, "LValueToRValue");

    /* There could be case like
      {
      "expression": {
          "kind": "number",
          "nodeType": "Literal",
          "typeDescriptions": {
              "typeIdentifier": "t_rational_11_by_1",
              "typeString": "int_const 11"
          },
          "value": "12345"
      },
      "nodeType": "Return",
      }
      Therefore, we need to pass the literal_type value.
      */

    exprt val;
    if (get_expr(implicit_cast_expr, literal_type, val))
      return true;

    solidity_gen_typecast(ns, val, return_type);
    ret_expr.return_value() = val;

    new_expr = ret_expr;

    break;
  }
  case SolidityGrammar::StatementT::ForStatement:
  {
    // Based on rule for-statement

    // For nested loop
    const nlohmann::json *old_forStmt = current_forStmt;
    current_forStmt = &stmt;

    // 1. annotate init
    codet init =
      code_skipt(); // code_skipt() means no init in for-stmt, e.g. for (; i< 10; ++i)
    if (stmt.contains("initializationExpression"))
      if (get_statement(stmt["initializationExpression"], init))
        return true;

    convert_expression_to_code(init);

    // 2. annotate condition
    exprt cond = true_exprt();
    if (stmt.contains("condition"))
      if (get_expr(stmt["condition"], cond))
        return true;

    // 3. annotate increment
    codet inc = code_skipt();
    if (stmt.contains("loopExpression"))
      if (get_statement(stmt["loopExpression"], inc))
        return true;

    convert_expression_to_code(inc);

    // 4. annotate body
    codet body = code_skipt();
    if (stmt.contains("body"))
      if (get_statement(stmt["body"], body))
        return true;

    convert_expression_to_code(body);

    code_fort code_for;
    code_for.init() = init;
    code_for.cond() = cond;
    code_for.iter() = inc;
    code_for.body() = body;

    new_expr = code_for;
    current_forStmt = old_forStmt;
    break;
  }
  case SolidityGrammar::StatementT::IfStatement:
  {
    // Based on rule if-statement
    // 1. Condition: make a exprt for condition
    exprt cond;
    if (get_expr(stmt["condition"], cond))
      return true;

    // 2. Then: make a exprt for trueBody
    exprt then;
    if (get_statement(stmt["trueBody"], then))
      return true;

    convert_expression_to_code(then);

    codet if_expr("ifthenelse");
    if_expr.copy_to_operands(cond, then);

    // 3. Else: make a exprt for "falseBody" if the if-statement node contains an "else" block
    if (stmt.contains("falseBody"))
    {
      exprt else_expr;
      if (get_statement(stmt["falseBody"], else_expr))
        return true;

      convert_expression_to_code(else_expr);
      if_expr.copy_to_operands(else_expr);
    }

    new_expr = if_expr;
    break;
  }
  case SolidityGrammar::StatementT::WhileStatement:
  {
    exprt cond = true_exprt();
    if (get_expr(stmt["condition"], cond))
      return true;

    codet body = codet();
    if (get_block(stmt["body"], body))
      return true;

    convert_expression_to_code(body);

    code_whilet code_while;
    code_while.cond() = cond;
    code_while.body() = body;

    new_expr = code_while;
    break;
  }
  case SolidityGrammar::StatementT::ContinueStatement:
  {
    new_expr = code_continuet();
    break;
  }
  case SolidityGrammar::StatementT::BreakStatement:
  {
    new_expr = code_breakt();
    break;
  }
  case SolidityGrammar::StatementT::RevertStatement:
  {
    // e.g.
    // {
    //   "errorCall": {
    //     "nodeType": "FunctionCall",
    //   }
    //   "nodeType": "RevertStatement",
    // }
    if (!stmt.contains("errorCall") || get_expr(stmt["errorCall"], new_expr))
      return true;

    break;
  }
  case SolidityGrammar::StatementT::EmitStatement:
  {
    // treat emit as function call
    if (!stmt.contains("eventCall"))
    {
      log_error("Unexpected emit statement.");
      return true;
    }
    if (get_expr(stmt["eventCall"], new_expr))
      return true;

    break;
  }
  case SolidityGrammar::StatementT::StatementTError:
  default:
  {
    log_error(
      "Unimplemented Statement type in rule statement. Got {}",
      SolidityGrammar::statement_to_str(type));
    return true;
  }
  }

  return false;
}

/**
     * @brief Populate the out parameter with the expression based on
     * the solidity expression grammar
     *
     * @param expr The expression ast is to be converted to the IR
     * @param new_expr Out parameter to hold the conversion
     * @return true iff the conversion has failed
     * @return false iff the conversion was successful
     */
bool solidity_convertert::get_expr(const nlohmann::json &expr, exprt &new_expr)
{
  return get_expr(expr, nullptr, new_expr);
}

/**
     * @brief Populate the out parameter with the expression based on
     * the solidity expression grammar. 
     * 
     * More specifically, parse each expression in the AST json and
     * convert it to a exprt ("new_expr"). The expression may have sub-expression
     * 
     * !Always check if the expression is a Literal before calling get_expr
     * !Unless you are 100% sure it will not be a constant
     * 
     * This function is called throught two paths:
     * 1. get_non_function_decl => get_var_decl => get_expr
     * 2. get_non_function_decl => get_function_definition => get_statement => get_expr
     * 
     * @param expr The expression that is to be converted to the IR
     * @param literal_type Type information ast to create the the literal
     * type in the IR (only needed for when the expression is a literal).
     * A literal_type is a "typeDescriptions" ast_node.
     * we need this due to some info is missing in the child node.
     * @param new_expr Out parameter to hold the conversion
     * @return true iff the conversion has failed
     * @return false iff the conversion was successful
     */
bool solidity_convertert::get_expr(
  const nlohmann::json &expr,
  const nlohmann::json &literal_type,
  exprt &new_expr)
{
  // For rule expression
  // We need to do location settings to match clang C's number of times to set the locations when recurring

  locationt location;
  get_start_location_from_stmt(expr, location);

  SolidityGrammar::ExpressionT type = SolidityGrammar::get_expression_t(expr);
  log_debug(
    "solidity",
    " @@@ got Expr: SolidityGrammar::ExpressionT::{}",
    SolidityGrammar::expression_to_str(type));

  switch (type)
  {
  case SolidityGrammar::ExpressionT::BinaryOperatorClass:
  {
    if (get_binary_operator_expr(expr, new_expr))
      return true;

    break;
  }
  case SolidityGrammar::ExpressionT::UnaryOperatorClass:
  {
    if (get_unary_operator_expr(expr, literal_type, new_expr))
      return true;
    break;
  }
  case SolidityGrammar::ExpressionT::ConditionalOperatorClass:
  {
    // for Ternary Operator (...?...:...) only
    if (get_conditional_operator_expr(expr, new_expr))
      return true;
    break;
  }
  case SolidityGrammar::ExpressionT::DeclRefExprClass:
  {
    if (expr["referencedDeclaration"] > 0)
    {
      // for Contract Type Identifier Only
      if (
        expr["typeDescriptions"]["typeString"].get<std::string>().find(
          "contract") != std::string::npos)
      {
        // TODO
        log_error("we do not handle contract type identifier for now");
        return true;
      }

      // Soldity uses +ve odd numbers to refer to var or functions declared in the contract
      const nlohmann::json &decl = find_decl_ref(expr["referencedDeclaration"]);
      if (decl == empty_json)
        return true;

      if (!check_intrinsic_function(decl))
      {
        if (decl["nodeType"] == "VariableDeclaration")
        {
          if (get_var_decl_ref(decl, new_expr))
            return true;
        }
        else if (decl["nodeType"] == "FunctionDefinition")
        {
          if (get_func_decl_ref(decl, new_expr))
            return true;
        }
        else if (decl["nodeType"] == "StructDefinition")
        {
          std::string id;
          id = prefix + "struct " + decl["canonicalName"].get<std::string>();

          if (context.find_symbol(id) == nullptr)
          {
            if (get_struct_class(decl))
              return true;
          }

          new_expr = symbol_expr(*context.find_symbol(id));
        }
        else if (decl["nodeType"] == "ErrorDefinition")
        {
          std::string name, id;
          name = decl["name"].get<std::string>();
          id = "sol:@" + name + "#" + std::to_string(decl["id"].get<int>());

          if (context.find_symbol(id) == nullptr)
            return true;
          new_expr = symbol_expr(*context.find_symbol(id));
        }
        else if (decl["nodeType"] == "EventDefinition")
        {
          // treat event as a function definition
          if (get_func_decl_ref(decl, new_expr))
            return true;
        }
        else
        {
          log_error(
            "Unsupported DeclRefExprClass type, got nodeType={}",
            decl["nodeType"].get<std::string>());
          return true;
        }
      }
      else
      {
        // for special functions, we need to deal with it separately
        if (get_esbmc_builtin_ref(expr, new_expr))
          return true;
      }
    }
    else
    {
      // Soldity uses -ve odd numbers to refer to built-in var or functions that
      // are NOT declared in the contract
      if (get_esbmc_builtin_ref(expr, new_expr))
        return true;
    }

    break;
  }
  case SolidityGrammar::ExpressionT::Literal:
  {
    // make a type-name json for integer literal conversion
    std::string the_value = expr["value"].get<std::string>();
    const nlohmann::json &literal = expr["typeDescriptions"];
    SolidityGrammar::ElementaryTypeNameT type_name =
      SolidityGrammar::get_elementary_type_name_t(literal);
    log_debug(
      "solidity",
      "	@@@ got Literal: SolidityGrammar::ElementaryTypeNameT::{}",
      SolidityGrammar::elementary_type_name_to_str(type_name));

    if (
      literal_type != nullptr &&
      literal_type["typeString"].get<std::string>().find("bytes") !=
        std::string::npos)
    {
      // literal_type["typeString"] could be
      //    "bytes1" ... "bytes32"
      //    "bytes storage ref"
      // e.g.
      //    bytes1 x = 0x12;
      //    bytes32 x = "string";
      //    bytes x = "string";
      //

      SolidityGrammar::ElementaryTypeNameT type =
        SolidityGrammar::get_elementary_type_name_t(literal_type);

      int byte_size;
      if (type == SolidityGrammar::ElementaryTypeNameT::BYTES)
        // dynamic bytes array, the type is set to unsignedbv(256);
        byte_size = 32;
      else
        byte_size = bytesn_type_name_to_size(type);

      // convert hex to decimal value and populate
      switch (type_name)
      {
      case SolidityGrammar::ElementaryTypeNameT::INT_LITERAL:
      {
        if (convert_hex_literal(the_value, new_expr, byte_size * 8))
          return true;
        break;
      }
      case SolidityGrammar::ElementaryTypeNameT::STRING_LITERAL:
      {
        std::string hex_val = expr["hexValue"].get<std::string>();

        // add padding
        for (int i = 0; i < byte_size; i++)
          hex_val += "00";
        hex_val.resize(byte_size * 2);

        if (convert_hex_literal(hex_val, new_expr, byte_size * 8))
          return true;
        break;
      }
      default:
        assert(!"Error occurred when handling bytes literal");
      }
      break;
    }

    switch (type_name)
    {
    case SolidityGrammar::ElementaryTypeNameT::INT_LITERAL:
    {
      assert(literal_type != nullptr);
      if (
        the_value.length() >= 2 &&
        the_value.substr(0, 2) == "0x") // meaning hex-string
      {
        if (convert_hex_literal(the_value, new_expr))
          return true;
      }
      else if (convert_integer_literal(literal_type, the_value, new_expr))
        return true;
      break;
    }
    case SolidityGrammar::ElementaryTypeNameT::BOOL:
    {
      if (convert_bool_literal(literal, the_value, new_expr))
        return true;
      break;
    }
    case SolidityGrammar::ElementaryTypeNameT::STRING_LITERAL:
    {
      if (convert_string_literal(the_value, new_expr))
        return true;
      break;
    }
    case SolidityGrammar::ElementaryTypeNameT::ADDRESS:
    case SolidityGrammar::ElementaryTypeNameT::ADDRESS_PAYABLE:
    {
      // 20 bytes
      if (convert_hex_literal(the_value, new_expr, 160))
        return true;
      break;
    }
    default:
      assert(!"Literal not implemented");
    }

    break;
  }
  case SolidityGrammar::ExpressionT::Tuple:
  {
    // "nodeType": "TupleExpression":
    //    1. InitList: uint[3] x = [1, 2, 3];
    //    2. Operator:
    //        - (x+1) % 2
    //        - if( x && (y || z) )
    //    3. TupleExpr:
    //        - multiple returns: return (x, y);
    //        - (x, y) = (y, x)

    assert(expr.contains("components"));
    SolidityGrammar::TypeNameT type =
      SolidityGrammar::get_type_name_t(expr["typeDescriptions"]);

    switch (type)
    {
    // case 1
    case SolidityGrammar::TypeNameT::ArrayTypeName:
    {
      assert(literal_type != nullptr);

      // get elem type
      nlohmann::json elem_literal_type =
        make_array_elementary_type(literal_type);

      // get size
      exprt size;
      size = constant_exprt(
        integer2binary(expr["components"].size(), bv_width(int_type())),
        integer2string(expr["components"].size()),
        int_type());

      // get array type
      typet arr_type;
      if (get_type_description(literal_type, arr_type))
        return true;

      // reallocate array size
      arr_type = array_typet(arr_type.subtype(), size);

      // declare static array tuple
      exprt inits;
      inits = gen_zero(arr_type);

      // populate array
      int i = 0;
      for (const auto &arg : expr["components"].items())
      {
        exprt init;
        if (get_expr(arg.value(), elem_literal_type, init))
          return true;

        inits.operands().at(i) = init;
        i++;
      }

      new_expr = inits;
      break;
    }

    // case 3
    case SolidityGrammar::TypeNameT::TupleTypeName: // case 3
    {
      /*
      we assume there are three types of tuple expr:
      0. dump: (x,y);
      1. fixed: (x,y) = (y,x);
      2. function-related: 
          2.1. (x,y) = func();
          2.2. return (x,y);

      case 0:
        1. create a struct type
        2. create a struct type instance
        3. new_expr = instance
        e.g.
        (x , y) ==>
        struct Tuple
        {
          uint x,
          uint y
        };
        Tuple tuple;

      case 1:
        1. add special handling in binary operation.
           when matching struct_expr A = struct_expr B,
           divided into A.operands()[i] = B.operands()[i]
           and populated into a code_block.
        2. new_expr = code_block
        e.g.
        (x, y) = (1, 2) ==>
        {
          tuple.x = 1;
          tuple.y = 2;
        }
        ? any potential scope issue?

      case 2:
        1. when parsing the funciton definition, if the returnParam > 1
           make the function return void instead, and create a struct type
        2. when parsing the return statement, if the return value is a tuple,
           create a struct type instance, do assignments,  and return empty;
        3. when the lhs is tuple and rhs is func_call, get_tuple_instance_expr based 
           on the func_call, and do case 1.
        e.g.
        function test() returns (uint, uint)
        {
          return (1,2);
        }
        ==>
        struct Tuple
        {
          uint x;
          uint y;
        }
        function test()
        {
          Tuple tuple;
          tuple.x = 1;
          tuple.y = 2;
          return;
        }
      */

      // 1. construct struct type
      if (get_tuple_definition(expr))
        return true;

      //2. construct struct_type instance
      if (get_tuple_instance(expr, new_expr))
        return true;

      break;
    }

    // case 2
    default:
    {
      if (get_expr(expr["components"][0], literal_type, new_expr))
        return true;
      break;
    }
    }

    break;
  }
  case SolidityGrammar::ExpressionT::Mapping:
  {
    exprt _block;
    if (get_mapping_definition(expr, _block))
      return true;

    new_expr = _block;
    break;
  }
  case SolidityGrammar::ExpressionT::CallExprClass:
  {
    const nlohmann::json &callee_expr_json = expr["expression"];

    // 0. check if it's a solidity built-in function
    if (
      !get_sol_builtin_ref(expr, new_expr) &&
      !check_intrinsic_function(callee_expr_json))
    {
      // construct call
      typet type = to_code_type(new_expr.type()).return_type();

      side_effect_expr_function_callt call;
      call.function() = new_expr;
      call.type() = type;

      // populate params
      // the number of arguments defined in the template
      size_t define_size = to_code_type(new_expr.type()).arguments().size();
      // the number of arguments actually inside the json file
      const size_t arg_size = expr["arguments"].size();
      if (define_size >= arg_size)
      {
        // we only populate the exact number of args according to the template
        for (const auto &arg : expr["arguments"].items())
        {
          exprt single_arg;
          if (get_expr(
                arg.value(), arg.value()["typeDescriptions"], single_arg))
            return true;

          call.arguments().push_back(single_arg);
        }
      }

      new_expr = call;
      break;
    }

    // 1. Get callee expr
    if (
      callee_expr_json.contains("nodeType") &&
      callee_expr_json["nodeType"] == "MemberAccess")
    {
      // ContractMemberCall

      const int contract_func_id =
        callee_expr_json["referencedDeclaration"].get<int>();
      const nlohmann::json caller_expr_json = find_decl_ref(contract_func_id);
      if (caller_expr_json == empty_json)
        return true;

      std::string ref_contract_name;
      if (get_current_contract_name(caller_expr_json, ref_contract_name))
        return true;

      std::string name, id;
      get_function_definition_name(caller_expr_json, name, id);

      if (context.find_symbol(id) == nullptr)
        // probably a built-in function
        // that is not supported yet
        return true;

      const symbolt s = *context.find_symbol(id);
      typet type = s.type;

      new_expr = exprt("symbol", type);
      new_expr.identifier(id);
      new_expr.cmt_lvalue(true);
      new_expr.name(name);
      new_expr.set("#member_name", prefix + ref_contract_name);

      // obtain the type of return value
      // It can be retrieved directly from the original function declaration
      typet t;
      if (get_type_description(caller_expr_json["returnParameters"], t))
        return true;

      side_effect_expr_function_callt call;
      call.function() = new_expr;
      call.type() = t;

      // populate params
      auto param_nodes = caller_expr_json["parameters"]["parameters"];
      unsigned num_args = 0;
      nlohmann::json param = nullptr;
      nlohmann::json::iterator itr = param_nodes.begin();

      for (const auto &arg : expr["arguments"].items())
      {
        if (itr != param_nodes.end())
        {
          if ((*itr).contains("typeDescriptions"))
          {
            param = (*itr)["typeDescriptions"];
          }
          ++itr;
        }

        exprt single_arg;
        if (get_expr(arg.value(), param, single_arg))
          return true;

        call.arguments().push_back(single_arg);
        ++num_args;
        param = nullptr;
      }

      new_expr = call;
      break;
    }

    // wrap it in an ImplicitCastExpr to perform conversion of FunctionToPointerDecay
    nlohmann::json implicit_cast_expr =
      make_implicit_cast_expr(callee_expr_json, "FunctionToPointerDecay");
    exprt callee_expr;
    if (get_expr(implicit_cast_expr, callee_expr))
      return true;

    // 2. Get type
    // extract from the return_type
    assert(callee_expr.is_symbol());
    if (expr["kind"] == "structConstructorCall")
    {
      // e.g. Book book = Book('Learn Java', 'TP', 1);
      if (callee_expr.type().id() != irept::id_struct)
        return true;

      typet t = callee_expr.type();
      exprt inits = gen_zero(t);

      int ref_id = callee_expr_json["referencedDeclaration"].get<int>();
      const nlohmann::json struct_ref = find_decl_ref(ref_id);
      if (struct_ref == empty_json)
        return true;

      const nlohmann::json members = struct_ref["members"];
      const nlohmann::json args = expr["arguments"];

      // popluate components
      for (size_t i = 0; i < inits.operands().size() && i < args.size(); i++)
      {
        exprt init;
        if (get_expr(args.at(i), members.at(i)["typeDescriptions"], init))
          return true;

        const struct_union_typet::componentt *c =
          &to_struct_type(t).components().at(i);
        typet elem_type = c->type();

        solidity_gen_typecast(ns, init, elem_type);
        inits.operands().at(i) = init;
      }

      new_expr = inits;
      break;
    }

    // funciton call expr
    assert(callee_expr.type().is_code());
    typet type = to_code_type(callee_expr.type()).return_type();

    side_effect_expr_function_callt call;
    call.function() = callee_expr;
    call.type() = type;

    // special case: handling revert and require
    // insert a bool false as the first argument.
    // drop the rest of params.
    if (
      callee_expr.type().get("#sol_name").as_string().find("revert") !=
      std::string::npos)
    {
      call.arguments().push_back(false_exprt());
      new_expr = call;

      break;
    }

    // 3. populate param
    assert(callee_expr_json.contains("referencedDeclaration"));

    //! we might use int_const instead of the original param type (e.g. uint_8).
    nlohmann::json param_nodes = callee_expr_json["argumentTypes"];
    nlohmann::json param = nullptr;
    nlohmann::json::iterator itr = param_nodes.begin();
    unsigned num_args = 0;

    for (const auto &arg : expr["arguments"].items())
    {
      exprt single_arg;
      if (get_expr(arg.value(), *itr, single_arg))
        return true;
      call.arguments().push_back(single_arg);

      ++num_args;
      ++itr;
      param = nullptr;

      // Special case: require
      // __ESBMC_assume only handle one param.
      if (
        callee_expr.type().get("#sol_name").as_string().find("require") !=
        std::string::npos)
        break;
    }
    log_debug("solidity", "  @@ num_args={}", num_args);

    new_expr = call;
    break;
  }
  case SolidityGrammar::ExpressionT::ImplicitCastExprClass:
  {
    if (get_cast_expr(expr, new_expr, literal_type))
      return true;
    break;
  }
  case SolidityGrammar::ExpressionT::IndexAccess:
  {
    // 1. get type, this is the base type of array
    typet t;
    if (get_type_description(expr["typeDescriptions"], t))
      return true;

    // for MAPPING
    typet base_t;
    if (get_type_description(
          expr["baseExpression"]["typeDescriptions"], base_t))
      return true;
    if (base_t.get("#sol_type").as_string() == "MAPPING")
    {
      // this could be set or get
      // e.g. y = map[x] or map[x] = y
      // here, we convert it to a 'get' and check if it's a 'set' in other places
      // ==> map_get_int(&m, "1234")

      // get m
      exprt base;
      if (get_expr(
            expr["baseExpression"],
            expr["baseExpression"]["typeDescriptions"],
            base))
        return true;
      // &m
      exprt address_of = address_of_exprt(base);

      // get key
      // e.g.  "1234", 1234, struct s...
      // "Let's say you have a mapping mapping(uint => uint) myMapping, then all elements myMapping[0], myMapping[1], myMapping[123123], ... are already initialized with the default value. If you map uint to uint, then you map key-type "uint" to value-type "uint"."

      exprt idx;
      if (get_mapping_key(expr["indexExpression"], idx))
        return true;

      // get func_call
      std::string _val;
      if (get_mapping_value_type(t, _val))
        return true;
      std::string func_name = "map_get_" + _val;
      std::string func_id = "c:@F@map_get_" + _val;

      if (context.find_symbol(func_id) == nullptr)
        return true;
      const auto &sym = *context.find_symbol(func_id);

      locationt l;
      get_location_from_decl(expr, l);

      side_effect_expr_function_callt call_expr;
      get_library_function_call(func_name, func_id, sym.type, l, call_expr);

      call_expr.arguments().push_back(address_of);
      call_expr.arguments().push_back(idx);

      // dereference: *map_get
      new_expr = dereference_exprt(call_expr, call_expr.type());

      // add label
      new_expr.type().set("#sol_type", "MAP_GET");
      new_expr.type().set("#sol_mapping_type", _val);
      break;
    }

    // for BYTESN, where the index access is read-only
    if (is_bytes_type(t))
    {
      // this means we are dealing with bytes type
      // jump out if it's "bytes[]" or "bytesN[]" or "func()[]"
      SolidityGrammar::TypeNameT tname = SolidityGrammar::get_type_name_t(
        expr["baseExpression"]["typeDescriptions"]);
      if (
        !(tname == SolidityGrammar::ArrayTypeName ||
          tname == SolidityGrammar::DynArrayTypeName) &&
        expr["baseExpression"].contains("referencedDeclaration"))
      {
        // e.g.
        //    bytes3 x = 0x123456
        //    bytes1 y = x[0]; // 0x12
        //    bytes1 z = x[1]; // 0x34
        // which equals to
        //    bytes1 z = bswap(x) >> 1 & 0xff
        // for bytes32 x = "test";
        //    x[10] == 0x00 due to the padding
        exprt src_val, src_offset, bswap, bexpr;

        const nlohmann::json &decl = find_decl_ref(
          expr["baseExpression"]["referencedDeclaration"].get<int>());
        if (decl == empty_json)
          return true;

        if (get_var_decl_ref(decl, src_val))
          return true;

        if (get_expr(
              expr["indexExpression"], expr["typeDescriptions"], src_offset))
          return true;

        // extract particular byte based on idx (offset)
        bexpr = exprt("byte_extract_big_endian", src_val.type());
        bexpr.copy_to_operands(src_val, src_offset);

        solidity_gen_typecast(ns, bexpr, unsignedbv_typet(8));

        new_expr = bexpr;
        break;
      }
    }

    // 2. get the decl ref of the array
    exprt array;

    // 2.1 arr[n] / x.arr[n]
    if (expr["baseExpression"].contains("referencedDeclaration"))
    {
      if (get_expr(expr["baseExpression"], literal_type, array))
        return true;
    }
    else
    {
      // 2.2 func()[n]
      const nlohmann::json &decl = expr["baseExpression"];
      nlohmann::json implicit_cast_expr =
        make_implicit_cast_expr(decl, "ArrayToPointerDecay");
      if (get_expr(implicit_cast_expr, literal_type, array))
        return true;
    }

    // 3. get the position index
    exprt pos;
    if (get_expr(expr["indexExpression"], expr["typeDescriptions"], pos))
      return true;

    // BYTES:  func_ret_bytes()[]
    // same process as above
    if (is_bytes_type(array.type()))
    {
      exprt bexpr = exprt("byte_extract_big_endian", pos.type());
      bexpr.copy_to_operands(array, pos);
      solidity_gen_typecast(ns, bexpr, unsignedbv_typet(8));
      new_expr = bexpr;
      break;
    }

    new_expr = index_exprt(array, pos, t);
    break;
  }
  case SolidityGrammar::ExpressionT::NewExpression:
  {
    // 1. new dynamic array, e.g.
    //    uint[] memory a = new uint[](7);
    // 2. new bytes array e.g.
    //    bytes memory b = new bytes(7)
    // 3. new object, e.g.
    //    Base x = new Base(1, 2);

    // case 1
    // e.g.
    //    a = new uint[](7)
    // convert to
    //    uint y[7] = {0,0,0,0,0,0,0};
    //    a = y;
    nlohmann::json callee_expr_json = expr["expression"];
    if (callee_expr_json.contains("typeName"))
    {
      // case 1
      // e.g.
      //    a = new uint[](7)
      // convert to
      //    uint y[7] = {0,0,0,0,0,0,0};
      //    a = y;
      if (is_dyn_array(callee_expr_json["typeName"]))
      {
        if (get_empty_array_ref(expr, new_expr))
          return true;
        break;
      }
      //case 2:
      // the contract/constructor name cannot be "bytes"
      if (
        callee_expr_json["typeName"]["typeDescriptions"]["typeString"]
          .get<std::string>() == "bytes")
      {
        // populate 0x00 to bytes array
        // same process in case SolidityGrammar::ExpressionT::Literal
        assert(expr.contains("arguments") && expr["arguments"].size() == 1);

        int byte_size = stoi(expr["arguments"][0]["value"].get<std::string>());
        std::string hex_val = "";

        for (int i = 0; i < byte_size; i++)
          hex_val += "00";
        hex_val.resize(byte_size * 2);

        if (convert_hex_literal(hex_val, new_expr, byte_size * 8))
          return true;
        break;
      }
    }

    // case 3
    // first, call the constructor
    if (get_constructor_call(expr, new_expr))
      return true;

    side_effect_exprt tmp_obj("temporary_object", new_expr.type());
    codet code_expr("expression");
    code_expr.operands().push_back(new_expr);
    tmp_obj.initializer(code_expr);
    tmp_obj.location() = new_expr.location();
    new_expr.swap(tmp_obj);

    break;
  }
  case SolidityGrammar::ExpressionT::ContractMemberCall:
  case SolidityGrammar::ExpressionT::StructMemberCall:
  case SolidityGrammar::ExpressionT::EnumMemberCall:
  {
    // 1. ContractMemberCall: contractInstance.call()
    //                        contractInstanceArray[0].call()
    //                        contractInstance.x
    // 2. StructMemberCall: struct.member
    // 3. EnumMemberCall: enum.member
    // 4. (?)internal property: tx.origin, msg.sender, ...

    // Function symbol id is sol:@C@referenced_function_contract_name@F@function_name#referenced_function_id
    // Using referencedDeclaration will point us to the original declared function. This works even for inherited function and overrided functions.
    assert(expr.contains("expression"));
    const nlohmann::json callee_expr_json = expr["expression"];

    const int caller_id = callee_expr_json["referencedDeclaration"].get<int>();

    const nlohmann::json caller_expr_json = find_decl_ref(caller_id);
    if (caller_expr_json == empty_json)
      return true;

    switch (type)
    {
    case SolidityGrammar::ExpressionT::StructMemberCall:
    {
      exprt base;
      if (get_expr(callee_expr_json, base))
        return true;

      const int struct_var_id = expr["referencedDeclaration"].get<int>();
      const nlohmann::json struct_var_ref = find_decl_ref(struct_var_id);
      if (struct_var_ref == empty_json)
        return true;

      exprt comp;
      if (get_var_decl_ref(struct_var_ref, comp))
        return true;

      assert(comp.name() == expr["memberName"]);
      new_expr = member_exprt(base, comp.name(), comp.type());

      break;
    }
    case SolidityGrammar::ExpressionT::ContractMemberCall:
    {
      // this should be handled in CallExprClass
      log_error("Unexpected ContractMemberCall");
      return true;
    }
    case SolidityGrammar::ExpressionT::EnumMemberCall:
    {
      const int enum_id = expr["referencedDeclaration"].get<int>();
      const nlohmann::json enum_member_ref = find_decl_ref(enum_id);
      if (enum_member_ref == empty_json)
        return true;

      if (get_enum_member_ref(enum_member_ref, new_expr))
        return true;

      break;
    }
    default:
    {
      if (get_expr(callee_expr_json, literal_type, new_expr))
        return true;

      break;
    }
    }

    break;
  }
  case SolidityGrammar::ExpressionT::BuiltinMemberCall:
  {
    if (get_sol_builtin_ref(expr, new_expr))
      return true;
    break;
  }
  case SolidityGrammar::ExpressionT::ElementaryTypeNameExpression:
  {
    // perform type conversion
    // e.g.
    // address payable public owner = payable(msg.sender);
    // or
    // uint32 a = 0x432178;
    // uint16 b = uint16(a); // b will be 0x2178 now

    assert(expr.contains("expression"));
    const nlohmann::json conv_expr = expr["expression"];
    typet type;
    exprt from_expr;

    // 1. get source expr
    // assume: only one argument
    if (get_expr(expr["arguments"][0], literal_type, from_expr))
      return true;

    // 2. get target type
    if (get_type_description(conv_expr["typeDescriptions"], type))
      return true;

    // 3. generate the type casting expr
    convert_type_expr(ns, from_expr, type);

    new_expr = from_expr;
    break;
  }
  case SolidityGrammar::ExpressionT::NullExpr:
  {
    // e.g. (, x) = (1, 2);
    // the first component in lhs is nil
    new_expr = nil_exprt();
    break;
  }
  default:
  {
    assert(!"Unimplemented type in rule expression");
    return true;
  }
  }

  new_expr.location() = location;
  return false;
}

bool solidity_convertert::get_current_contract_name(
  const nlohmann::json &ast_node,
  std::string &contract_name)
{
  // check if it is recorded in the scope_map
  if (ast_node.contains("scope"))
  {
    int scope_id = ast_node["scope"];

    if (exportedSymbolsList.count(scope_id))
    {
      std::string c_name = exportedSymbolsList[scope_id];
      if (linearizedBaseList.count(c_name))
      {
        contract_name = c_name;
        return false;
      }
    }
  }

  // implicit constructor
  if (ast_node.empty())
  {
    contract_name = current_contractName;
    return false;
  }

  // utilize the find_decl_ref
  if (ast_node.contains("id"))
  {
    const int ref_id = ast_node["id"].get<int>();

    if (exportedSymbolsList.count(ref_id))
    {
      // this can be contract, error, et al.
      // therefore, we utilize the linearizedBaseList to make sure it's really a contract
      std::string c_name = exportedSymbolsList[ref_id];
      if (linearizedBaseList.count(c_name))
        contract_name = exportedSymbolsList[ref_id];
    }
    else
      find_decl_ref(ref_id, contract_name);
    return false;
  }

  // unexpected
  return true;
}

bool solidity_convertert::get_binary_operator_expr(
  const nlohmann::json &expr,
  exprt &new_expr)
{
  // preliminary step for recursive BinaryOperation
  current_BinOp_type.push(&(expr["typeDescriptions"]));

  // 1. Convert LHS and RHS
  // For "Assignment" expression, it's called "leftHandSide" or "rightHandSide".
  // For "BinaryOperation" expression, it's called "leftExpression" or "leftExpression"
  exprt lhs, rhs;
  if (expr.contains("leftHandSide"))
  {
    nlohmann::json literalType = expr["leftHandSide"]["typeDescriptions"];

    if (get_expr(expr["leftHandSide"], lhs))
      return true;

    if (get_expr(expr["rightHandSide"], literalType, rhs))
      return true;
  }
  else if (expr.contains("leftExpression"))
  {
    nlohmann::json literalType_l = expr["leftExpression"]["typeDescriptions"];
    nlohmann::json literalType_r = expr["rightExpression"]["typeDescriptions"];

    if (get_expr(expr["leftExpression"], literalType_l, lhs))
      return true;

    if (get_expr(expr["rightExpression"], literalType_r, rhs))
      return true;
  }
  else
    assert(!"should not be here - unrecognized LHS and RHS keywords in expression JSON");

  // 2. Get type
  typet t;
  assert(current_BinOp_type.size());
  const nlohmann::json &binop_type = *(current_BinOp_type.top());
  if (get_type_description(binop_type, t))
    return true;

  typet common_type;
  if (expr.contains("commonType"))
  {
    if (get_type_description(expr["commonType"], common_type))
      return true;
  }

  // 3. Convert opcode
  SolidityGrammar::ExpressionT opcode =
    SolidityGrammar::get_expr_operator_t(expr);
  log_debug(
    "solidity",
    "	@@@ got binop.getOpcode: SolidityGrammar::{}",
    SolidityGrammar::expression_to_str(opcode));

  // special handling for mapping set value
  // for any mapping index access, we will initially return as map_get
  // here, we further identidy it as map_get or map_set
  std::string op_str = SolidityGrammar::expression_to_str(opcode);
  if (
    lhs.type().get("#sol_type").as_string() == "MAP_GET" &&
    op_str.find("Assign") != std::string::npos)
  {
    // get map_set function call
    assert(!lhs.type().get("#sol_mapping_type").empty());
    std::string _val = lhs.type().get("#sol_mapping_type").as_string();
    std::string func_name = "map_set_" + _val;
    std::string func_id = "c:@F@map_set_" + _val;

    if (context.find_symbol(func_id) == nullptr)
      return true;
    const auto &sym = *context.find_symbol(func_id);

    locationt l;
    get_location_from_decl(expr, l);

    side_effect_expr_function_callt call_expr;
    get_library_function_call(func_name, func_id, sym.type, l, call_expr);

    // extract args from map_get
    exprt fst_arg = lhs.op0().op1().op0();
    exprt snd_arg = lhs.op0().op1().op1();

    if (opcode == SolidityGrammar::ExpressionT::BO_Assign)
    {
      // handling non-compound assignment
      // e.g  *map_get(map, x) = 1 ==> map_set(map, x, 1);
      call_expr.arguments().push_back(fst_arg); // map
      call_expr.arguments().push_back(snd_arg); // x
      call_expr.arguments().push_back(rhs);     // 1

      new_expr = call_expr;
    }
    else
    {
      // handling compound assignment
      // e.g. map[x] +=1 ==> map[x] = map[x] + 1 ==> map_set(map,x,(*map_get(x,1) + 1))

      exprt sub_expr;
      switch (opcode)
      {
      case SolidityGrammar::ExpressionT::BO_AddAssign:
      {
        sub_expr = exprt("+", t);
        break;
      }
      case SolidityGrammar::ExpressionT::BO_SubAssign:
      {
        sub_expr = exprt("-", t);
        break;
      }
      case SolidityGrammar::ExpressionT::BO_MulAssign:
      {
        sub_expr = exprt("*", t);
        break;
      }
      case SolidityGrammar::ExpressionT::BO_DivAssign:
      {
        sub_expr = exprt("/", t);
        break;
      }
      case SolidityGrammar::ExpressionT::BO_RemAssign:
      {
        sub_expr = exprt("mod", t);
        break;
      }
      case SolidityGrammar::ExpressionT::BO_ShlAssign:
      {
        sub_expr = exprt("shl", t);
        break;
      }
      case SolidityGrammar::ExpressionT::BO_ShrAssign:
      {
        sub_expr = exprt("shr", t);
        break;
      }
      case SolidityGrammar::ExpressionT::BO_AndAssign:
      {
        sub_expr = exprt("bitand", t);
        break;
      }
      case SolidityGrammar::ExpressionT::BO_XorAssign:
      {
        sub_expr = exprt("bitxor", t);
        break;
      }
      case SolidityGrammar::ExpressionT::BO_OrAssign:
      {
        sub_expr = exprt("bitor", t);
        break;
      }
      default:
      {
        log_error(
          "Unexpected side-effect, got {}",
          SolidityGrammar::expression_to_str(opcode));
        return true;
      }
      }

      // gen_type_cast
      convert_type_expr(ns, lhs, t);
      convert_type_expr(ns, rhs, t);

      sub_expr.copy_to_operands(lhs, rhs);
      call_expr.arguments().push_back(fst_arg);  // map
      call_expr.arguments().push_back(snd_arg);  // x
      call_expr.arguments().push_back(sub_expr); // (*map_get(x,1) + 1)

      new_expr = call_expr;
    }

    current_BinOp_type.pop();
    return false;
  }

  switch (opcode)
  {
  case SolidityGrammar::ExpressionT::BO_Assign:
  {
    // special handling for tuple-type assignment;
    typet lt = lhs.type();
    typet rt = rhs.type();
    if (lt.get("#sol_type") == "TUPLE_INSTANCE")
    {
      code_blockt _block;
      if (rt.get("#sol_type") == "TUPLE_INSTANCE")
      {
        // e.g. (x,y) = (1,2); (x,y) = (func(),x);
        // =>
        //  t.mem0 = 1; #1
        //  t.mem1 = 2; #2
        //  x = t.mem0; #3
        //  y = t.mem1; #4

        size_t i = 0;
        size_t j = 0;
        size_t ls = to_struct_type(lhs.type()).components().size();
        size_t rs = to_struct_type(rhs.type()).components().size();
        assert(ls <= rs);

        // do #1 #2
        while (i < rs)
        {
          exprt lop;
          if (get_tuple_member_call(
                rhs.identifier(),
                to_struct_type(rhs.type()).components().at(i),
                lop))
            return true;

          exprt rop = rhs.operands().at(i);
          //do assignment
          get_tuple_assignment(_block, lop, rop);
          // update counter
          ++i;
        }

        // reset
        i = 0;

        // do #3 #4
        while (i < ls && j < rs)
        {
          // construct assignemnt
          exprt lcomp = to_struct_type(lhs.type()).components().at(i);
          exprt rcomp = to_struct_type(rhs.type()).components().at(j);
          exprt lop = lhs.operands().at(i);
          exprt rop;

          if (get_tuple_member_call(
                rhs.identifier(),
                to_struct_type(rhs.type()).components().at(j),
                rop))
            return true;

          if (lcomp.name() != rcomp.name())
          {
            // e.g. (, x) = (1, 2)
            //        null <=> tuple2.mem0
            // tuple1.mem1 <=> tuple2.mem1
            ++j;
            continue;
          }
          //do assignment
          get_tuple_assignment(_block, lop, rop);
          // update counter
          ++i;
          ++j;
        }
      }
      else if (rt.get("#sol_type") == "TUPLE")
      {
        // e.g. (x,y) = func(); (x,y) = func(func2()); (x, (x,y)) = (x, func());
        exprt new_rhs;
        if (get_tuple_function_ref(
              expr["rightHandSide"]["expression"], new_rhs))
          return true;

        // add function call
        get_tuple_function_call(_block, rhs);

        size_t ls = to_struct_type(lhs.type()).components().size();
        size_t rs = to_struct_type(new_rhs.type()).components().size();
        assert(ls == rs);

        for (size_t i = 0; i < ls; i++)
        {
          exprt lop = lhs.operands().at(i);

          exprt rop;
          if (get_tuple_member_call(
                new_rhs.identifier(),
                to_struct_type(new_rhs.type()).components().at(i),
                rop))
            return true;

          get_tuple_assignment(_block, lop, rop);
        }
      }
      else
      {
        log_error("Unexpected Tuple");
        abort();
      }

      // fix ordering
      // e.g.
      // x = 1;
      // y = 2;
      // (x , y , x, y) =(y, x , 0, 0);
      // assert(x == 2); // hold
      // assert(y == 1); // hold
      code_blockt ordered_block;
      std::set<irep_idt> assigned_symbol;
      for (auto &assign : _block.operands())
      {
        // assume lhs should always be a symbol type
        irep_idt id = assign.op0().op0().identifier();
        if (!id.empty() && assigned_symbol.count(id))
          // e.g. (x,x) = (1, 2); x==1 hold
          continue;
        assigned_symbol.insert(id);
        ordered_block.move_to_operands(assign);
      }

      new_expr = ordered_block;

      current_BinOp_type.pop();
      return false;
    }

    new_expr = side_effect_exprt("assign", t);
    break;
  }
  case SolidityGrammar::ExpressionT::BO_Add:
  {
    if (t.is_floatbv())
      assert(!"Solidity does not support FP arithmetic as of v0.8.6.");
    else
      new_expr = exprt("+", t);
    break;
  }
  case SolidityGrammar::ExpressionT::BO_Sub:
  {
    if (t.is_floatbv())
      assert(!"Solidity does not support FP arithmetic as of v0.8.6.");
    else
      new_expr = exprt("-", t);
    break;
  }
  case SolidityGrammar::ExpressionT::BO_Mul:
  {
    if (t.is_floatbv())
      assert(!"Solidity does not support FP arithmetic as of v0.8.6.");
    else
      new_expr = exprt("*", t);
    break;
  }
  case SolidityGrammar::ExpressionT::BO_Div:
  {
    if (t.is_floatbv())
      assert(!"Solidity does not support FP arithmetic as of v0.8.6.");
    else
      new_expr = exprt("/", t);
    break;
  }
  case SolidityGrammar::ExpressionT::BO_Rem:
  {
    new_expr = exprt("mod", t);
    break;
  }
  case SolidityGrammar::ExpressionT::BO_Shl:
  {
    new_expr = exprt("shl", t);
    break;
  }
  case SolidityGrammar::ExpressionT::BO_Shr:
  {
    new_expr = exprt("shr", t);
    break;
  }
  case SolidityGrammar::BO_And:
  {
    new_expr = exprt("bitand", t);
    break;
  }
  case SolidityGrammar::BO_Xor:
  {
    new_expr = exprt("bitxor", t);
    break;
  }
  case SolidityGrammar::BO_Or:
  {
    new_expr = exprt("bitor", t);
    break;
  }
  case SolidityGrammar::ExpressionT::BO_GT:
  {
    new_expr = exprt(">", t);
    break;
  }
  case SolidityGrammar::ExpressionT::BO_LT:
  {
    new_expr = exprt("<", t);
    break;
  }
  case SolidityGrammar::ExpressionT::BO_GE:
  {
    new_expr = exprt(">=", t);
    break;
  }
  case SolidityGrammar::ExpressionT::BO_LE:
  {
    new_expr = exprt("<=", t);
    break;
  }
  case SolidityGrammar::ExpressionT::BO_NE:
  {
    new_expr = exprt("notequal", t);
    break;
  }
  case SolidityGrammar::ExpressionT::BO_EQ:
  {
    new_expr = exprt("=", t);
    break;
  }
  case SolidityGrammar::ExpressionT::BO_LAnd:
  {
    new_expr = exprt("and", t);
    break;
  }
  case SolidityGrammar::ExpressionT::BO_LOr:
  {
    new_expr = exprt("or", t);
    break;
  }
  case SolidityGrammar::ExpressionT::BO_Pow:
  {
    // lhs**rhs => pow(lhs, rhs)
    // double pow(double base, double exponent)

    side_effect_expr_function_callt call_expr;
    get_library_function_call(
      "pow", "c:@F@pow", double_type(), lhs.location(), call_expr);

    solidity_gen_typecast(ns, lhs, double_type());
    solidity_gen_typecast(ns, rhs, double_type());
    call_expr.arguments().push_back(lhs);
    call_expr.arguments().push_back(rhs);

    new_expr = call_expr;

    return false;
  }
  default:
  {
    if (get_compound_assign_expr(expr, new_expr))
    {
      assert(!"Unimplemented binary operator");
      return true;
    }

    current_BinOp_type.pop();

    return false;
  }
  }

  // for bytes type
  if (is_bytes_type(lhs.type()) || is_bytes_type(rhs.type()))
  {
    switch (opcode)
    {
    case SolidityGrammar::ExpressionT::BO_GT:
    case SolidityGrammar::ExpressionT::BO_LT:
    case SolidityGrammar::ExpressionT::BO_GE:
    case SolidityGrammar::ExpressionT::BO_LE:
    case SolidityGrammar::ExpressionT::BO_NE:
    case SolidityGrammar::ExpressionT::BO_EQ:
    {
      // e.g. cmp(0x74,  0x7500)
      // ->   cmp(0x74, 0x0075)

      // convert string to bytes
      // e.g.
      //    data1 = "test"; data2 = 0x74657374; // "test"
      //    assert(data1 == data2); // true
      // Do type conversion before the bswap
      // the arguement of bswap should only be int/uint type, not string
      // e.g. data1 == "test", it should not be bswap("test")
      // instead it should be bswap(0x74657374)
      // this may overwrite the lhs & rhs.
      if (!is_bytes_type(lhs.type()))
      {
        if (get_expr(expr["leftExpression"], expr["commonType"], lhs))
          return true;
      }
      if (!is_bytes_type(rhs.type()))
      {
        if (get_expr(expr["rightExpression"], expr["commonType"], rhs))
          return true;
      }

      // do implicit type conversion
      convert_type_expr(ns, lhs, common_type);
      convert_type_expr(ns, rhs, common_type);

      exprt bwrhs, bwlhs;
      bwlhs = exprt("bswap", common_type);
      bwlhs.operands().push_back(lhs);
      lhs = bwlhs;

      bwrhs = exprt("bswap", common_type);
      bwrhs.operands().push_back(rhs);
      rhs = bwrhs;

      new_expr.copy_to_operands(lhs, rhs);
      // Pop current_BinOp_type.push as we've finished this conversion
      current_BinOp_type.pop();
      return false;
    }
    case SolidityGrammar::ExpressionT::BO_Shl:
    {
      // e.g.
      //    bytes1 = 0x11
      //    x<<8 == 0x00
      new_expr.copy_to_operands(lhs, rhs);
      convert_type_expr(ns, new_expr, lhs.type());
      current_BinOp_type.pop();
      return false;
    }
    default:
    {
      break;
    }
    }
  }

  // 4.1 check if it needs implicit type conversion
  if (common_type.id() != "")
  {
    convert_type_expr(ns, lhs, common_type);
    convert_type_expr(ns, rhs, common_type);
  }

  // 4.2 Copy to operands
  new_expr.copy_to_operands(lhs, rhs);

  // Pop current_BinOp_type.push as we've finished this conversion
  current_BinOp_type.pop();

  return false;
}

bool solidity_convertert::get_compound_assign_expr(
  const nlohmann::json &expr,
  exprt &new_expr)
{
  // equivalent to clang_c_convertert::get_compound_assign_expr

  SolidityGrammar::ExpressionT opcode =
    SolidityGrammar::get_expr_operator_t(expr);

  switch (opcode)
  {
  case SolidityGrammar::ExpressionT::BO_AddAssign:
  {
    new_expr = side_effect_exprt("assign+");
    break;
  }
  case SolidityGrammar::ExpressionT::BO_SubAssign:
  {
    new_expr = side_effect_exprt("assign-");
    break;
  }
  case SolidityGrammar::ExpressionT::BO_MulAssign:
  {
    new_expr = side_effect_exprt("assign*");
    break;
  }
  case SolidityGrammar::ExpressionT::BO_DivAssign:
  {
    new_expr = side_effect_exprt("assign_div");
    break;
  }
  case SolidityGrammar::ExpressionT::BO_RemAssign:
  {
    new_expr = side_effect_exprt("assign_mod");
    break;
  }
  case SolidityGrammar::ExpressionT::BO_ShlAssign:
  {
    new_expr = side_effect_exprt("assign_shl");
    break;
  }
  case SolidityGrammar::ExpressionT::BO_ShrAssign:
  {
    new_expr = side_effect_exprt("assign_shr");
    break;
  }
  case SolidityGrammar::ExpressionT::BO_AndAssign:
  {
    new_expr = side_effect_exprt("assign_bitand");
    break;
  }
  case SolidityGrammar::ExpressionT::BO_XorAssign:
  {
    new_expr = side_effect_exprt("assign_bitxor");
    break;
  }
  case SolidityGrammar::ExpressionT::BO_OrAssign:
  {
    new_expr = side_effect_exprt("assign_bitor");
    break;
  }
  default:
    return true;
  }

  exprt lhs, rhs;
  if (expr.contains("leftHandSide"))
  {
    nlohmann::json literalType = expr["leftHandSide"]["typeDescriptions"];

    if (get_expr(expr["leftHandSide"], lhs))
      return true;

    if (get_expr(expr["rightHandSide"], literalType, rhs))
      return true;
  }
  else if (expr.contains("leftExpression"))
  {
    nlohmann::json literalType_l = expr["leftExpression"]["typeDescriptions"];
    nlohmann::json literalType_r = expr["rightExpression"]["typeDescriptions"];

    if (get_expr(expr["leftExpression"], literalType_l, lhs))
      return true;

    if (get_expr(expr["rightExpression"], literalType_r, rhs))
      return true;
  }
  else
    assert(!"should not be here - unrecognized LHS and RHS keywords in expression JSON");

  assert(current_BinOp_type.size());
  const nlohmann::json &binop_type = *(current_BinOp_type.top());
  if (get_type_description(binop_type, new_expr.type()))
    return true;

  typet common_type;
  if (expr.contains("commonType"))
  {
    if (get_type_description(expr["commonType"], common_type))
      return true;
  }

  if (common_type.id() != "")
  {
    convert_type_expr(ns, lhs, common_type);
    convert_type_expr(ns, rhs, common_type);
  }

  new_expr.copy_to_operands(lhs, rhs);
  return false;
}

bool solidity_convertert::get_unary_operator_expr(
  const nlohmann::json &expr,
  const nlohmann::json &literal_type,
  exprt &new_expr)
{
  // TODO: Fix me! Currently just support prefix == true,e.g. pre-increment

  // 1. get UnaryOperation opcode
  SolidityGrammar::ExpressionT opcode =
    SolidityGrammar::get_unary_expr_operator_t(expr, expr["prefix"]);
  log_debug(
    "solidity",
    "	@@@ got uniop.getOpcode: SolidityGrammar::{}",
    SolidityGrammar::expression_to_str(opcode));

  // 2. get type
  typet uniop_type;
  if (get_type_description(expr["typeDescriptions"], uniop_type))
    return true;

  // 3. get subexpr
  exprt unary_sub;
  if (get_expr(expr["subExpression"], literal_type, unary_sub))
    return true;

  switch (opcode)
  {
  case SolidityGrammar::ExpressionT::UO_PreDec:
  {
    new_expr = side_effect_exprt("predecrement", uniop_type);
    break;
  }
  case SolidityGrammar::ExpressionT::UO_PreInc:
  {
    new_expr = side_effect_exprt("preincrement", uniop_type);
    break;
  }
  case SolidityGrammar::UO_PostDec:
  {
    new_expr = side_effect_exprt("postdecrement", uniop_type);
    break;
  }
  case SolidityGrammar::UO_PostInc:
  {
    new_expr = side_effect_exprt("postincrement", uniop_type);
    break;
  }
  case SolidityGrammar::ExpressionT::UO_Minus:
  {
    new_expr = exprt("unary-", uniop_type);
    break;
  }
  case SolidityGrammar::ExpressionT::UO_Not:
  {
    new_expr = exprt("bitnot", uniop_type);
    break;
  }

  case SolidityGrammar::ExpressionT::UO_LNot:
  {
    new_expr = exprt("not", bool_type());
    break;
  }
  default:
  {
    assert(!"Unimplemented unary operator");
  }
  }

  new_expr.operands().push_back(unary_sub);
  return false;
}

bool solidity_convertert::get_conditional_operator_expr(
  const nlohmann::json &expr,
  exprt &new_expr)
{
  exprt cond;
  if (get_expr(expr["condition"], cond))
    return true;

  exprt then;
  if (get_expr(expr["trueExpression"], expr["typeDescriptions"], then))
    return true;

  exprt else_expr;
  if (get_expr(expr["falseExpression"], expr["typeDescriptions"], else_expr))
    return true;

  typet t;
  if (get_type_description(expr["typeDescriptions"], t))
    return true;

  exprt if_expr("if", t);
  if_expr.copy_to_operands(cond, then, else_expr);

  new_expr = if_expr;

  return false;
}

bool solidity_convertert::get_cast_expr(
  const nlohmann::json &cast_expr,
  exprt &new_expr,
  const nlohmann::json literal_type)
{
  // 1. convert subexpr
  exprt expr;
  if (get_expr(cast_expr["subExpr"], literal_type, expr))
    return true;

  // 2. get type
  typet type;
  if (cast_expr["castType"].get<std::string>() == "ArrayToPointerDecay")
  {
    // Array's cast_expr will have cast_expr["subExpr"]["typeDescriptions"]:
    //  "typeIdentifier": "t_array$_t_uint8_$2_memory_ptr"
    //  "typeString": "uint8[2] memory"
    // For the data above, SolidityGrammar::get_type_name_t will return ArrayTypeName.
    // But we want Pointer type. Hence, adjusting the type manually to make it like:
    //   "typeIdentifier": "ArrayToPtr",
    //   "typeString": "uint8[2] memory"
    nlohmann::json adjusted_type =
      make_array_to_pointer_type(cast_expr["subExpr"]["typeDescriptions"]);
    if (get_type_description(adjusted_type, type))
      return true;
  }
  // TODO: Maybe can just type = expr.type() for other types as well. Need to make sure types are all set in get_expr (many functions are called multiple times to perform the same action).
  else
  {
    type = expr.type();
  }

  // 3. get cast type and generate typecast
  SolidityGrammar::ImplicitCastTypeT cast_type =
    SolidityGrammar::get_implicit_cast_type_t(
      cast_expr["castType"].get<std::string>());
  switch (cast_type)
  {
  case SolidityGrammar::ImplicitCastTypeT::LValueToRValue:
  {
    solidity_gen_typecast(ns, expr, type);
    break;
  }
  case SolidityGrammar::ImplicitCastTypeT::FunctionToPointerDecay:
  case SolidityGrammar::ImplicitCastTypeT::ArrayToPointerDecay:
  {
    break;
  }
  default:
  {
    assert(!"Unimplemented implicit cast type");
  }
  }

  new_expr = expr;
  return false;
}

bool solidity_convertert::get_var_decl_ref(
  const nlohmann::json &decl,
  exprt &new_expr)
{
  // Function to configure new_expr that has a +ve referenced id, referring to a variable declaration
  assert(decl["nodeType"] == "VariableDeclaration");
  std::string name, id;
  if (decl["stateVariable"])
    get_state_var_decl_name(decl, name, id);
  else
    get_var_decl_name(decl, name, id);

  if (context.find_symbol(id) != nullptr)
    new_expr = symbol_expr(*context.find_symbol(id));
  else
  {
    typet type;
    if (get_type_description(
          decl["typeName"]["typeDescriptions"],
          type)) // "type-name" as in state-variable-declaration
      return true;

    // variable with no value
    new_expr = exprt("symbol", type);
    new_expr.identifier(id);
    new_expr.cmt_lvalue(true);
    new_expr.name(name);
    new_expr.pretty_name(name);
  }

  return false;
}

bool solidity_convertert::get_func_decl_ref(
  const nlohmann::json &decl,
  exprt &new_expr)
{
  // Function to configure new_expr that has a +ve referenced id, referring to a function declaration
  // This allow to get func symbol before we add it to the symbol table
  assert(
    decl["nodeType"] == "FunctionDefinition" ||
    decl["nodeType"] == "EventDefinition");
  std::string name, id;
  get_function_definition_name(decl, name, id);

  if (context.find_symbol(id) != nullptr)
  {
    new_expr = symbol_expr(*context.find_symbol(id));
    return false;
  }

  typet type;
  if (get_func_decl_ref_type(
        decl, type)) // "type-name" as in state-variable-declaration
    return true;
  //! function with no value i.e function body
  new_expr = exprt("symbol", type);
  new_expr.identifier(id);
  new_expr.cmt_lvalue(true);
  new_expr.name(name);
  return false;
}

bool solidity_convertert::get_enum_member_ref(
  const nlohmann::json &decl,
  exprt &new_expr)
{
  assert(decl["nodeType"] == "EnumValue");
  assert(decl.contains("Value"));

  const std::string val = decl["Value"].get<std::string>();

  new_expr = constant_exprt(
    integer2binary(string2integer(val), bv_width(int_type())), val, int_type());

  return false;
}

// get the esbmc built-in methods
bool solidity_convertert::get_esbmc_builtin_ref(
  const nlohmann::json &decl,
  exprt &new_expr)
{
  // Function to configure new_expr that has a -ve referenced id
  // -ve ref id means built-in functions or variables.
  // Add more special function names here

  assert(decl.contains("name"));
  const std::string blt_name = decl["name"].get<std::string>();
  std::string name, id;

  // "require" keyword is virtually identical to "assume"
  if (blt_name == "require" || blt_name == "revert")
    name = "__ESBMC_assume";
  else
    name = blt_name;
  id = name;

  // manually unrolled recursion here
  // type config for Builtin && Int
  typet type;
  // Creat a new code_typet, parse the return_type and copy the code_typet to typet
  code_typet convert_type;
  typet return_type;
  if (
    name == "assert" || name == "__ESBMC_assume" || name == "__VERIFIER_assume")
  {
    // clang's assert(.) uses "signed_int" as assert(.) type (NOT the argument type),
    // while Solidity's assert uses "bool" as assert(.) type (NOT the argument type).
    return_type = bool_type();
    std::string c_type = "bool";
    return_type.set("#cpp_type", c_type);
    convert_type.return_type() = return_type;

    if (!convert_type.arguments().size())
      convert_type.make_ellipsis();
  }
  else
  {
    //!assume it's a solidity built-in func
    return get_sol_builtin_ref(decl, new_expr);
  }

  type = convert_type;
  type.set("#sol_name", blt_name);

  new_expr = exprt("symbol", type);
  new_expr.identifier(id);
  new_expr.cmt_lvalue(true);
  new_expr.name(name);

  return false;
}

/*
  check if it's a solidity built-in function
  - if so, get the function definition reference, assign to new_expr and return false  
  - if not, return true
*/
bool solidity_convertert::get_sol_builtin_ref(
  const nlohmann::json expr,
  exprt &new_expr)
{
  // get the reference from the pre-populated symbol table
  // note that this could be either vars or funcs.
  assert(expr.contains("nodeType"));

  if (expr["nodeType"].get<std::string>() == "FunctionCall")
  {
    //  e.g. gasleft() <=> c:@F@gasleft
    if (expr["expression"]["nodeType"].get<std::string>() != "Identifier")
      // this means it's not a builtin funciton
      return true;

    std::string name = expr["expression"]["name"].get<std::string>();
    std::string id = "c:@F@" + name;
    if (context.find_symbol(id) == nullptr)
      return true;
    const symbolt &sym = *context.find_symbol(id);
    new_expr = symbol_expr(sym);
  }
  else if (expr["nodeType"].get<std::string>() == "MemberAccess")
  {
    // e.g. string.concat() <=> c:@string_concat
    std::string bs;
    if (expr["expression"].contains("name"))
      bs = expr["expression"]["name"].get<std::string>();
    else if (
      expr["expression"].contains("typeName") &&
      expr["expression"]["typeName"].contains("name"))
      bs = expr["expression"]["typeName"]["name"].get<std::string>();
    else if (0)
    {
      //TODO：support something like <address>.balance
    }
    else
      return true;

    std::string mem = expr["memberName"].get<std::string>();
    std::string id_var = "c:@" + bs + "_" + mem;
    std::string id_func = "c:@F@" + bs + "_" + mem;
    if (context.find_symbol(id_var) != nullptr)
    {
      symbolt &sym = *context.find_symbol(id_var);

      if (sym.value.is_empty() || sym.value.is_zero())
      {
        // update: set the value to rand (default 0）
        // since all the current support built-in vars are uint type.
        // we just set the value to c:@F@nondet_uint
        symbolt &r = *context.find_symbol("c:@F@nondet_uint");
        sym.value = r.value;
      }
      new_expr = symbol_expr(sym);
    }

    else if (context.find_symbol(id_func) != nullptr)
      new_expr = symbol_expr(*context.find_symbol(id_func));
    else
      return true;
  }
  else
    return true;

  return false;
}

bool solidity_convertert::get_type_description(
  const nlohmann::json &type_name,
  typet &new_type)
{
  // For Solidity rule type-name:
  SolidityGrammar::TypeNameT type = SolidityGrammar::get_type_name_t(type_name);

  switch (type)
  {
  case SolidityGrammar::TypeNameT::ElementaryTypeName:
  {
    // rule state-variable-declaration
    return get_elementary_type_name(type_name, new_type);
  }
  case SolidityGrammar::TypeNameT::ParameterList:
  {
    // rule parameter-list
    // Used for Solidity function parameter or return list
    return get_parameter_list(type_name, new_type);
  }
  case SolidityGrammar::TypeNameT::Pointer:
  {
    // auxiliary type: pointer (FuncToPtr decay)
    // This part is for FunctionToPointer decay only
    assert(
      type_name["typeString"].get<std::string>().find("function") !=
        std::string::npos ||
      type_name["typeString"].get<std::string>().find("contract") !=
        std::string::npos);

    // Since Solidity does not have this, first make a pointee
    nlohmann::json pointee = make_pointee_type(type_name);
    typet sub_type;
    if (get_func_decl_ref_type(pointee, sub_type))
      return true;

    if (sub_type.is_struct() || sub_type.is_union())
      assert(!"struct or union is NOT supported");

    new_type = gen_pointer_type(sub_type);
    break;
  }
  case SolidityGrammar::TypeNameT::PointerArrayToPtr:
  {
    // auxiliary type: pointer (FuncToPtr decay)
    // This part is for FunctionToPointer decay only
    assert(
      type_name["typeIdentifier"].get<std::string>().find("ArrayToPtr") !=
      std::string::npos);

    // Array type descriptor is like:
    //  "typeIdentifier": "ArrayToPtr",
    //  "typeString": "uint8[2] memory"

    // Since Solidity does not have this, first make a pointee
    typet sub_type;
    if (get_array_to_pointer_type(type_name, sub_type))
      return true;

    if (
      sub_type.is_struct() ||
      sub_type.is_union()) // for "assert(sum > 100)", false || false
      assert(!"struct or union is NOT supported");

    new_type = gen_pointer_type(sub_type);
    break;
  }
  case SolidityGrammar::TypeNameT::ArrayTypeName:
  {
    // Deal with array with constant size, e.g., int a[2]; Similar to clang::Type::ConstantArray
    // array's typeDescription is in a compact form, e.g.:
    //    "typeIdentifier": "t_array$_t_uint8_$2_storage_ptr",
    //    "typeString": "uint8[2]"
    // We need to extract the elementary type of array from the information provided above
    // We want to make it like ["baseType"]["typeDescriptions"]
    nlohmann::json array_elementary_type =
      make_array_elementary_type(type_name);
    typet the_type;
    if (get_type_description(array_elementary_type, the_type))
      return true;

    assert(the_type.is_unsignedbv()); // assuming array size is unsigned bv
    std::string the_size = get_array_size(type_name);
    unsigned z_ext_value = std::stoul(the_size, nullptr);
    new_type = array_typet(
      the_type,
      constant_exprt(
        integer2binary(z_ext_value, bv_width(int_type())),
        integer2string(z_ext_value),
        int_type()));

    new_type.set("#sol_type", "ARRAY");
    break;
  }
  case SolidityGrammar::TypeNameT::DynArrayTypeName:
  {
    // Dynamic array in Solidity is complicated. We have
    // 1. dynamic_memory: which will convert to fixed array and
    //    cannot be modified once got allocated. This can be seen
    //    as a fixed array whose length will be set later.
    //    e.g.
    //      uint[] memory data;
    //      data = new uint[](10);
    //    and
    //      uint[] memory data = new uint[](10);
    // 2. dynamic_storage: which can be re-allocated or changed at any time.
    //    e.g.
    //      uint[] data;
    //      func(){ data = [1,2,3]; data = new uint[](10); }
    //    and
    //      data.pop(); data.push();
    //    Idealy, this should be set as a vector_type.
    exprt size_expr;

    if (type_name.contains("sizeExpr"))
    {
      // dynamic memory with initial list

      const nlohmann::json &rtn_expr = type_name["sizeExpr"];
      // wrap it in an ImplicitCastExpr to convert LValue to RValue
      nlohmann::json implicit_cast_expr =
        make_implicit_cast_expr(rtn_expr, "LValueToRValue");

      assert(rtn_expr.contains("typeDescriptions"));
      nlohmann::json l_type = rtn_expr["typeDescriptions"];
      if (get_expr(implicit_cast_expr, l_type, size_expr))
        return true;
      typet subtype;
      nlohmann::json array_elementary_type =
        make_array_elementary_type(type_name);
      if (get_type_description(array_elementary_type, subtype))
        return true;

      new_type = array_typet(subtype, size_expr);
    }
    else
    {
      // e.g.
      // "typeDescriptions": {
      //     "typeIdentifier": "t_array$_t_uint256_$dyn_memory_ptr",
      //     "typeString": "uint256[]"

      // 1. rebuild baseType
      nlohmann::json new_json;
      std::string temp = type_name["typeString"].get<std::string>();
      auto pos = temp.find("[]"); // e.g. "uint256[] memory"
      const std::string typeString = temp.substr(0, pos);
      const std::string typeIdentifier = "t_" + typeString;
      new_json["typeString"] = typeString;
      new_json["typeIdentifier"] = typeIdentifier;

      // 2. get subType
      typet sub_type;
      if (get_type_description(new_json, sub_type))
        return true;

      // 3. make pointer
      new_type = gen_pointer_type(sub_type);
    }

    new_type.set("#sol_type", "DYNARRAY");
    break;
  }
  case SolidityGrammar::TypeNameT::ContractTypeName:
  {
    // e.g. ContractName tmp = new ContractName(Args);

    std::string constructor_name = type_name["typeString"].get<std::string>();
    size_t pos = constructor_name.find(" ");
    std::string id = prefix + constructor_name.substr(pos + 1);

    if (context.find_symbol(id) == nullptr)
      return true;

    const symbolt &s = *context.find_symbol(id);
    new_type = s.type;
    new_type.set("#sol_type", "CONTRACT");
    break;
  }
  case SolidityGrammar::TypeNameT::TypeConversionName:
  {
    // e.g.
    // uint32 a = 0x432178;
    // uint16 b = uint16(a); // b will be 0x2178 now
    // "nodeType": "ElementaryTypeNameExpression",
    //             "src": "155:6:0",
    //             "typeDescriptions": {
    //                 "typeIdentifier": "t_type$_t_uint16_$",
    //                 "typeString": "type(uint16)"
    //             },
    //             "typeName": {
    //                 "id": 10,
    //                 "name": "uint16",
    //                 "nodeType": "ElementaryTypeName",
    //                 "src": "155:6:0",
    //                 "typeDescriptions": {}
    //             }

    nlohmann::json new_json;
    std::string typeIdentifier = type_name["typeIdentifier"].get<std::string>();
    std::string typeString = type_name["typeString"].get<std::string>();

    // convert it back to ElementaryTypeName by removing the "type" prefix
    std::size_t begin = typeIdentifier.find("$_");
    std::size_t end = typeIdentifier.rfind("_$");
    typeIdentifier = typeIdentifier.substr(begin + 2, end - begin - 2);

    begin = typeString.find("type(");
    end = typeString.rfind(")");
    typeString = typeString.substr(begin + 5, end - begin - 5);

    new_json["typeIdentifier"] = typeIdentifier;
    new_json["typeString"] = typeString;

    get_elementary_type_name(new_json, new_type);

    break;
  }
  case SolidityGrammar::TypeNameT::EnumTypeName:
  {
    new_type = enum_type();
    new_type.set("#sol_type", "ENUM");
    break;
  }
  case SolidityGrammar::TypeNameT::StructTypeName:
  {
    // e.g. struct ContractName.StructName
    //   "typeDescriptions": {
    //   "typeIdentifier": "t_struct$_Book_$8_storage",
    //   "typeString": "struct Base.Book storage ref"
    // }

    // extract id and ref_id;
    std::string typeString = type_name["typeString"].get<std::string>();
    std::string delimiter = " ";

    int cnt = 1;
    std::string token;

    // extract the seconde string
    while (cnt >= 0)
    {
      if (typeString.find(delimiter) == std::string::npos)
      {
        token = typeString;
        break;
      }
      size_t pos = typeString.find(delimiter);
      token = typeString.substr(0, pos);
      typeString.erase(0, pos + delimiter.length());
      cnt--;
    }

    const std::string id = prefix + "struct " + token;

    if (context.find_symbol(id) == nullptr)
    {
      // if struct is not parsed, handle the struct first
      // extract the decl ref id
      std::string typeIdentifier =
        type_name["typeIdentifier"].get<std::string>();
      typeIdentifier.replace(
        typeIdentifier.find("t_struct$_"), sizeof("t_struct$_") - 1, "");

      auto pos_1 = typeIdentifier.find("$");
      auto pos_2 = typeIdentifier.find("_storage");

      const int ref_id = stoi(typeIdentifier.substr(pos_1 + 1, pos_2));
      const nlohmann::json struct_base = find_decl_ref(ref_id);

      if (get_struct_class(struct_base))
        return true;
    }

    new_type = symbol_typet(id);
    new_type.set("#sol_type", "STRUCT");
    break;
  }
  case SolidityGrammar::TypeNameT::MappingTypeName:
  {
    // do nothing as it won't be used
    new_type = struct_typet();
    new_type.set("#cpp_type", "void");
    new_type.set("#sol_type", "MAPPING");
    break;
  }
  case SolidityGrammar::TypeNameT::TupleTypeName:
  {
    // do nothing as it won't be used
    new_type = struct_typet();
    new_type.set("#cpp_type", "void");
    new_type.set("#sol_type", "TUPLE");
    break;
  }
  default:
  {
    log_debug(
      "solidity",
      "	@@@ got type name=SolidityGrammar::TypeNameT::{}",
      SolidityGrammar::type_name_to_str(type));
    assert(!"Unimplemented type in rule type-name");
    return true;
  }
  }

  // TODO: More var decl attributes checks:
  //    - Constant
  //    - Volatile
  //    - isRestrict

  return false;
}

bool solidity_convertert::get_func_decl_ref_type(
  const nlohmann::json &decl,
  typet &new_type)
{
  // For FunctionToPointer decay:
  // Get type when we make a function call:
  //  - FunnctionNoProto: x = nondet()
  //  - FunctionProto:    z = add(x, y)
  // Similar to the function get_type_description()
  SolidityGrammar::FunctionDeclRefT type =
    SolidityGrammar::get_func_decl_ref_t(decl);

  switch (type)
  {
  case SolidityGrammar::FunctionDeclRefT::FunctionNoProto:
  {
    code_typet type;

    // Return type
    const nlohmann::json &rtn_type = decl["returnParameters"];

    typet return_type;
    if (get_type_description(rtn_type, return_type))
      return true;

    type.return_type() = return_type;

    if (!type.arguments().size())
      type.make_ellipsis();

    new_type = type;
    break;
  }
  case SolidityGrammar::FunctionDeclRefT::FunctionProto:
  {
    code_typet type;

    // store current state
    const nlohmann::json *old_functionDecl = current_functionDecl;
    const std::string old_functionName = current_functionName;

    // need in get_function_params()
    current_functionName = decl["name"].get<std::string>();
    current_functionDecl = &decl;

    const nlohmann::json &rtn_type = decl["returnParameters"];

    typet return_type;
    if (get_type_description(rtn_type, return_type))
      return true;

    type.return_type() = return_type;
    // convert parameters if the function has them
    // update the typet, since typet contains parameter annotations
    for (const auto &decl : decl["parameters"]["parameters"].items())
    {
      const nlohmann::json &func_param_decl = decl.value();

      code_typet::argumentt param;
      if (get_function_params(func_param_decl, param))
        return true;

      type.arguments().push_back(param);
    }

    current_functionName = old_functionName;
    current_functionDecl = old_functionDecl;

    new_type = type;
    break;
  }
  default:
  {
    log_debug(
      "solidity",
      "	@@@ Got type={}",
      SolidityGrammar::func_decl_ref_to_str(type));
    //TODO: seem to be unnecessary, need investigate
    // assert(!"Unimplemented type in auxiliary type to convert function call");
    return true;
  }
  }

  // TODO: More var decl attributes checks:
  //    - Constant
  //    - Volatile
  //    - isRestrict
  return false;
}

bool solidity_convertert::get_array_to_pointer_type(
  const nlohmann::json &type_descriptor,
  typet &new_type)
{
  // Function to get the base type in ArrayToPointer decay
  //  - unrolled the get_type...
  if (
    type_descriptor["typeString"].get<std::string>().find("uint8") !=
    std::string::npos)
  {
    new_type = unsigned_char_type();
    new_type.set("#cpp_type", "unsigned_char");
  }
  else
    assert(!"Unsupported types in ArrayToPinter decay");

  // TODO: More var decl attributes checks:
  //    - Constant
  //    - Volatile
  //    - isRestrict
  return false;
}

bool solidity_convertert::get_tuple_definition(const nlohmann::json &ast_node)
{
  struct_typet t = struct_typet();

  // get name/id:
  std::string name, id;
  get_tuple_name(ast_node, name, id);

  // get type:
  t.tag("struct " + name);

  // get location
  locationt location_begin;
  get_location_from_decl(ast_node, location_begin);

  // get debug module name
  std::string debug_modulename =
    get_modulename_from_path(location_begin.file().as_string());
  current_fileName = debug_modulename;

  // populate struct type symbol
  symbolt symbol;
  get_default_symbol(symbol, debug_modulename, t, name, id, location_begin);
  symbolt &added_symbol = *move_symbol_to_context(symbol);

  auto &args = ast_node.contains("components")
                 ? ast_node["components"]
                 : ast_node["returnParameters"]["parameters"];

  // populate params
  //TODO: flatten the nested tuple (e.g. ((x,y),z) = (func(),1); )
  size_t counter = 0;
  for (const auto &arg : args.items())
  {
    if (arg.value().is_null())
    {
      ++counter;
      continue;
    }

    struct_typet::componentt comp;

    // manually create a member_name
    // follow the naming rule defined in get_var_decl_name
    assert(!current_contractName.empty());
    const std::string mem_name = "mem" + std::to_string(counter);
    const std::string mem_id = "sol:@C@" + current_contractName + "@" + name +
                               "@" + mem_name + "#" +
                               i2string(ast_node["id"].get<std::int16_t>());

    // get type
    typet mem_type;
    if (get_type_description(arg.value()["typeDescriptions"], mem_type))
      return true;

    // construct comp
    comp.type() = mem_type;
    comp.type().set("#member_name", t.tag());
    comp.identifier(mem_id);
    comp.cmt_lvalue(true);
    comp.name(mem_name);
    comp.pretty_name(mem_name);
    comp.set_access("internal");

    // update struct type component
    t.components().push_back(comp);

    // update cnt
    ++counter;
  }

  t.location() = location_begin;
  added_symbol.type = t;

  return false;
}

bool solidity_convertert::get_tuple_instance(
  const nlohmann::json &ast_node,
  exprt &new_expr)
{
  std::string name, id;
  get_tuple_name(ast_node, name, id);

  if (context.find_symbol(id) == nullptr)
    return true;
  const symbolt &sym = *context.find_symbol(id);

  // get type
  typet t = sym.type;
  t.set("#sol_type", "TUPLE_INSTANCE");
  assert(t.id() == typet::id_struct);

  // get instance name,id
  if (get_tuple_instance_name(ast_node, name, id))
    return true;

  // get location
  locationt location_begin;
  get_location_from_decl(ast_node, location_begin);

  // get debug module name
  std::string debug_modulename =
    get_modulename_from_path(location_begin.file().as_string());
  current_fileName = debug_modulename;

  // populate struct type symbol
  symbolt symbol;
  get_default_symbol(symbol, debug_modulename, t, name, id, location_begin);
  symbolt &added_symbol = *move_symbol_to_context(symbol);

  if (!ast_node.contains("components"))
  {
    // assume it's function return parameter list
    // therefore no initial value
    new_expr = symbol_expr(added_symbol);

    return false;
  }

  // populate initial value
  // e.g. (1,2) ==> Tuple tuple = Tuple(1,2);
  //! since there is no tuple type variable in solidity
  // we can just convert it as inital value instead of assignment
  //? should we set the default value as zero?

  exprt inits = gen_zero(t);
  auto &args = ast_node["components"];

  size_t i = 0;
  size_t j = 0;
  unsigned is = inits.operands().size();
  unsigned as = args.size();
  assert(is <= as);

  while (i < is && j < as)
  {
    if (args.at(j).is_null())
    {
      ++j;
      continue;
    }

    exprt init;
    const nlohmann::json &litera_type = args.at(j)["typeDescriptions"];

    if (get_expr(args.at(j), litera_type, init))
      return true;

    const struct_union_typet::componentt *c =
      &to_struct_type(t).components().at(i);
    typet elem_type = c->type();

    solidity_gen_typecast(ns, init, elem_type);
    inits.operands().at(i) = init;

    // update
    ++i;
    ++j;
  }

  added_symbol.value = inits;
  new_expr = added_symbol.value;
  new_expr.identifier(id);
  return false;
}

void solidity_convertert::get_tuple_name(
  const nlohmann::json &ast_node,
  std::string &name,
  std::string &id)
{
  name = "tuple" + std::to_string(ast_node["id"].get<int>());
  id = prefix + "struct " + name;
}

bool solidity_convertert::get_tuple_instance_name(
  const nlohmann::json &ast_node,
  std::string &name,
  std::string &id)
{
  std::string c_name;
  if (get_current_contract_name(ast_node, c_name))
    return true;
  if (c_name.empty())
    return true;

  name = "tuple_instance" + std::to_string(ast_node["id"].get<int>());
  id = "sol:@C@" + c_name + "@" + name;
  return false;
}

/*
  obtain the corresponding tuple struct instance from the symbol table 
  based on the function definition json
*/
bool solidity_convertert::get_tuple_function_ref(
  const nlohmann::json &ast_node,
  exprt &new_expr)
{
  assert(ast_node.contains("nodeType") && ast_node["nodeType"] == "Identifier");

  std::string c_name;
  if (get_current_contract_name(ast_node, c_name))
    return true;
  if (c_name.empty())
    return true;

  std::string name =
    "tuple_instance" +
    std::to_string(ast_node["referencedDeclaration"].get<int>());
  std::string id = "sol:@C@" + c_name + "@" + name;

  if (context.find_symbol(id) == nullptr)
    return true;

  new_expr = symbol_expr(*context.find_symbol(id));
  return false;
}

// Knowing that there is a component x in the struct_tuple A, we construct A.x
bool solidity_convertert::get_tuple_member_call(
  const irep_idt instance_id,
  const exprt &comp,
  exprt &new_expr)
{
  // tuple_instance
  assert(!instance_id.empty());
  exprt base;
  if (context.find_symbol(instance_id) == nullptr)
    return true;

  base = symbol_expr(*context.find_symbol(instance_id));
  new_expr = member_exprt(base, comp.name(), comp.type());
  return false;
}

void solidity_convertert::get_tuple_function_call(
  code_blockt &_block,
  const exprt &op)
{
  assert(op.id() == "sideeffect");
  exprt func_call = op;
  convert_expression_to_code(func_call);
  _block.move_to_operands(func_call);
}

void solidity_convertert::get_tuple_assignment(
  code_blockt &_block,
  const exprt &lop,
  exprt rop)
{
  exprt assign_expr = side_effect_exprt("assign", lop.type());
  assign_expr.copy_to_operands(lop, rop);

  convert_expression_to_code(assign_expr);
  _block.move_to_operands(assign_expr);
}

// get the value type of the mapping
bool solidity_convertert::get_mapping_value_type(
  const typet &val_type,
  std::string &_val)
{
  /*
    _ValueType can be any type, including mappings, arrays and structs
  */
  std::string sol_type = val_type.get("#sol_type").as_string();
  if (sol_type == "MAPPING")
  {
    log_error("Unsupported nested mapping");
    return true;
  }
  else if (sol_type.compare(0, 3, "INT") == 0 || sol_type == "ENUM")
    _val = "int";
  else if (
    sol_type.compare(0, 4, "UINT") == 0 ||
    sol_type.compare(0, 5, "BYTES") == 0 || sol_type == "ADDRESS")
    _val = "uint";
  else if (sol_type == "STRING")
    _val = "string";
  else if (sol_type == "BOOL")
    _val = "bool";
  else if (
    sol_type == "ARRAY" || sol_type == "DYNARRAY" || sol_type == "STRUCT" ||
    sol_type == "CONTRACT")
  {
    log_error("Unsupported array-type mapping");
    return true;
  }
  return false;
}

bool solidity_convertert::get_mapping_key(
  const nlohmann::json &ast_node,
  exprt &new_expr)
{
  /*
    The _KeyType can be any built-in value type, bytes, string, or any contract or enum type.
    Other user-defined or complex types, such as mappings, structs or array types are not allowed.
  */
  exprt idx;
  if (get_expr(ast_node, ast_node["typeDescriptions"], idx))
    return true;

  //TODO: not including contract since the contract type is not supported
  if (
    idx.type().id() == irept::id_signedbv ||
    idx.type().id() == irept::id_unsignedbv ||
    idx.type().id() == irept::id_bool)
  {
    // int, enum
    // uint, address, bytes
    // bool

    // convert int/uint to string via i256toa/u256toa
    std::string func_id, func_name;
    typet type_cast;
    if (idx.type().id() == irept::id_signedbv)
    {
      func_name = "i256toa";
      func_id = "c:@F@i256toa";
      type_cast = signedbv_typet(256);
    }
    else
    {
      func_name = "u256toa";
      func_id = "c:@F@u256toa";
      type_cast = unsignedbv_typet(256);
    }
    const auto &sym = *context.find_symbol(func_id);

    locationt l;
    get_location_from_decl(ast_node, l);

    side_effect_expr_function_callt call_expr;
    get_library_function_call(func_name, func_id, sym.type, l, call_expr);

    // gen typecast (necessary?)
    solidity_gen_typecast(ns, idx, type_cast);

    // pass arg
    call_expr.arguments().push_back(idx);
    new_expr = call_expr;
  }
  else if (idx.type().id() == irept::id_array)
  {
    // string
    new_expr = idx;
  }
  else
  {
    log_error(
      "Unexpected mapping index type, got {}", idx.type().id().as_string());
    return true;
  }
  return false;
}

/*
  This function converts
    mapping(string => int) m;
  to
  {
    map_int_t m;
    map_init_int(&m);
  }
*/
bool solidity_convertert::get_mapping_definition(
  const nlohmann::json &ast_node,
  exprt &new_expr)
{
  // get type
  const auto &val_node = ast_node["typeName"]["valueType"];
  typet val_type;
  if (get_type_description(val_node["typeDescriptions"], val_type))
    return true;

  std::string _val;
  if (get_mapping_value_type(val_type, _val))
    return true;

  std::string struct_name = "struct map_" + _val + "_t";
  std::string struct_id = prefix + struct_name; // e.g. tag-struct map_str_t
  typet t = symbol_typet(struct_id);

  // get name, id
  std::string name, id;
  bool is_state_var = ast_node["stateVariable"] == true;
  if (is_state_var)
    get_state_var_decl_name(ast_node, name, id);
  else if (current_functionDecl)
  {
    assert(current_functionName != "");
    get_var_decl_name(ast_node, name, id);
  }
  else
  {
    log_error("ESBMC could not find the parent scope for this local variable");
    return true;
  }

  // get location
  locationt location_begin;
  get_location_from_decl(ast_node, location_begin);

  // get debug module name
  std::string debug_modulename =
    get_modulename_from_path(location_begin.file().as_string());

  // populate symbool
  symbolt symbol;
  get_default_symbol(symbol, debug_modulename, t, name, id, location_begin);
  symbol.is_extern = false;

  symbolt &added_symbol = *move_symbol_to_context(symbol);
  exprt mapping_ins = symbol_expr(added_symbol);

  // get init
  // e.g. map_init_int(&m);
  std::string func_name = "map_init_" + _val;
  std::string func_id = "c:@F@map_init_" + _val;

  side_effect_expr_function_callt call_expr;
  locationt l;
  get_location_from_decl(ast_node["typeName"], l);

  if (context.find_symbol(func_id) == nullptr)
    return true;

  const auto &s = *context.find_symbol(func_id);
  get_library_function_call(func_name, func_id, s.type, l, call_expr);

  // get address: &m
  exprt address_of = address_of_exprt(mapping_ins);
  call_expr.arguments().push_back(mapping_ins);

  // get block
  code_blockt _block;
  convert_expression_to_code(mapping_ins);
  convert_expression_to_code(call_expr);
  _block.move_to_operands(mapping_ins, call_expr);

  new_expr = _block;
  return false;
}

bool solidity_convertert::move_mapping_to_ctor()
{
  // no mapping
  if (map_init_block.is_empty())
    return false;

  // get ctor
  std::string ctor_id = get_ctor_call_id(current_contractName);
  if (context.find_symbol(ctor_id) == nullptr)
    return true;
  symbolt &ctor = *context.find_symbol(ctor_id);

  if (ctor.value.is_empty())
  {
    // empty or implicit ctor
    ctor.value = map_init_block;
  }
  else
  {
    // move to operands (insert in the front)
    for (auto &op : ctor.value.operands())
      map_init_block.operands().push_back(op);
    ctor.value.operands() = map_init_block.operands();
  }
  return false;
}

// invoking a function in the library
// note that the function symbol might not be inside the symbol table at the moment
void solidity_convertert::get_library_function_call(
  const std::string &func_name,
  const std::string &func_id,
  const typet &t,
  const locationt &l,
  exprt &new_expr)
{
  side_effect_expr_function_callt call_expr;

  exprt type_expr("symbol");
  type_expr.name(func_name);
  type_expr.identifier(func_id);
  type_expr.location() = l;

  code_typet type;
  if (t.is_code())
    // this means it's a func symbol read from the symbol_table
    type_expr.type() = to_code_type(t);
  else
  {
    type.return_type() = t;
    type_expr.type() = type;
  }

  call_expr.function() = type_expr;
  if (t.is_code())
    call_expr.type() = to_code_type(t).return_type();
  else
    call_expr.type() = t;

  new_expr = call_expr;
}

/**
     * @brief Populate the out `typet` parameter with the uint type specified by type parameter
     *
     * @param type The type of the uint to be poulated
     * @param out The variable that holds the resulting type
     * @return true iff population failed
     * @return false iff population was successful
     */
bool solidity_convertert::get_elementary_type_name_uint(
  SolidityGrammar::ElementaryTypeNameT &type,
  typet &out)
{
  const unsigned int uint_size = SolidityGrammar::uint_type_name_to_size(type);
  out = unsignedbv_typet(uint_size);

  return false;
}

/**
     * @brief Populate the out `typet` parameter with the int type specified by type parameter
     *
     * @param type The type of the int to be poulated
     * @param out The variable that holds the resulting type
     * @return false iff population was successful
     */
bool solidity_convertert::get_elementary_type_name_int(
  SolidityGrammar::ElementaryTypeNameT &type,
  typet &out)
{
  const unsigned int int_size = SolidityGrammar::int_type_name_to_size(type);
  out = signedbv_typet(int_size);

  return false;
}

bool solidity_convertert::get_elementary_type_name_bytesn(
  SolidityGrammar::ElementaryTypeNameT &type,
  typet &out)
{
  /*
    bytes1 has size of 8 bits (possible values 0x00 to 0xff), 
    which you can implicitly convert to uint8 (unsigned integer of size 8 bits) but not to int8
  */
  const unsigned int byte_num = SolidityGrammar::bytesn_type_name_to_size(type);
  out = unsignedbv_typet(byte_num * 8);

  return false;
}

bool solidity_convertert::get_elementary_type_name(
  const nlohmann::json &type_name,
  typet &new_type)
{
  // For Solidity rule elementary-type-name:
  // equivalent to clang's get_builtin_type()
  std::string c_type;
  SolidityGrammar::ElementaryTypeNameT type =
    SolidityGrammar::get_elementary_type_name_t(type_name);

  log_debug(
    "solidity",
    "	@@@ got ElementaryType: SolidityGrammar::ElementaryTypeNameT::{}",
    fmt::underlying(type));

  switch (type)
  {
  // rule unsigned-integer-type
  case SolidityGrammar::ElementaryTypeNameT::UINT8:
  case SolidityGrammar::ElementaryTypeNameT::UINT16:
  case SolidityGrammar::ElementaryTypeNameT::UINT24:
  case SolidityGrammar::ElementaryTypeNameT::UINT32:
  case SolidityGrammar::ElementaryTypeNameT::UINT40:
  case SolidityGrammar::ElementaryTypeNameT::UINT48:
  case SolidityGrammar::ElementaryTypeNameT::UINT56:
  case SolidityGrammar::ElementaryTypeNameT::UINT64:
  case SolidityGrammar::ElementaryTypeNameT::UINT72:
  case SolidityGrammar::ElementaryTypeNameT::UINT80:
  case SolidityGrammar::ElementaryTypeNameT::UINT88:
  case SolidityGrammar::ElementaryTypeNameT::UINT96:
  case SolidityGrammar::ElementaryTypeNameT::UINT104:
  case SolidityGrammar::ElementaryTypeNameT::UINT112:
  case SolidityGrammar::ElementaryTypeNameT::UINT120:
  case SolidityGrammar::ElementaryTypeNameT::UINT128:
  case SolidityGrammar::ElementaryTypeNameT::UINT136:
  case SolidityGrammar::ElementaryTypeNameT::UINT144:
  case SolidityGrammar::ElementaryTypeNameT::UINT152:
  case SolidityGrammar::ElementaryTypeNameT::UINT160:
  case SolidityGrammar::ElementaryTypeNameT::UINT168:
  case SolidityGrammar::ElementaryTypeNameT::UINT176:
  case SolidityGrammar::ElementaryTypeNameT::UINT184:
  case SolidityGrammar::ElementaryTypeNameT::UINT192:
  case SolidityGrammar::ElementaryTypeNameT::UINT200:
  case SolidityGrammar::ElementaryTypeNameT::UINT208:
  case SolidityGrammar::ElementaryTypeNameT::UINT216:
  case SolidityGrammar::ElementaryTypeNameT::UINT224:
  case SolidityGrammar::ElementaryTypeNameT::UINT232:
  case SolidityGrammar::ElementaryTypeNameT::UINT240:
  case SolidityGrammar::ElementaryTypeNameT::UINT248:
  case SolidityGrammar::ElementaryTypeNameT::UINT256:
  {
    if (get_elementary_type_name_uint(type, new_type))
      return true;

    new_type.set("#sol_type", elementary_type_name_to_str(type));
    break;
  }
  case SolidityGrammar::ElementaryTypeNameT::INT8:
  case SolidityGrammar::ElementaryTypeNameT::INT16:
  case SolidityGrammar::ElementaryTypeNameT::INT24:
  case SolidityGrammar::ElementaryTypeNameT::INT32:
  case SolidityGrammar::ElementaryTypeNameT::INT40:
  case SolidityGrammar::ElementaryTypeNameT::INT48:
  case SolidityGrammar::ElementaryTypeNameT::INT56:
  case SolidityGrammar::ElementaryTypeNameT::INT64:
  case SolidityGrammar::ElementaryTypeNameT::INT72:
  case SolidityGrammar::ElementaryTypeNameT::INT80:
  case SolidityGrammar::ElementaryTypeNameT::INT88:
  case SolidityGrammar::ElementaryTypeNameT::INT96:
  case SolidityGrammar::ElementaryTypeNameT::INT104:
  case SolidityGrammar::ElementaryTypeNameT::INT112:
  case SolidityGrammar::ElementaryTypeNameT::INT120:
  case SolidityGrammar::ElementaryTypeNameT::INT128:
  case SolidityGrammar::ElementaryTypeNameT::INT136:
  case SolidityGrammar::ElementaryTypeNameT::INT144:
  case SolidityGrammar::ElementaryTypeNameT::INT152:
  case SolidityGrammar::ElementaryTypeNameT::INT160:
  case SolidityGrammar::ElementaryTypeNameT::INT168:
  case SolidityGrammar::ElementaryTypeNameT::INT176:
  case SolidityGrammar::ElementaryTypeNameT::INT184:
  case SolidityGrammar::ElementaryTypeNameT::INT192:
  case SolidityGrammar::ElementaryTypeNameT::INT200:
  case SolidityGrammar::ElementaryTypeNameT::INT208:
  case SolidityGrammar::ElementaryTypeNameT::INT216:
  case SolidityGrammar::ElementaryTypeNameT::INT224:
  case SolidityGrammar::ElementaryTypeNameT::INT232:
  case SolidityGrammar::ElementaryTypeNameT::INT240:
  case SolidityGrammar::ElementaryTypeNameT::INT248:
  case SolidityGrammar::ElementaryTypeNameT::INT256:
  {
    if (get_elementary_type_name_int(type, new_type))
      return true;

    new_type.set("#sol_type", elementary_type_name_to_str(type));
    break;
  }
  case SolidityGrammar::ElementaryTypeNameT::INT_LITERAL:
  {
    // for int_const type
    new_type = signedbv_typet(256);
    new_type.set("#cpp_type", "signed_char");
    new_type.set("#sol_type", "INT_CONST");
    break;
  }
  case SolidityGrammar::ElementaryTypeNameT::BOOL:
  {
    new_type = bool_type();
    new_type.set("#cpp_type", "bool");
    new_type.set("#sol_type", "BOOL");
    break;
  }
  case SolidityGrammar::ElementaryTypeNameT::STRING:
  {
    size_t value_length = 128;

    new_type = array_typet(
      signed_char_type(),
      constant_exprt(
        integer2binary(value_length, bv_width(int_type())),
        integer2string(value_length),
        int_type()));
    new_type.set("#sol_type", "STRING");
    break;
  }
  case SolidityGrammar::ElementaryTypeNameT::ADDRESS:
  case SolidityGrammar::ElementaryTypeNameT::ADDRESS_PAYABLE:
  {
    //  An Address is a DataHexString of 20 bytes (uint160)
    // e.g. 0x1f9840a85d5aF5bf1D1762F925BDADdC4201F984
    // ops: <=, <, ==, !=, >= and >

    new_type = unsignedbv_typet(160);

    // for type conversion
    new_type.set("#sol_type", elementary_type_name_to_str(type));
    new_type.set("#sol_type", "ADDRESS");
    break;
  }
  case SolidityGrammar::ElementaryTypeNameT::BYTES1:
  case SolidityGrammar::ElementaryTypeNameT::BYTES2:
  case SolidityGrammar::ElementaryTypeNameT::BYTES3:
  case SolidityGrammar::ElementaryTypeNameT::BYTES4:
  case SolidityGrammar::ElementaryTypeNameT::BYTES5:
  case SolidityGrammar::ElementaryTypeNameT::BYTES6:
  case SolidityGrammar::ElementaryTypeNameT::BYTES7:
  case SolidityGrammar::ElementaryTypeNameT::BYTES8:
  case SolidityGrammar::ElementaryTypeNameT::BYTES9:
  case SolidityGrammar::ElementaryTypeNameT::BYTES10:
  case SolidityGrammar::ElementaryTypeNameT::BYTES11:
  case SolidityGrammar::ElementaryTypeNameT::BYTES12:
  case SolidityGrammar::ElementaryTypeNameT::BYTES13:
  case SolidityGrammar::ElementaryTypeNameT::BYTES14:
  case SolidityGrammar::ElementaryTypeNameT::BYTES15:
  case SolidityGrammar::ElementaryTypeNameT::BYTES16:
  case SolidityGrammar::ElementaryTypeNameT::BYTES17:
  case SolidityGrammar::ElementaryTypeNameT::BYTES18:
  case SolidityGrammar::ElementaryTypeNameT::BYTES19:
  case SolidityGrammar::ElementaryTypeNameT::BYTES20:
  case SolidityGrammar::ElementaryTypeNameT::BYTES21:
  case SolidityGrammar::ElementaryTypeNameT::BYTES22:
  case SolidityGrammar::ElementaryTypeNameT::BYTES23:
  case SolidityGrammar::ElementaryTypeNameT::BYTES24:
  case SolidityGrammar::ElementaryTypeNameT::BYTES25:
  case SolidityGrammar::ElementaryTypeNameT::BYTES26:
  case SolidityGrammar::ElementaryTypeNameT::BYTES27:
  case SolidityGrammar::ElementaryTypeNameT::BYTES28:
  case SolidityGrammar::ElementaryTypeNameT::BYTES29:
  case SolidityGrammar::ElementaryTypeNameT::BYTES30:
  case SolidityGrammar::ElementaryTypeNameT::BYTES31:
  case SolidityGrammar::ElementaryTypeNameT::BYTES32:
  {
    if (get_elementary_type_name_bytesn(type, new_type))
      return true;

    // for type conversion
    new_type.set("#sol_type", elementary_type_name_to_str(type));
    new_type.set("#sol_bytes_size", bytesn_type_name_to_size(type));

    break;
  }
  case SolidityGrammar::ElementaryTypeNameT::BYTES:
  {
    new_type = unsignedbv_typet(256);
    new_type.set("#sol_type", elementary_type_name_to_str(type));

    break;
  }
  default:
  {
    log_debug(
      "solidity",
      "	@@@ Got elementary-type-name={}",
      SolidityGrammar::elementary_type_name_to_str(type));
    assert(!"Unimplemented type in rule elementary-type-name");
    return true;
  }
  }

  return false;
}

bool solidity_convertert::get_parameter_list(
  const nlohmann::json &type_name,
  typet &new_type)
{
  // For Solidity rule parameter-list:
  //  - For non-empty param list, it may need to call get_elementary_type_name, since parameter-list is just a list of types
  std::string c_type;
  SolidityGrammar::ParameterListT type =
    SolidityGrammar::get_parameter_list_t(type_name);

  switch (type)
  {
  case SolidityGrammar::ParameterListT::EMPTY:
  {
    // equivalent to clang's "void"
    new_type = empty_typet();
    c_type = "void";
    new_type.set("#cpp_type", c_type);
    break;
  }
  case SolidityGrammar::ParameterListT::ONE_PARAM:
  {
    assert(
      type_name["parameters"].size() ==
      1); // TODO: Fix me! assuming one return parameter
    const nlohmann::json &rtn_type =
      type_name["parameters"].at(0)["typeDescriptions"];
    return get_type_description(rtn_type, new_type);

    break;
  }
  case SolidityGrammar::ParameterListT::MORE_THAN_ONE_PARAM:
  {
    // if contains multiple return types
    // We will return null because we create the symbols of the struct accordingly
    assert(type_name["parameters"].size() > 1);
    new_type = empty_typet();
    new_type.set("#cpp_type", "void");
    new_type.set("#sol_type", "TUPLE");
    break;
  }
  default:
  {
    assert(!"Unimplemented type in rule parameter-list");
    return true;
  }
  }

  return false;
}

// parse the state variable
void solidity_convertert::get_state_var_decl_name(
  const nlohmann::json &ast_node,
  std::string &name,
  std::string &id)
{
  // Follow the way in clang:
  //  - For state variable name, just use the ast_node["name"]
  //  - For state variable id, add prefix "sol:@"
  std::string contract_name;
  if (get_current_contract_name(ast_node, contract_name))
  {
    log_error("Internal error when obtaining the contract name. Aborting...");
    abort();
  }
  name =
    ast_node["name"]
      .get<
        std::
          string>(); // assume Solidity AST json object has "name" field, otherwise throws an exception in nlohmann::json

  // e.g. sol:@C@Base@x#11
  // The prefix is used to avoid duplicate names
  if (!contract_name.empty())
    id = "sol:@C@" + contract_name + "@" + name + "#" +
         i2string(ast_node["id"].get<std::int16_t>());
  else
    id = "sol:@" + name + "#" + i2string(ast_node["id"].get<std::int16_t>());
}

// parse the non-state variable
void solidity_convertert::get_var_decl_name(
  const nlohmann::json &ast_node,
  std::string &name,
  std::string &id)
{
  std::string contract_name;
  if (get_current_contract_name(ast_node, contract_name))
  {
    log_error("Internal error when obtaining the contract name. Aborting...");
    abort();
  }

  name =
    ast_node["name"]
      .get<
        std::
          string>(); // assume Solidity AST json object has "name" field, otherwise throws an exception in nlohmann::json

  assert(ast_node.contains("id"));
  if (
    current_functionDecl && !contract_name.empty() &&
    !current_functionName.empty())
  {
    // converting local variable inside a function
    // For non-state functions, we give it different id.
    // E.g. for local variable i in function nondet(), it's "sol:@C@Base@F@nondet@i#55".

    // As the local variable inside the function will not be inherited, we can use current_functionName
    id = "sol:@C@" + contract_name + "@F@" + current_functionName + "@" + name +
         "#" + i2string(ast_node["id"].get<std::int16_t>());
  }
  else if (ast_node.contains("scope"))
  {
    // This means we are handling a local variable which is not inside a function body.
    //! Assume it is a variable inside struct/error
    int scp = ast_node["scope"].get<int>();
    if (scope_map.count(scp) == 0)
    {
      log_error("cannot find struct/error name");
      abort();
    }
    std::string struct_name = scope_map.at(scp);
    if (contract_name.empty())
      id = "sol:@" + struct_name + "@" + name + "#" +
           i2string(ast_node["id"].get<std::int16_t>());
    else
      id = "sol:@C@" + contract_name + "@" + struct_name + "@" + name + "#" +
           i2string(ast_node["id"].get<std::int16_t>());
  }
  else
  {
    log_error("Unsupported local variable");
    abort();
  }
}

void solidity_convertert::get_function_definition_name(
  const nlohmann::json &ast_node,
  std::string &name,
  std::string &id)
{
  // Follow the way in clang:
  //  - For function name, just use the ast_node["name"]
  // assume Solidity AST json object has "name" field, otherwise throws an exception in nlohmann::json
  std::string contract_name;

  if (get_current_contract_name(ast_node, contract_name))
  {
    log_error("Internal error when obtaining the contract name. Aborting...");
    abort();
  }

  if (
    ast_node.contains("kind") &&
    ast_node["kind"].get<std::string>() == "constructor")
  {
    name = contract_name;
    // constructors cannot be overridden, primarily because they don't have names
    // to align with the implicit constructor, we do not add the 'id'
    id = "sol:@C@" + contract_name + "@F@" + name + "#";
  }
  else
  {
    name = ast_node["name"].get<std::string>();
    id = "sol:@C@" + contract_name + "@F@" + name + "#" +
         i2string(ast_node["id"].get<std::int16_t>());
  }
}

unsigned int solidity_convertert::add_offset(
  const std::string &src,
  unsigned int start_position)
{
  // extract the length from "start:length:index"
  std::string offset = src.substr(1, src.find(":"));
  // already added 1 in start_position
  unsigned int end_position = start_position + std::stoul(offset);
  return end_position;
}

std::string
solidity_convertert::get_ctor_call_id(const std::string &contract_name)
{
  return "sol:@C@" + contract_name + "@F@" + contract_name + "#";
}

std::string
solidity_convertert::get_src_from_json(const nlohmann::json &ast_node)
{
  // some nodes may have "src" inside a member json object
  // we need to deal with them case by case based on the node type
  SolidityGrammar::ExpressionT type =
    SolidityGrammar::get_expression_t(ast_node);
  switch (type)
  {
  case SolidityGrammar::ExpressionT::ImplicitCastExprClass:
  {
    assert(ast_node.contains("subExpr"));
    assert(ast_node["subExpr"].contains("src"));
    return ast_node["subExpr"]["src"].get<std::string>();
  }
  case SolidityGrammar::ExpressionT::NullExpr:
  {
    // empty address
    return "-1:-1:-1";
  }
  default:
  {
    assert(!"Unsupported node type when getting src from JSON");
    abort();
  }
  }
}

unsigned int solidity_convertert::get_line_number(
  const nlohmann::json &ast_node,
  bool final_position)
{
  // Solidity src means "start:length:index", where "start" represents the position of the first char byte of the identifier.
  std::string src = ast_node.contains("src")
                      ? ast_node["src"].get<std::string>()
                      : get_src_from_json(ast_node);

  std::string position = src.substr(0, src.find(":"));
  unsigned int byte_position = std::stoul(position) + 1;

  if (final_position)
    byte_position = add_offset(src, byte_position);

  // the line number can be calculated by counting the number of line breaks prior to the identifier.
  unsigned int loc = std::count(
                       contract_contents.begin(),
                       (contract_contents.begin() + byte_position),
                       '\n') +
                     1;
  return loc;
}

void solidity_convertert::get_location_from_decl(
  const nlohmann::json &ast_node,
  locationt &location)
{
  location.set_line(get_line_number(ast_node));
  location.set_file(
    absolute_path); // assume absolute_path is the name of the contrace file, since we ran solc in the same directory

  // To annotate local declaration within a function
  if (
    ast_node["nodeType"] == "VariableDeclaration" &&
    ast_node["stateVariable"] == false)
  {
    assert(
      current_functionDecl); // must have a valid current function declaration
    location.set_function(
      current_functionName); // set the function where this local variable belongs to
  }
}

void solidity_convertert::get_start_location_from_stmt(
  const nlohmann::json &ast_node,
  locationt &location)
{
  std::string function_name;

  if (current_functionDecl)
    function_name = current_functionName;

  // The src manager of Solidity AST JSON is too encryptic.
  // For the time being we are setting it to "1".
  location.set_line(get_line_number(ast_node));
  location.set_file(
    absolute_path); // assume absolute_path is the name of the contrace file, since we ran solc in the same directory

  if (!function_name.empty())
    location.set_function(function_name);
}

void solidity_convertert::get_final_location_from_stmt(
  const nlohmann::json &ast_node,
  locationt &location)
{
  std::string function_name;

  if (current_functionDecl)
    function_name = current_functionName;

  // The src manager of Solidity AST JSON is too encryptic.
  // For the time being we are setting it to "1".
  location.set_line(get_line_number(ast_node, true));
  location.set_file(
    absolute_path); // assume absolute_path is the name of the contrace file, since we ran solc in the same directory

  if (!function_name.empty())
    location.set_function(function_name);
}

std::string solidity_convertert::get_modulename_from_path(std::string path)
{
  std::string filename = get_filename_from_path(path);

  if (filename.find_last_of('.') != std::string::npos)
    return filename.substr(0, filename.find_last_of('.'));

  return filename;
}

std::string solidity_convertert::get_filename_from_path(std::string path)
{
  if (path.find_last_of('/') != std::string::npos)
    return path.substr(path.find_last_of('/') + 1);

  return path; // for _x, it just returns "overflow_2.c" because the test program is in the same dir as esbmc binary
}

// A wrapper to obtain additional contract_name info
const nlohmann::json &solidity_convertert::find_decl_ref(int ref_decl_id)
{
  // An empty contract name means that the node outside any contracts.
  std::string empty_contract_name = "";
  return find_decl_ref(ref_decl_id, empty_contract_name);
}

const nlohmann::json &
solidity_convertert::find_decl_ref(int ref_decl_id, std::string &contract_name)
{
  //TODO: Clean up this funciton. Such a mess...

  if (ref_decl_id < 0)
  {
    log_warning("Cannot find declaration reference for the built-in function.");
    abort();
  }

  // First, search state variable nodes
  nlohmann::json &nodes = src_ast_json["nodes"];
  unsigned index = 0;
  for (nlohmann::json::iterator itr = nodes.begin(); itr != nodes.end();
       ++itr, ++index)
  {
    // check the nodes outside of the contract
    // it can be referred to the data structure
    // or the members inside the structure.
    if ((*itr)["id"] == ref_decl_id)
      return nodes.at(index);

    if (
      (*itr)["nodeType"] == "EnumDefinition" ||
      (*itr)["nodeType"] == "StructDefinition")
    {
      unsigned men_idx = 0;
      nlohmann::json &mem_nodes = nodes.at(index)["members"];
      for (nlohmann::json::iterator mem_itr = mem_nodes.begin();
           mem_itr != mem_nodes.end();
           ++mem_itr, ++men_idx)
      {
        if ((*mem_itr)["id"] == ref_decl_id)
          return mem_nodes.at(men_idx);
      }
    }

    if ((*itr)["nodeType"] == "ErrorDefinition")
    {
      if (
        (*itr).contains("parameters") &&
        ((*itr)["parameters"]).contains("parameters"))
      {
        unsigned men_idx = 0;
        nlohmann::json &mem_nodes = (*itr)["parameters"]["parameters"];
        for (nlohmann::json::iterator mem_itr = mem_nodes.begin();
             mem_itr != mem_nodes.end();
             ++mem_itr, ++men_idx)
        {
          if ((*mem_itr)["id"] == ref_decl_id)
            return mem_nodes.at(men_idx);
        }
      }
    }

    // check the nodes inside a contract
    if ((*itr)["nodeType"] == "ContractDefinition")
    {
      // update the contract name first
      contract_name = (*itr)["name"].get<std::string>();

      nlohmann::json &ast_nodes = nodes.at(index)["nodes"];
      unsigned idx = 0;
      for (nlohmann::json::iterator itrr = ast_nodes.begin();
           itrr != ast_nodes.end();
           ++itrr, ++idx)
      {
        if ((*itrr)["id"] == ref_decl_id)
          return ast_nodes.at(idx);

        if (
          (*itrr)["nodeType"] == "EnumDefinition" ||
          (*itrr)["nodeType"] == "StructDefinition")
        {
          unsigned men_idx = 0;
          nlohmann::json &mem_nodes = ast_nodes.at(idx)["members"];

          for (nlohmann::json::iterator mem_itr = mem_nodes.begin();
               mem_itr != mem_nodes.end();
               ++mem_itr, ++men_idx)
          {
            if ((*mem_itr)["id"] == ref_decl_id)
              return mem_nodes.at(men_idx);
          }
        }

        if ((*itr)["nodeType"] == "ErrorDefinition")
        {
          if (
            (*itr).contains("parameters") &&
            ((*itr)["parameters"]).contains("parameters"))
          {
            unsigned men_idx = 0;
            nlohmann::json &mem_nodes = (*itr)["parameters"]["parameters"];
            for (nlohmann::json::iterator mem_itr = mem_nodes.begin();
                 mem_itr != mem_nodes.end();
                 ++mem_itr, ++men_idx)
            {
              if ((*mem_itr)["id"] == ref_decl_id)
                return mem_nodes.at(men_idx);
            }
          }
        }
      }
    }
  }

  //! otherwise, assume it is current_contractName
  contract_name = current_contractName;

  if (current_functionDecl != nullptr)
  {
    // Then search "declarations" in current function scope
    const nlohmann::json &current_func = *current_functionDecl;
    if (!current_func.contains("body"))
    {
      //! assume it's an event-definition
      if (
        current_func.contains("nodeType") &&
        current_func["nodeType"] == "EventDefinition")
      {
        return current_func;
      }
      log_error(
        "Unable to find the corresponding local variable decl. Current "
        "function "
        "does not have a function body.");
      abort();
    }

    // var declaration in local statements
    // bfs visit
    std::queue<const nlohmann::json *> body_stmts;
    body_stmts.emplace(&current_func["body"]);

    while (!body_stmts.empty())
    {
      const nlohmann::json &top_stmt = *(body_stmts).front();
      for (const auto &body_stmt : top_stmt["statements"].items())
      {
        const nlohmann::json &stmt = body_stmt.value();
        if (stmt["nodeType"] == "VariableDeclarationStatement")
        {
          for (const auto &local_decl : stmt["declarations"].items())
          {
            const nlohmann::json &the_decl = local_decl.value();
            if (the_decl["id"] == ref_decl_id)
            {
              assert(the_decl.contains("nodeType"));
              return the_decl;
            }
          }
        }

        // nested block e.g.
        // {
        //    {
        //        int x = 1;
        //    }
        // }
        if (stmt["nodeType"] == "Block" && stmt.contains("statements"))
          body_stmts.emplace(&stmt);
      }

      body_stmts.pop();
    }

    // Search function parameter
    if (current_func.contains("parameters"))
    {
      if (current_func["parameters"]["parameters"].size())
      {
        // var decl in function parameter array
        for (const auto &param_decl :
             current_func["parameters"]["parameters"].items())
        {
          const nlohmann::json &param = param_decl.value();
          assert(param["nodeType"] == "VariableDeclaration");
          if (param["id"] == ref_decl_id)
            return param;
        }
      }
    }
  }

  // if no matching state or local var decl, search decl in current_forStmt
  if (current_forStmt != nullptr)
  {
    const nlohmann::json &current_for = *current_forStmt;
    if (current_for.contains("initializationExpression"))
    {
      if (current_for["initializationExpression"].contains("declarations"))
      {
        assert(current_for["initializationExpression"]["declarations"]
                 .size()); // Assuming a non-empty declaration array
        const nlohmann::json &decls =
          current_for["initializationExpression"]["declarations"];
        for (const auto &init_decl : decls.items())
        {
          const nlohmann::json &the_decl = init_decl.value();
          if (the_decl["id"] == ref_decl_id)
            return the_decl;
        }
      }
      else
        assert(!"Unable to find the corresponding local variable decl. No local declarations found in current For-Statement");
    }
    else
      assert(!"Unable to find the corresponding local variable decl. Current For-Statement does not have any init.");
  }

  // Instead of reporting errors here, we return an empty json
  // and leave the error handling to the caller.
  return empty_json;
}

// return construcor node
const nlohmann::json &solidity_convertert::find_constructor_ref(int ref_decl_id)
{
  nlohmann::json &nodes = src_ast_json["nodes"];
  unsigned index = 0;
  for (nlohmann::json::iterator itr = nodes.begin(); itr != nodes.end();
       ++itr, ++index)
  {
    if (
      (*itr)["id"].get<int>() == ref_decl_id &&
      (*itr)["nodeType"] == "ContractDefinition")
    {
      nlohmann::json &ast_nodes = nodes.at(index)["nodes"];
      unsigned idx = 0;
      for (nlohmann::json::iterator ittr = ast_nodes.begin();
           ittr != ast_nodes.end();
           ++ittr, ++idx)
      {
        if ((*ittr)["kind"] == "constructor")
        {
          return ast_nodes.at(idx);
        }
      }
    }
  }

  // implicit constructor call
  return empty_json;
}

void solidity_convertert::get_default_symbol(
  symbolt &symbol,
  std::string module_name,
  typet type,
  std::string name,
  std::string id,
  locationt location)
{
  symbol.mode = mode;
  symbol.module = module_name;
  symbol.location = std::move(location);
  symbol.type = std::move(type);
  symbol.name = name;
  symbol.id = id;
}

symbolt *solidity_convertert::move_symbol_to_context(symbolt &symbol)
{
  return context.move_symbol_to_context(symbol);
}

void solidity_convertert::convert_expression_to_code(exprt &expr)
{
  if (expr.is_code())
    return;

  codet code("expression");
  code.location() = expr.location();
  code.move_to_operands(expr);

  expr.swap(code);
}

bool solidity_convertert::check_intrinsic_function(
  const nlohmann::json &ast_node)
{
  // function to detect special intrinsic functions, e.g. __ESBMC_assume
  return (
    ast_node.contains("name") && (ast_node["name"] == "__ESBMC_assume" ||
                                  ast_node["name"] == "__VERIFIER_assume" ||
                                  ast_node["name"] == "__ESBMC_assert" ||
                                  ast_node["name"] == "__VERIFIER_assert"));
}

nlohmann::json solidity_convertert::make_implicit_cast_expr(
  const nlohmann::json &sub_expr,
  std::string cast_type)
{
  // Since Solidity AST does not have type cast information about return values,
  // we need to manually make a JSON object and wrap the return expression in it.
  std::map<std::string, std::string> m = {
    {"nodeType", "ImplicitCastExprClass"},
    {"castType", cast_type},
    {"subExpr", {}}};
  nlohmann::json implicit_cast_expr = m;
  implicit_cast_expr["subExpr"] = sub_expr;

  return implicit_cast_expr;
}

nlohmann::json
solidity_convertert::make_pointee_type(const nlohmann::json &sub_expr)
{
  // Since Solidity function call node does not have enough information, we need to make a JSON object
  // manually create a JSON object to complete the conversions of function to pointer decay

  // make a mapping for JSON object creation latter
  // based on the usage of get_func_decl_ref_t() in get_func_decl_ref_type()
  nlohmann::json adjusted_expr;

  if (
    sub_expr["typeString"].get<std::string>().find("function") !=
    std::string::npos)
  {
    // Add more special functions here
    if (
      sub_expr["typeString"].get<std::string>().find("function ()") !=
        std::string::npos ||
      sub_expr["typeIdentifier"].get<std::string>().find(
        "t_function_assert_pure$") != std::string::npos ||
      sub_expr["typeIdentifier"].get<std::string>().find(
        "t_function_internal_pure$") != std::string::npos)
    {
      // e.g. FunctionNoProto: "typeString": "function () returns (uint8)" with () empty after keyword 'function'
      // "function ()" contains the function args in the parentheses.
      // make a type to behave like SolidityGrammar::FunctionDeclRefT::FunctionNoProto
      // Note that when calling "assert(.)", it's like "typeIdentifier": "t_function_assert_pure$......",
      //  it's also treated as "FunctionNoProto".
      auto j2 = R"(
            {
              "nodeType": "FunctionDefinition",
              "parameters":
                {
                  "parameters" : []
                }
            }
          )"_json;
      adjusted_expr = j2;

      if (
        sub_expr["typeString"].get<std::string>().find("returns") !=
        std::string::npos)
      {
        adjusted_expr = R"(
            {
              "nodeType": "FunctionDefinition",
              "parameters":
                {
                  "parameters" : []
                }
            }
          )"_json;
        // e.g. for typeString like:
        // "typeString": "function () returns (uint8)"
        // use regex to capture the type and convert it to shorter form.
        std::smatch matches;
        std::regex e("returns \\((\\w+)\\)");
        std::string typeString = sub_expr["typeString"].get<std::string>();
        if (std::regex_search(typeString, matches, e))
        {
          auto j2 = nlohmann::json::parse(
            R"({
                "typeIdentifier": "t_)" +
            matches[1].str() + R"(",
                "typeString": ")" +
            matches[1].str() + R"("
              })");
          adjusted_expr["returnParameters"]["parameters"][0]
                       ["typeDescriptions"] = j2;
        }
        else if (
          sub_expr["typeString"].get<std::string>().find("returns (contract") !=
          std::string::npos)
        {
          // TODO: Fix me
          auto j2 = R"(
              {
                "nodeType": "ParameterList",
                "parameters": []
              }
            )"_json;
          adjusted_expr["returnParameters"] = j2;
        }
        else
          assert(!"Unsupported return types in pointee");
      }
      else
      {
        // e.g. for typeString like:
        // "typeString": "function (bool) pure"
        auto j2 = R"(
              {
                "nodeType": "ParameterList",
                "parameters": []
              }
            )"_json;
        adjusted_expr["returnParameters"] = j2;
      }
    }
    else
      assert(!"Unsupported - detected function call with parameters");
  }
  else
    assert(!"Unsupported pointee - currently we only support the semantics of function to pointer decay");

  return adjusted_expr;
}

// Parse typet object into a typeDescriptions json
nlohmann::json solidity_convertert::make_return_type_from_typet(typet type)
{
  // Useful to get the width of a int literal type for return statement
  nlohmann::json adjusted_expr;
  if (type.is_signedbv() || type.is_unsignedbv())
  {
    std::string width = type.width().as_string();
    std::string type_name = (type.is_signedbv() ? "int" : "uint") + width;
    auto j2 = nlohmann::json::parse(
      R"({
              "typeIdentifier": "t_)" +
      type_name + R"(",
              "typeString": ")" +
      type_name + R"("
            })");
    adjusted_expr = j2;
  }
  return adjusted_expr;
}

nlohmann::json solidity_convertert::make_array_elementary_type(
  const nlohmann::json &type_descrpt)
{
  // Function used to extract the type of the array and its elements
  // In order to keep the consistency and maximum the reuse of get_type_description function,
  // we used ["typeDescriptions"] instead of ["typeName"], despite the fact that the latter contains more information.
  // Although ["typeDescriptions"] also contains all the information needed, we have to do some pre-processing

  // e.g.
  //   "typeDescriptions": {
  //     "typeIdentifier": "t_array$_t_uint256_$dyn_memory_ptr",
  //     "typeString": "uint256[] memory"
  //      }
  //
  // convert to
  //
  //   "typeDescriptions": {
  //     "typeIdentifier": "t_uint256",
  //     "typeString": "uint256"
  //     }

  //! current implement does not consider Multi-Dimensional Arrays

  // 1. declare an empty json node
  nlohmann::json elementary_type;
  const std::string typeIdentifier =
    type_descrpt["typeIdentifier"].get<std::string>();

  // 2. extract type info
  // e.g.
  //  bytes[] memory x => "t_array$_t_bytes_$dyn_storage_ptr"  => t_bytes
  //  [1,2,3]          => "t_array$_t_uint8_$3_memory_ptr      => t_uint8
  assert(typeIdentifier.substr(0, 8) == "t_array$");
  std::regex rgx("\\$_\\w*_\\$");

  std::smatch match;
  if (!std::regex_search(typeIdentifier, match, rgx))
    assert(!"Cannot find array element type in typeIdentifier");
  std::string sub_match = match[0];
  std::string t_type = sub_match.substr(2, sub_match.length() - 4);
  std::string type = t_type.substr(2);

  // 3. populate node
  elementary_type = {{"typeIdentifier", t_type}, {"typeString", type}};

  return elementary_type;
}

nlohmann::json solidity_convertert::make_array_to_pointer_type(
  const nlohmann::json &type_descrpt)
{
  // Function to replace the content of ["typeIdentifier"] with "ArrayToPtr"
  // All the information in ["typeIdentifier"] should also be available in ["typeString"]
  std::string type_identifier = "ArrayToPtr";
  std::string type_string = type_descrpt["typeString"].get<std::string>();

  std::map<std::string, std::string> m = {
    {"typeIdentifier", type_identifier}, {"typeString", type_string}};
  nlohmann::json adjusted_type = m;

  return adjusted_type;
}

nlohmann::json solidity_convertert::add_dyn_array_size_expr(
  const nlohmann::json &type_descriptor,
  const nlohmann::json &dyn_array_node)
{
  nlohmann::json adjusted_descriptor;
  adjusted_descriptor = type_descriptor;
  // get the JSON object for size expr and merge it with the original type descriptor
  adjusted_descriptor.push_back(nlohmann::json::object_t::value_type(
    "sizeExpr", dyn_array_node["initialValue"]["arguments"][0]));

  return adjusted_descriptor;
}

std::string
solidity_convertert::get_array_size(const nlohmann::json &type_descrpt)
{
  const std::string s = type_descrpt["typeString"].get<std::string>();
  std::regex rgx(".*\\[([0-9]+)\\]");
  std::string the_size;

  std::smatch match;
  if (std::regex_search(s.begin(), s.end(), match, rgx))
  {
    std::ssub_match sub_match = match[1];
    the_size = sub_match.str();
  }
  else
    assert(!"Unsupported - Missing array size in type descriptor. Detected dynamic array?");

  return the_size;
}

bool solidity_convertert::is_dyn_array(const nlohmann::json &ast_node)
{
  if (
    ast_node.contains("typeDescriptions") &&
    SolidityGrammar::get_type_name_t(ast_node["typeDescriptions"]) ==
      SolidityGrammar::DynArrayTypeName)
    return true;
  return false;
}

// check if the node is a mapping
bool solidity_convertert::is_mapping(const nlohmann::json &ast_node)
{
  if (
    ast_node.contains("typeDescriptions") &&
    SolidityGrammar::get_type_name_t(ast_node["typeDescriptions"]) ==
      SolidityGrammar::MappingTypeName)
    return true;
  return false;
}

bool solidity_convertert::get_constructor_call(
  const nlohmann::json &ast_node,
  exprt &new_expr)
{
  nlohmann::json callee_expr_json = ast_node["expression"];
  int ref_decl_id = callee_expr_json["typeName"]["referencedDeclaration"];
  exprt callee;

  const std::string contract_name = exportedSymbolsList[ref_decl_id];
  assert(linearizedBaseList.count(contract_name) && !contract_name.empty());

  const nlohmann::json constructor_ref = find_constructor_ref(ref_decl_id);

  // Special handling of implicit constructor
  // since there is no ast nodes for implicit constructor
  if (constructor_ref.empty())
    return get_implicit_ctor_ref(new_expr, contract_name);

  if (get_func_decl_ref(constructor_ref, callee))
    return true;

  // obtain the type info
  std::string id = prefix + contract_name;
  if (context.find_symbol(id) == nullptr)
    return true;

  const symbolt &s = *context.find_symbol(id);
  typet type = s.type;

  side_effect_expr_function_callt call;
  call.function() = callee;
  call.type() = type;

  auto param_nodes = constructor_ref["parameters"]["parameters"];
  unsigned num_args = 0;

  exprt new_obj("new_object");
  new_obj.type() = type;
  call.arguments().push_back(address_of_exprt(new_obj));

  for (const auto &arg : ast_node["arguments"].items())
  {
    nlohmann::json param = nullptr;
    nlohmann::json::iterator itr = param_nodes.begin();
    if (itr != param_nodes.end())
    {
      if ((*itr).contains("typeDescriptions"))
      {
        param = (*itr)["typeDescriptions"];
      }
      ++itr;
    }

    exprt single_arg;
    if (get_expr(arg.value(), param, single_arg))
      return true;

    call.arguments().push_back(single_arg);
    ++num_args;
  }

  // for adjustment
  call.set("constructor", 1);
  new_expr = call;

  return false;
}

bool solidity_convertert::get_implicit_ctor_ref(
  exprt &new_expr,
  const std::string &contract_name)
{
  // to obtain the type info
  std::string name, id;

  id = get_ctor_call_id(contract_name);
  if (context.find_symbol(id) == nullptr)
    return true;
  const symbolt &s = *context.find_symbol(id);
  typet type = s.type;

  new_expr = exprt("symbol", type);
  new_expr.identifier(id);
  new_expr.cmt_lvalue(true);
  new_expr.name(name);

  side_effect_expr_function_callt call;
  struct_typet tmp = struct_typet();
  call.function() = new_expr;
  call.type() = tmp;

  call.set("constructor", 1);
  new_expr = call;

  return false;
}

/*
  construct a void function with empty function body
  then add this function to symbol table
*/
bool solidity_convertert::get_default_function(
  const std::string name,
  const std::string id)
{
  nlohmann::json ast_node;
  auto j2 = R"(
              {
                "nodeType": "ParameterList",
                "parameters": []
              }
            )"_json;
  ast_node["returnParameters"] = j2;

  code_typet type;
  if (get_type_description(ast_node["returnParameters"], type.return_type()))
    return true;

  locationt location_begin;

  if (current_fileName == "")
    return true;
  std::string debug_modulename = current_fileName;

  symbolt symbol;
  get_default_symbol(symbol, debug_modulename, type, name, id, location_begin);

  symbol.lvalue = true;
  symbol.is_extern = false;
  symbol.file_local = false;

  symbolt &added_symbol = *move_symbol_to_context(symbol);

  code_blockt body_exprt = code_blockt();
  added_symbol.value = body_exprt;

  type.make_ellipsis();
  added_symbol.type = type;

  return false;
}

bool solidity_convertert::is_bytes_type(const typet &t)
{
  if (t.get("#sol_type").as_string().find("BYTES") != std::string::npos)
    return true;
  return false;
}

void solidity_convertert::convert_type_expr(
  const namespacet &ns,
  exprt &src_expr,
  const typet &dest_type)
{
  if (src_expr.type() != dest_type)
  {
    // only do conversion when the src.type != dest.type
    if (is_bytes_type(src_expr.type()) && is_bytes_type(dest_type))
    {
      // 1. Fixed-size Bytes Converted to Smaller Types
      //    bytes2 a = 0x4326;
      //    bytes1 b = bytes1(a); // b will be 0x43
      // 2. Fixed-size Bytes Converted to Larger Types
      //    bytes2 a = 0x4235;
      //    bytes4 b = bytes4(a); // b will be 0x42350000
      // which equals to:
      //    new_type b = bswap(new_type)(bswap(x)))

      exprt bswap_expr, sub_bswap_expr;

      // 1. bswap
      sub_bswap_expr = exprt("bswap", src_expr.type());
      sub_bswap_expr.operands().push_back(src_expr);

      // 2. typecast
      solidity_gen_typecast(ns, sub_bswap_expr, dest_type);

      // 3. bswap back
      bswap_expr = exprt("bswap", sub_bswap_expr.type());
      bswap_expr.operands().push_back(sub_bswap_expr);

      src_expr = bswap_expr;
    }
    else
      solidity_gen_typecast(ns, src_expr, dest_type);
  }
}

static inline void static_lifetime_init(const contextt &context, codet &dest)
{
  dest = code_blockt();

  // call designated "initialization" functions
  context.foreach_operand_in_order([&dest](const symbolt &s) {
    if (s.type.initialization() && s.type.is_code())
    {
      code_function_callt function_call;
      function_call.function() = symbol_expr(s);
      dest.move_to_operands(function_call);
    }
  });
}

// declare an empty array symbol and move it to the context
bool solidity_convertert::get_empty_array_ref(
  const nlohmann::json &expr,
  exprt &new_expr)
{
  // Get Name
  nlohmann::json callee_expr_json = expr["expression"];
  nlohmann::json callee_arg_json = expr["arguments"][0];

  // get unique label
  // e.g. "sol:@C@BASE@array#14"
  //TODO: FIX ME. This will probably not work in multi-contract verification.
  std::string label = std::to_string(callee_expr_json["id"].get<int>());
  std::string name, id, contract_name;
  if (get_current_contract_name(callee_expr_json, contract_name))
  {
    log_error("Internal error when obtaining the contract name. Aborting...");
    abort();
  }

  name = "array#" + label;
  if (!contract_name.empty())
    id = "sol:@C@" + contract_name + "@" + name;
  else
    id = "sol:@" + name;

  // Get Location
  locationt location_begin;
  get_location_from_decl(callee_expr_json, location_begin);

  // Get Debug Module Name
  std::string debug_modulename =
    get_modulename_from_path(location_begin.file().as_string());

  // Get Type
  // 1. get elem type
  typet elem_type;
  const nlohmann::json elem_node =
    callee_expr_json["typeName"]["baseType"]["typeDescriptions"];
  if (get_type_description(elem_node, elem_type))
    return true;

  // 2. get array size
  exprt size;
  const nlohmann::json literal_type = callee_arg_json["typeDescriptions"];
  if (get_expr(callee_arg_json, literal_type, size))
    return true;

  // 3. declare array
  typet arr_type = array_typet(elem_type, size);

  // Get Symbol
  symbolt symbol;
  get_default_symbol(
    symbol, debug_modulename, arr_type, name, id, location_begin);

  symbol.lvalue = true;
  symbol.static_lifetime = true;
  symbol.file_local = false;
  symbol.is_extern = true;

  symbolt &added_symbol = *move_symbol_to_context(symbol);

  // Poplulate default value
  if (size.value().as_string() != "" && size.value().as_string() != "0")
  {
    added_symbol.value = gen_zero(arr_type);
    added_symbol.value.zero_initializer(true);
  }

  new_expr = symbol_expr(added_symbol);
  return false;
}

/*
  perform multi-transaction verification
  the idea is to verify the assertions that must be held 
  in any function calling order.
*/
bool solidity_convertert::multi_transaction_verification(
  const std::string &contractName)
{
  /*
  convert the verifying contract to a "sol_main" function, e.g.

  Contract Base             
  {
      constrcutor(){}
      function A(){}
      function B(){}
  }

  will be converted to

  void sol_main()
  {
    Base()  // constructor_call
    while(nondet_bool)
    {
      if(nondet_bool) A();
      if(nondet_bool) B();
    }
  }

  Additionally, we need to handle the inheritance. Theoretically, we need to merge (i.e. create a copy) the public and internal state variables and functions inside Base contracts into the Derive contract. However, in practice we do not need to do so. Instead, we 
    - call the constructors based on the linearizedBaseList 
    - add the inherited public function call to the if-body 
  */

  // 0. initialize "sol_main" body and while-loop body
  codet func_body, while_body;
  static_lifetime_init(context, while_body);
  static_lifetime_init(context, func_body);

  while_body.make_block();
  func_body.make_block();

  // 1. get constructor call
  const std::vector<int> &id_list = linearizedBaseList[contractName];
  // iterating from the end to the beginning
  if (id_list.empty())
  {
    log_error("Input contract is not found in the source file.");
    return true;
  }

  for (auto it = id_list.rbegin(); it != id_list.rend(); ++it)
  {
    // 1.1 get contract symbol ("tag-contractName")
    std::string c_name = exportedSymbolsList[*it];
    const std::string id = prefix + c_name;
    if (context.find_symbol(id) == nullptr)
      return true;
    const symbolt &contract = *context.find_symbol(id);
    assert(contract.type.is_struct() && "A contract should be a struct");

    // 1.2 construct a constructor call and move to func_body
    const std::string ctor_id = get_ctor_call_id(c_name);

    if (context.find_symbol(ctor_id) == nullptr)
    {
      // if the input contract name is not found in the src file, return true
      log_error("Input contract is not found in the source file.");
      return true;
    }
    const symbolt &constructor = *context.find_symbol(ctor_id);
    code_function_callt call;
    call.location() = constructor.location;
    call.function() = symbol_expr(constructor);
    const code_typet::argumentst &arguments =
      to_code_type(constructor.type).arguments();
    call.arguments().resize(
      arguments.size(), static_cast<const exprt &>(get_nil_irep()));

    // move to "sol_main" body
    func_body.move_to_operands(call);

    // 2. construct a while-loop and move to func_body

    // 2.0 check visibility setting
    bool skip_vis =
      config.options.get_option("no-visibility").empty() ? false : true;
    if (skip_vis)
    {
      log_warning(
        "force to verify every function, even it's an unreachable "
        "internal/private function. This might lead to false positives.");
    }

    // 2.1 construct ifthenelse statement
    const struct_typet::componentst &methods =
      to_struct_type(contract.type).methods();
    bool is_tgt_cnt = c_name == contractName ? true : false;

    for (const auto &method : methods)
    {
      // we only handle public (and external) function
      // as the private and internal function cannot be directly called
      if (is_tgt_cnt)
      {
        if (
          !skip_vis && method.get_access().as_string() != "public" &&
          method.get_access().as_string() != "external")
          continue;
      }
      else
      {
        // this means functions inherited from base contracts
        if (!skip_vis && method.get_access().as_string() != "public")
          continue;
      }

      // skip constructor
      const std::string func_id = method.identifier().as_string();
      if (func_id == ctor_id)
        continue;

      // guard: nondet_bool()
      if (context.find_symbol("c:@F@nondet_bool") == nullptr)
        return true;
      const symbolt &guard = *context.find_symbol("c:@F@nondet_bool");

      side_effect_expr_function_callt guard_expr;
      get_library_function_call(
        "nondet_bool",
        "c:@F@nondet_bool",
        guard.type,
        guard.location,
        guard_expr);

      // then: function_call
      if (context.find_symbol(func_id) == nullptr)
        return true;
      const symbolt &func = *context.find_symbol(func_id);
      code_function_callt then_expr;
      then_expr.location() = func.location;
      then_expr.function() = symbol_expr(func);
      const code_typet::argumentst &arguments =
        to_code_type(func.type).arguments();
      then_expr.arguments().resize(
        arguments.size(), static_cast<const exprt &>(get_nil_irep()));

      // ifthenelse-statement:
      codet if_expr("ifthenelse");
      if_expr.copy_to_operands(guard_expr, then_expr);

      // move to while-loop body
      while_body.move_to_operands(if_expr);
    }
  }

  // while-cond:
  const symbolt &guard = *context.find_symbol("c:@F@nondet_bool");
  side_effect_expr_function_callt cond_expr;
  get_library_function_call(
    "nondet_bool",
    "c:@F@nondet_bool",
    guard.type,
    func_body.location(),
    cond_expr);

  // while-loop statement:
  code_whilet code_while;
  code_while.cond() = cond_expr;
  code_while.body() = while_body;

  // move to "sol_main"
  func_body.move_to_operands(code_while);

  // 3. add "sol_main" to symbol table
  symbolt new_symbol;
  code_typet main_type;
  typet e_type = empty_typet();
  e_type.set("cpp_type", "void");
  main_type.return_type() = e_type;
  const std::string sol_name = "sol_main_" + contractName;
  const std::string sol_id = "sol:@C@" + contractName + "@F@" + sol_name;
  const symbolt &contract = *context.find_symbol(prefix + contractName);
  new_symbol.location = contract.location;
  std::string debug_modulename =
    get_modulename_from_path(contract.location.file().as_string());
  get_default_symbol(
    new_symbol,
    debug_modulename,
    main_type,
    sol_name,
    sol_id,
    contract.location);

  new_symbol.lvalue = true;
  new_symbol.is_extern = false;
  new_symbol.file_local = false;

  symbolt &added_symbol = *context.move_symbol_to_context(new_symbol);

  // no params
  main_type.make_ellipsis();

  added_symbol.type = main_type;
  added_symbol.value = func_body;

  // 4. set "sol_main" as main function
  // this will be overwrite in multi-contract mode.
  config.main = sol_name;

  return false;
}

/*
  This function perform multi-transaction verification on each contract in isolation.
  To do so, we construct nondetered switch_case;
*/
bool solidity_convertert::multi_contract_verification()
{
  // 0. initialize "sol_main" body and switch body
  codet func_body, switch_body;
  static_lifetime_init(context, switch_body);
  static_lifetime_init(context, func_body);

  switch_body.make_block();
  func_body.make_block();
  // 1. construct switch-case
  int cnt = 0;
  for (const auto &sym : exportedSymbolsList)
  {
    // 1.1 construct multi-transaction verification entry function
    // function "sol_main_contractname" will be created and inserted to the symbol table.
    const std::string &c_name = sym.second;
    if (linearizedBaseList.count(c_name))
    {
      if (multi_transaction_verification(c_name))
        return true;
    }
    else
      //! Assume is not a contract (e.g. error type)
      continue;

    // 1.2 construct a "case n"
    exprt case_cond = constant_exprt(
      integer2binary(cnt, bv_width(int_type())),
      integer2string(cnt),
      int_type());

    // 1.3 construct case body: entry function + break
    codet case_body;
    static_lifetime_init(context, case_body);
    case_body.make_block();

    // func_call: sol_main_contractname
    const std::string sub_sol_id = "sol:@C@" + c_name + "@F@sol_main_" + c_name;
    if (context.find_symbol(sub_sol_id) == nullptr)
      return true;

    const symbolt &func = *context.find_symbol(sub_sol_id);
    code_function_callt func_expr;
    func_expr.location() = func.location;
    func_expr.function() = symbol_expr(func);
    const code_typet::argumentst &arguments =
      to_code_type(func.type).arguments();
    func_expr.arguments().resize(
      arguments.size(), static_cast<const exprt &>(get_nil_irep()));
    case_body.move_to_operands(func_expr);

    // break statement
    exprt break_expr = code_breakt();
    case_body.move_to_operands(break_expr);

    // 1.4 construct case statement
    code_switch_caset switch_case;
    switch_case.case_op() = case_cond;
    convert_expression_to_code(case_body);
    switch_case.code() = to_code(case_body);

    // 1.5 move to switch body
    switch_body.move_to_operands(switch_case);

    // update case number counter
    ++cnt;
  }

  // 2. move switch to func_body
  // 2.1 construct nondet_uint jump condition
  if (context.find_symbol("c:@F@nondet_uint") == nullptr)
    return true;
  const symbolt &cond = *context.find_symbol("c:@F@nondet_uint");

  side_effect_expr_function_callt cond_expr;
  cond_expr.name("nondet_uint");
  cond_expr.identifier("c:@F@nondet_uint");
  cond_expr.location() = cond.location;
  cond_expr.cmt_lvalue(true);
  cond_expr.function() = symbol_expr(cond);

  // 2.2 construct switch statement
  code_switcht code_switch;
  code_switch.value() = cond_expr;
  code_switch.body() = switch_body;
  func_body.move_to_operands(code_switch);

  // 3. add "sol_main" to symbol table
  symbolt new_symbol;
  code_typet main_type;
  main_type.return_type() = empty_typet();
  const std::string sol_id = "sol:@F@sol_main";
  const std::string sol_name = "sol_main";

  if (
    context.find_symbol(prefix + linearizedBaseList.begin()->first) == nullptr)
    return true;
  // use first contract's location
  const symbolt &contract =
    *context.find_symbol(prefix + linearizedBaseList.begin()->first);
  new_symbol.location = contract.location;
  std::string debug_modulename =
    get_modulename_from_path(contract.location.file().as_string());
  get_default_symbol(
    new_symbol,
    debug_modulename,
    main_type,
    sol_name,
    sol_id,
    new_symbol.location);

  new_symbol.lvalue = true;
  new_symbol.is_extern = false;
  new_symbol.file_local = false;

  symbolt &added_symbol = *context.move_symbol_to_context(new_symbol);

  // no params
  main_type.make_ellipsis();

  added_symbol.type = main_type;
  added_symbol.value = func_body;
  config.main = sol_name;
  return false;
}
