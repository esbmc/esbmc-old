#include <solidity-frontend/solidity_convert.h>
#include <solidity-frontend/solidity_template.h>
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
#include <iostream>

solidity_convertert::solidity_convertert(
  contextt &_context,
  nlohmann::json &_ast_json,
  const std::string &_sol_func,
  const std::string &_contract_path)
  : context(_context),
    ns(context),
    src_ast_json_array(_ast_json),
    sol_func(_sol_func),
    contract_path(_contract_path),
    global_scope_id(0),
    current_scope_var_num(1),
    current_functionDecl(nullptr),
    current_forStmt(nullptr),
    current_typeName(nullptr),
    current_blockDecl(code_blockt()),
    current_lhsDecl(false),
    current_rhsDecl(false),
    current_functionName(""),
    current_contractName(""),
    scope_map({}),
    initializers({}),
    ctor_modifier(empty_json),
    base_contracts(empty_json),
    is_contract_member_access(false),
    tgt_func(config.options.get_option("function")),
    tgt_cnt(config.options.get_option("contract")),
    aux_counter(0)
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

  // First, we handle the multiple JSON files(imported files)
  // The imported files are stored in src_ast_json_array and we merge them into a single JSON file
  multi_json_file();
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
      pattern_check->do_pattern_check();
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
  // including struct, enum, interface, event, error, library, constant...
  // noted that some can also be inside the contract, e.g. struct, enum...
  index = 0;
  for (nlohmann::json::iterator itr = nodes.begin(); itr != nodes.end();
       ++itr, ++index)
  {
    if (get_noncontract_defition(*itr))
      return true;
    if (
      (*itr)["nodeType"].get<std::string>() == "VariableDeclaration" &&
      (*itr)["mutability"].get<std::string>() == "constant")
    {
      // for constant variable defined in the file level which is outside the contract definition
      exprt dump;
      if (get_var_decl(*itr, dump))
        return true;
    }
  }

  // second round: populate linearizedBaseList & base_contracts
  // this is to obtain the contract name list
  index = 0;
  for (nlohmann::json::iterator itr = nodes.begin(); itr != nodes.end();
       ++itr, ++index)
  {
    std::string node_type = (*itr)["nodeType"].get<std::string>();

    if (node_type == "ContractDefinition") // rule source-unit
    {
      current_contractName = (*itr)["name"].get<std::string>();

      // store linearizedBaseList
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
      // store baseContracts
      // this will be used in ctor initialization
      if (!(*itr)["baseContracts"].empty())
        base_contracts = (*itr)["baseContracts"];

      current_contractName = (*itr)["name"].get<std::string>();

      // for inheritance: merge the ast node
      // make copy. As we do not want to modify the src json
      nlohmann::json c_node = *itr;
      std::set<std::string> dump = {};
      merge_inheritance_ast(c_node, current_contractName, dump);

      nlohmann::json &ast_nodes = (*itr)["nodes"];
      for (nlohmann::json::iterator ittr = ast_nodes.begin();
           ittr != ast_nodes.end();
           ++ittr)
      {
        if (get_noncontract_defition(*ittr))
          return true;
      }
      ast_nodes = c_node["nodes"];
      for (nlohmann::json::iterator ittr = ast_nodes.begin();
           ittr != ast_nodes.end();
           ++ittr)
      {
        if (get_noncontract_defition(*ittr))
          return true;
      }

      // add a struct symbol for each contract
      // e.g. contract Base => struct Base
      if (get_struct_class(c_node))
        return true;

      if (convert_ast_nodes(c_node))
        return true; // 'true' indicates something goes wrong.

      // add implicit construcor function
      if (add_implicit_constructor(current_contractName))
        return true;

      // initialize state variable
      if (move_initializer_to_ctor(current_contractName))
        return true;

      // handling mapping_init
    }

    // reset
    current_contractName = "";
    current_functionName = "";
    current_functionDecl = nullptr;
    current_typeName = nullptr;
    current_forStmt = nullptr;
    current_blockDecl.clear();
    global_scope_id = 0;
    map_init_block.clear();
    initializers.clear();
    ctor_modifier = empty_json;
    base_contracts = empty_json;
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

  // otherwise, we verify the target function.
  return false; // 'false' indicates successful completion.
}

void solidity_convertert::multi_json_file()
{
  // Import relationship diagram
  std::unordered_map<std::string, std::unordered_set<std::string>> import_graph;
  // Path to JSON object mapping
  std::unordered_map<std::string, nlohmann::json> path_to_json;
  // Constructing an import relationship diagram
  for (auto &ast_json : src_ast_json_array)
  {
    std::string path = ast_json["absolutePath"];
    path_to_json[path] = ast_json;
    std::unordered_set<std::string> imports;
    // Extract the import path from the ImportDirective node.
    for (const auto &node : ast_json["nodes"])
    {
      if (node["nodeType"] == "ImportDirective")
      {
        std::string import_path = node["absolutePath"];
        imports.insert(import_path);
      }
    }
    import_graph[path] = imports;
  }

  // Perform topological sorting
  std::vector<nlohmann::json> sorted_json_files =
    topological_sort(import_graph, path_to_json);

  // Update order of src_ast_json_array
  src_ast_json_array = sorted_json_files;

  // src_ast_json_array[0] means the .sol file that is being verified and not being imported.
  src_ast_json = src_ast_json_array[0];

  // The initial part of the nodes in a single AST includes an import information description section
  // and a version description section.This is followed by all the information that needs to be verified.
  // Therefore, the rest of the key nodes need to be inserted sequentially thereafter
  // It also means before the first ContractDefinition node.
  size_t insert_pos = 0;
  for (size_t i = 0; i < src_ast_json["nodes"].size(); ++i)
  {
    if (src_ast_json["nodes"][i]["nodeType"] == "ContractDefinition")
    {
      insert_pos = i;
      break;
    }
  }

  for (size_t i = 1; i < src_ast_json_array.size(); ++i)
  {
    nlohmann::json &imported_part = src_ast_json_array[i];
    // Traverse nodes in the imported part
    for (const auto &node : imported_part["nodes"])
    {
      if (
        node["nodeType"] == "ContractDefinition" &&
        (node["contractKind"] == "contract" ||
         node["contractKind"] == "interface"))
      {
        // Add the node before the first ContractDefinition node
        // chose to insert it here instead of at the end because splitting a piece of Solidity code(use import)
        // into multiple files results in the import order of contracts and interfaces in the AST file
        // being reversed compared to the unsplit version.
        src_ast_json["nodes"].insert(
          src_ast_json["nodes"].begin() + insert_pos, node);
        ++insert_pos; // Adjust the insert position for the next node
      }
    }
  }
}

std::vector<nlohmann::json> solidity_convertert::topological_sort(
  std::unordered_map<std::string, std::unordered_set<std::string>> &graph,
  std::unordered_map<std::string, nlohmann::json> &path_to_json)
{
  std::unordered_map<std::string, int> in_degree;
  std::queue<std::string> zero_in_degree_queue;
  std::vector<nlohmann::json> sorted_files;
  //Topological sorting function for sorting files according to import relationships
  // Calculate the in-degree for each node
  for (const auto &pair : graph)
  {
    if (in_degree.find(pair.first) == in_degree.end())
    {
      in_degree[pair.first] = 0;
    }
    for (const auto &neighbor : pair.second)
    {
      if (pair.first != neighbor)
      {
        // Ignore the case of importing itself.
        in_degree[neighbor]++;
      }
    }
  }

  // Find all the nodes with 0 entry and add them to the queue.
  for (const auto &pair : in_degree)
  {
    if (pair.second == 0)
    {
      zero_in_degree_queue.push(pair.first);
    }
  }
  // Process nodes in the queue
  while (!zero_in_degree_queue.empty())
  {
    std::string node = zero_in_degree_queue.front();
    zero_in_degree_queue.pop();
    // add the node's corresponding JSON file to the sorted result
    sorted_files.push_back(path_to_json[node]);
    // Update the in-degree of neighbouring nodes and add the new node with in-degree 0 to the queue
    for (const auto &neighbor : graph[node])
    {
      if (node != neighbor)
      { // Ignore the case of importing itself.
        in_degree[neighbor]--;
        if (in_degree[neighbor] == 0)
        {
          zero_in_degree_queue.push(neighbor);
        }
      }
    }
  }

  return sorted_files;
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
      "@@@ Converting node[{}]: name={}, nodeType={} ...",
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

  log_debug(
    "solidity",
    "\t@@@ Expecting non-function definition, Got {}",
    SolidityGrammar::contract_body_element_to_str(type));

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

  log_debug(
    "solidity",
    "\t@@@ Expecting function definition, Got {}",
    SolidityGrammar::contract_body_element_to_str(type));

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

// push back a this pointer to the type
void solidity_convertert::get_function_this_pointer_param(
  const std::string &contract_name,
  const std::string &func_id,
  const std::string &debug_modulename,
  const locationt &location_begin,
  code_typet &type)
{
  code_typet::argumentt this_param;
  std::string this_name = "this";
  //? do we need to drop the '#n' tail in func_id?
  std::string this_id = func_id + "#" + this_name;

  this_param.cmt_base_name(this_name);
  this_param.cmt_identifier(this_id);

  this_param.type() = gen_pointer_type(symbol_typet(prefix + contract_name));
  symbolt param_symbol;
  get_default_symbol(
    param_symbol,
    debug_modulename,
    this_param.type(),
    this_name,
    this_id,
    location_begin);
  param_symbol.lvalue = true;
  param_symbol.is_parameter = true;
  param_symbol.file_local = true;

  if (context.find_symbol(this_id) == nullptr)
  {
    context.move_symbol_to_context(param_symbol);
  }

  type.arguments().push_back(this_param);
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
  bool mapping = is_mapping(ast_node);
  if (mapping)
  {
    if (get_mapping_type(ast_node, t))
      return true;
  }
  else
  {
    const nlohmann::json *old_typeName = current_typeName;
    current_typeName = &ast_node["typeName"];
    if (get_type_description(ast_node["typeName"]["typeDescriptions"], t))
      return true;
    current_typeName = old_typeName;
  }

  if (
    t.get("#sol_type") == "CONTRACT" &&
    t.identifier().as_string() == prefix + current_contractName)
  {
    // contract Base{
    //   Base x;
    // }
    // which is not allowed in C++
    // thus we have to log_error in this case
    log_error("Unspported contract variable declaration.");
    abort();
  }

  // set const qualifier
  if (ast_node.contains("mutability") && ast_node["mutability"] == "constant")
    t.cmt_constant(true);

  // record the state info
  // this will be used to decide if the var will be converted to this->var
  // when parsing function body.
  bool is_state_var = ast_node["stateVariable"].get<bool>();
  t.set("#sol_state_var", is_state_var);

  bool is_inherited = ast_node.contains("is_inherited");

  // 2. populate id and name
  std::string name, id;
  //TODO: Omitted variable
  if (ast_node["name"].get<std::string>().empty())
  {
    log_error("Variables with omitted name are not supported.");
    return true;
  }
  if (get_var_decl_name(ast_node, name, id))
    return true;

  // skip if we have already populated the var symbol
  if (context.find_symbol(id) != nullptr)
    return false;

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
  // static_lifetime: this means it's defined in the file level, not inside contract
  symbol.static_lifetime = current_contractName.empty();
  symbol.file_local = !symbol.static_lifetime;
  symbol.is_extern = false;

  // For state var decl, we look for "value".
  // For local var decl, we look for "initialValue"
  bool has_init =
    (ast_node.contains("value") || ast_node.contains("initialValue"));
  bool is_not_init_contract_var =
    (SolidityGrammar::get_type_name_t(
       ast_node["typeName"]["typeDescriptions"]) ==
     SolidityGrammar::ContractTypeName) &&
    !has_init;

  if ((!has_init || is_inherited) && !is_not_init_contract_var && !mapping)
  {
    // for both state and non-state variables, set default value as zero
    symbol.value = gen_zero(get_complete_type(t, ns), true);
    symbol.value.zero_initializer(true);
  }

  // 6. add symbol into the context
  // just like clang-c-frontend, we have to add the symbol before converting the initial assignment
  symbolt &added_symbol = *move_symbol_to_context(symbol);

  // 7. populate init value if there is any
  code_declt decl(symbol_expr(added_symbol));

  // special handling for array/dynarray
  std::string t_sol_type = t.get("#sol_type").as_string();
  exprt val;
  if (t_sol_type == "ARRAY")
  {
    /** 
      uint[2] z;            // uint *z = (uint *)calloc(2, sizeof(uint));
      
                            // uint tmp1[2] = {1,2}; // populated into sym tab, not a real statement
      uint[2] zz = [1,2];   // uint *zz = (uint *)arrcpy(tmp1, 2, 2, sizeof(uint));

      uint[2] y = x;        // uint *zz = (uint *)arrcpy(x, 2, 2, sizeof(uint));

      TODO: suport disorder:
      uint[2] y = x;
      uint[2] x = [1,2];
    **/

    // get size
    std::string arr_size = "0";
    if (!t.get("#sol_array_size").empty())
      arr_size = t.get("#sol_array_size").as_string();
    else if (t.has_subtype() && !t.subtype().get("#sol_array_size").empty())
      arr_size = t.subtype().get("#sol_array_size").as_string();
    else
    {
      log_error("cannot get the size of fixed array");
      return true;
    }
    exprt size_expr = constant_exprt(
      integer2binary(string2integer(arr_size), bv_width(uint_type())),
      arr_size,
      uint_type());

    // get sizeof
    exprt size_of_expr;
    get_size_of_expr(t.subtype(), size_of_expr);

    if (has_init && !is_inherited)
    {
      if (get_init_expr(ast_node, t, val))
        return true;

      side_effect_expr_function_callt acpy_call;
      get_arrcpy_function_call(location_begin, acpy_call);
      acpy_call.arguments().push_back(val);
      acpy_call.arguments().push_back(size_expr);
      acpy_call.arguments().push_back(size_of_expr);
      // typecast
      solidity_gen_typecast(ns, acpy_call, t);
      // set as rvalue
      added_symbol.value = acpy_call;
      decl.operands().push_back(acpy_call);
    }
    else
    {
      // do calloc
      side_effect_expr_function_callt calc_call;
      get_calloc_function_call(location_begin, calc_call);
      calc_call.arguments().push_back(size_expr);
      calc_call.arguments().push_back(size_of_expr);
      // typecast
      solidity_gen_typecast(ns, calc_call, t);
      // set as rvalue
      added_symbol.value = calc_call;
      decl.operands().push_back(calc_call);
    }
  }
  else if (t_sol_type == "DYNARRAY" && (has_init && !is_inherited))
  {
    exprt val;
    if (get_init_expr(ast_node, t, val))
      return true;

    if (val.is_typecast())
    {
      // uint[] zz = new uint(10);
      //=> uint* zz = (uint *)calloc(10, sizeof(uint));
      solidity_gen_typecast(ns, val, t);
      added_symbol.value = val;
      decl.operands().push_back(val);
    }
    else if (val.is_symbol())
    {
      /** 
      uint[] zzz;           // uint* zzz; // will not reach here actually
                            // 
      uint[] zzzz = [1,2];  // memcpy(zzzz, tmp2, 2*sizeof(uint));
                            // uint* zzzzz = 0;
      uint[2] zzzzz = z;    // memcpy(zzzzz, z, 2*sizeof(uint));
                            // uint* zzzzz = 0;
      uint[] zzzzzz = zzz;  // memcpy(zzzzzz, zzz, zzz.size * sizeof(uint));

      Theoretically we can convert it to something like int *z = new int[2]{0,1};
      However, this feature seems to be not fully supported in current esbmc-cpp (v7.6.1)
    */
      // get size
      std::string arr_size = "0";
      typet val_t = val.type();
      if (!val_t.get("#sol_array_size").empty())
        arr_size = val_t.get("#sol_array_size").as_string();
      else if (
        val_t.has_subtype() && !val_t.subtype().get("#sol_array_size").empty())
        arr_size = val_t.subtype().get("#sol_array_size").as_string();

      exprt size_expr = constant_exprt(
        integer2binary(string2integer(arr_size), bv_width(uint_type())),
        arr_size,
        uint_type());

      // get sizeof
      exprt size_of_expr;
      get_size_of_expr(t.subtype(), size_of_expr);

      side_effect_expr_function_callt acpy_call;
      get_arrcpy_function_call(location_begin, acpy_call);
      acpy_call.arguments().push_back(val);
      acpy_call.arguments().push_back(size_expr);
      acpy_call.arguments().push_back(size_of_expr);
      // typecast
      solidity_gen_typecast(ns, acpy_call, t);
      // set as rvalue
      added_symbol.value = acpy_call;
      decl.operands().push_back(acpy_call);
    }
    else
    {
      log_error("Unexpect initialization for dynamic array");
      log_debug("solidity", "{}", val);
      return true;
    }
  }
  else if (is_not_init_contract_var)
  {
    log_debug("solidity", "Handling uninitialized contract type variable");
    /*
      Special handling for contract-type variable instantiation
      e.g.  Base x ==> Base x = Base();
      In Solidity, the contract-type var will not get automatically instantiated.
      However in c++ (and also the backend of ESBMC), the class-type object will get instantiated.
      Therefore, we manually create a constructor which
      - has a empty body
      - will not be conflict with the ctor in the src file by any means.
      Approach: since there is no pointer in Solidity, we create a ctor like:
        Base(int *p){}
      and the object will be instantiated as Base x = Base(nullptr);
    */

    // 1. get object-constract name
    assert(
      ast_node["typeName"]["nodeType"].get<std::string>() ==
      "UserDefinedTypeName");
    const std::string contract_name =
      ast_node["typeName"]["pathNode"]["name"].get<std::string>();

    // 2. add a empty constructor to mimic C++ object auto instantiation
    // e.g. Base x => Base x = Base(p) where p is a integer pointer
    if (get_instantiation_ctor_call(contract_name, val))
      return true;

    // 3. add constructor call to declaration operands
    added_symbol.value = val;
    decl.operands().push_back(val);
  }
  // special handling for mapping
  else if (mapping)
  {
    // mapping(string => uint) test;
    // => int256* x = calloc(50, sizeof(int256));

    // construct calloc call
    side_effect_expr_function_callt calc_call;
    get_calloc_function_call(location_begin, calc_call);

    exprt size_expr = constant_exprt(
      integer2binary(50, bv_width(uint_type())),
      integer2string(50),
      uint_type());

    exprt size_of_expr;
    get_size_of_expr(t.subtype(), size_of_expr);

    // populate arguments for calloc call
    calc_call.arguments().push_back(size_expr);
    calc_call.arguments().push_back(size_of_expr);

    // assign it as the initial value
    added_symbol.value = calc_call;
    decl.operands().push_back(calc_call);
  }

  // now we have rule out other special cases
  else if (has_init && !is_inherited)
  {
    if (get_init_expr(ast_node, t, val))
      return true;
    added_symbol.value = val;
    decl.operands().push_back(val);
  }

  // store state variable, which will be initialized in the constructor
  // note that for the state variables that do not have initializer
  // we have already set it as zero value
  if (is_state_var && !is_inherited)
  {
    initializers.push_back(&added_symbol);
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

    // populate the scope_map
    // this map is used to find reference when there is no decl_ref_id provided in the nodes
    // or replace the find_decl_ref in order to speed up
    int scp = struct_def["id"].get<int>();
    scope_map.insert(std::pair<int, std::string>(scp, name));
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

    log_debug(
      "solidity",
      "@@@ got ContractBodyElementT = {}",
      SolidityGrammar::contract_body_element_to_str(type));

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
    {
      exprt tmp_expr;
      if (get_noncontract_decl_ref(*itr, tmp_expr))
        return true;

      struct_typet::componentt comp;
      comp.swap(tmp_expr);
      comp.id("component");
      comp.type().set("#member_name", t.tag());

      if (get_access_from_decl(*itr, comp))
        return true;
      t.components().push_back(comp);
      break;
    }
    case SolidityGrammar::ContractBodyElementT::EnumDef:
    {
      // skip as it do not need to be populated to the value of the struct
      break;
    }
    case SolidityGrammar::ContractBodyElementT::ErrorDef:
    case SolidityGrammar::ContractBodyElementT::EventDef:
    {
      exprt tmp_expr;
      if (get_noncontract_decl_ref(*itr, tmp_expr))
        return true;
      struct_typet::componentt comp;
      comp.swap(tmp_expr);

      if (comp.is_code() && to_code(comp).statement() == "skip")
        break;

      // set virtual / override
      if ((*itr).contains("virtual") && (*itr)["virtual"] == true)
        comp.set("#is_sol_virtual", true);
      else if ((*itr).contains("overrides"))
        comp.set("#is_sol_override", true);

      t.methods().push_back(comp);
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
  // TODO: add bitfield
  // if (comp.type().get_bool("#extint"))
  // {
  //   typet t;
  //   if (get_type_description(ast_node["typeName"]["typeDescriptions"], t))
  //     return true;

  //   comp.type().set("#bitfield", true);
  //   comp.type().subtype() = t;
  //   comp.set_is_unnamed_bitfield(false);
  // }
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

  // set virtual / override
  if (ast_node.contains("virtual") && ast_node["virtual"] == true)
    comp.set("#is_sol_virtual", true);
  else if (ast_node.contains("overrides"))
    comp.set("#is_sol_override", true);

  type.methods().push_back(comp);
  return false;
}

bool solidity_convertert::get_noncontract_decl_ref(
  const nlohmann::json &decl,
  exprt &new_expr)
{
  if (decl["nodeType"] == "StructDefinition")
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

  return false;
}

bool solidity_convertert::get_noncontract_defition(nlohmann::json &ast_node)
{
  std::string node_type = (ast_node)["nodeType"].get<std::string>();
  log_debug(
    "solidity", "@@@ Expecting non-contract definition, got {}", node_type);

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
    add_empty_body_node(ast_node);
    if (get_error_definition(ast_node))
      return true;
  }
  else if (node_type == "EventDefinition")
  {
    add_empty_body_node(ast_node);
    if (get_function_definition(ast_node))
      return true;
  }
  else if (node_type == "ContractDefinition" && ast_node["abstract"] == true)
  {
    // for abstract contract
    add_empty_body_node(ast_node);
  }

  return false;
}

// add a "body" node to funcitons within interfacae && abstract && event
// the idea is to utilize the function-handling APIs.
void solidity_convertert::add_empty_body_node(nlohmann::json &ast_node)
{
  if (ast_node["nodeType"] == "EventDefinition")
  {
    // for event-definition
    if (!ast_node.contains("body"))

      ast_node["body"] = {
        {"nodeType", "Block"},
        {"statements", nlohmann::json::array()},
        {"src", ast_node["src"]}};
  }
  else if (ast_node["contractKind"] == "interface")
  {
    // For interface: functions have no body
    for (auto &subNode : ast_node["nodes"])
    {
      if (
        subNode["nodeType"] == "FunctionDefinition" &&
        !subNode.contains("body"))
        subNode["body"] = {
          {"nodeType", "Block"},
          {"statements", nlohmann::json::array()},
          {"src", ast_node["src"]}};
    }
  }
  else if (ast_node["abstract"] == true)
  {
    // For abstract: functions may or may not have body
    for (auto &subNode : ast_node["nodes"])
    {
      if (
        subNode["nodeType"] == "FunctionDefinition" &&
        !subNode.contains("body"))
        subNode["body"] = {
          {"nodeType", "Block"},
          {"statements", nlohmann::json::array()},
          {"src", ast_node["src"]}};
    }
  }
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
    if (!(*itr).contains("Value"))
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

  if (context.find_symbol(id) != nullptr)
  {
    current_functionDecl = old_functionDecl;
    current_functionName = old_functionName;
    return false;
  }
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

void solidity_convertert::merge_inheritance_ast(
  nlohmann::json &c_node,
  const std::string &c_name,
  std::set<std::string> &merged_list)
{
  // we have merged this contract
  if (merged_list.count(c_name) > 0)
    return;

  if (linearizedBaseList[c_name].size() > 1)
  {
    // this means the contract is inherited from others
    // skip the first one as it's contract itself
    for (auto i_ptr = linearizedBaseList[c_name].begin() + 1;
         i_ptr != linearizedBaseList[c_name].end();
         i_ptr++)
    {
      std::string i_name = exportedSymbolsList[*i_ptr];
      if (linearizedBaseList[i_name].size() > 1)
      {
        if (merged_list.count(i_name) == 0)
        {
          merged_list.insert(i_name);
          merge_inheritance_ast(c_node, i_name, merged_list);
        }
        else
          // we have merged this contract
          continue;
      }

      nlohmann::json i_node = find_decl_ref(*i_ptr);

      // might be abstract contract
      // *@i: incoming node
      // *@c_i: current node
      if (i_node.contains("nodes"))
      {
        for (auto i : i_node["nodes"])
        {
          // skip duplicate
          bool is_dubplicate = false;
          for (const auto &c_i : c_node["nodes"])
          {
            if (c_i.contains("id") && c_i["id"] == i["id"])
            {
              is_dubplicate = true;
              break;
            }
          }
          if (is_dubplicate)
            continue;

          // external & private function/state variable cannot be inherited
          // however, we need to simulate cases like public functions accessing
          // private members. thus we just inherited it and avoid setting them as the
          // entry function for the verification, i.e. set it to private
          if (i.contains("visibility") && i["visibility"] == "external")
            i["visibility"] = "private";

          // skip ctor
          if (i.contains("kind") && i["kind"] == "constructor")
            continue;

          // for virtual/override function
          if (
            i.contains("nodeType") && i["nodeType"] == "FunctionDefinition" &&
            !i["name"].empty())
          {
            // to avoid the name ambiguous/conflict
            // order: current_contract -> most base -> derived
            bool is_conflict = false;
            for (auto &c_i : c_node["node"])
            {
              if (
                c_i.contains("nodeType") &&
                c_i["nodeType"] == "FunctionDefinition" && i["name"] != "" &&
                i["name"] == c_i["name"])
              {
                /*
                    A
                  / \
                  B   C
                  \ /
                    D
                  for cases above, there must be an override inside D if B and C both override A.
                */
                is_conflict = true;

                // if current function is virtual, we replace it with override
                if (c_i["virtual"] == true)
                  c_i = i;

                break;
              }
            }
            if (is_conflict)
              continue;
          }

          // Here we have ruled out the special cases
          // so that we could merge the AST
          i.push_back({"is_inherited", true});
          c_node["nodes"].push_back(i);
        }
      }
    }
  }
}
// add a empty constructor to the contract
bool solidity_convertert::add_implicit_constructor(
  const std::string &contract_name)
{
  std::string name, id;
  name = contract_name;

  // do nothing if there is already an explicit or implicit ctor
  get_ctor_call_id(contract_name, id);
  if (context.find_symbol(id) != nullptr)
    return false;

  // if we reach here, the id must be equal to get_implicit_ctor_id()
  // an implicit constructor is an void empty function
  symbolt dump;
  return get_default_function(name, id, dump);
}

void solidity_convertert::get_temporary_object(exprt &call, exprt &new_expr)
{
  side_effect_exprt tmp_obj("temporary_object", call.type());
  codet code_expr("expression");
  code_expr.operands().push_back(call);
  tmp_obj.initializer(code_expr);
  tmp_obj.location() = call.location();
  call.swap(tmp_obj);
  new_expr = call;
}

// convert the initialization of the state variable
// into the equivalent assignmment in the ctor
bool solidity_convertert::move_initializer_to_ctor(
  const std::string contract_name,
  std::string ctor_id)
{
  log_debug(
    "solidity",
    "@@@ Moving initialization of the state variable to the constructor");

  if (ctor_id.empty())
  {
    if (get_ctor_call_id(contract_name, ctor_id))
      return true;
  }

  symbolt &sym = *context.find_symbol(ctor_id);

  // get this pointer
  exprt base;
  if (get_func_decl_this_ref(ctor_id, base))
    return true;

  // queue insert initialization of the state
  for (auto it = initializers.rbegin(); it != initializers.rend(); ++it)
  {
    symbolt *i = *it;
    log_debug(
      "solidity",
      "\t@@@ initializing symbol {} in the constructor",
      i->name.as_string());
    assert(i->value.id() != "nil");

    if (i->type.id() == typet::id_empty)
    {
      // auxilary
      std::string i_sol_type = i->type.get("#sol_type").as_string();
      assert(!i_sol_type.empty());
      if (i_sol_type == "ARRAY_MEMCPY" || i_sol_type == "STRING_CPY")
      {
        exprt memc = i->value;
        // Array:
        // memcpy(copy, origin, sizeof() * N);
        // since we are handling state var, we need to add the this ptr
        // i.e. memcpy(this->copy, (this->)origin, sizeof() * N);

        // String:
        // strcpy(copy, origin);
        exprt &arg0 = to_side_effect_expr_function_call(memc).arguments().at(0);
        exprt &arg1 = to_side_effect_expr_function_call(memc).arguments().at(1);

        arg0 = member_exprt(base, arg0.name(), arg0.type());
        if (arg1.type().get_bool("#sol_state_var") == true)
          // e.g. uint[] x = y;
          // ? shouldn't we already add the this ptr?
          arg1 = member_exprt(base, arg1.name(), arg1.type());

        convert_expression_to_code(memc);
        sym.value.operands().insert(sym.value.operands().begin(), memc);
      }
    }
    else
    {
      exprt comp = symbol_expr(*i);
      exprt lhs = member_exprt(base, comp.name(), comp.type());
      exprt rhs = i->value;
      exprt _assign;
      if (lhs.type().get("#sol_type") == "STRING")
        get_string_assignment(lhs, rhs, _assign);
      else
      {
        _assign = side_effect_exprt("assign", comp.type());
        _assign.location() = sym.location;
        convert_type_expr(ns, rhs, comp.type());
        _assign.copy_to_operands(lhs, rhs);
      }

      convert_expression_to_code(_assign);
      // insert before the sym.value.operands
      sym.value.operands().insert(sym.value.operands().begin(), _assign);
    }
  }

  // insert parent ctor call in the front
  if (move_inheritance_to_ctor(contract_name, ctor_id, sym))
    return true;

  return false;
}

bool solidity_convertert::move_inheritance_to_ctor(
  const std::string contract_name,
  std::string ctor_id,
  symbolt &sym)
{
  log_debug(
    "solidity",
    "@@@ Moving parents' constructor calls to the current constructor");

  std::string this_id = ctor_id + "#this";
  exprt this_expr = symbol_expr(*context.find_symbol(this_id));

  // queue insert the ctor initializaiton based on the linearizedBaseList
  if (base_contracts != empty_json && context.find_symbol(this_id) != nullptr)
  {
    /*
      Constructors are executed in the following order:
      1 - Base2
      2 - Base1
      3 - Derived3
      contract Derived3 is Base2, Base1 {
          constructor() Base1() Base2() {}
        }

      E.g. 
        contract DD is BB(3)
      Result ctor symbol table:
        Symbol......: c:@S@DD@F@DD#
        Module......: 1
        Base name...: DD
        Mode........: C++
        Type........: constructor  (struct DD *)
        Value.......: 
        {
          BB((struct BB *)this, 3);
        }
      However, since the c++ frontend is broken(esbmc/issues/1866),
      we convert it as 
        function ctor()
        {
          // create temporary object
          Base2 __ESBMC_ctor_Base2_tmp = new Base();
          // copy value
          this.x =  __ESBMC_ctor_Base2_tmp.x ;
          ...
        }
    */

    const std::vector<int> &id_list = linearizedBaseList[contract_name];
    for (auto it = id_list.begin() + 1; it != id_list.end(); ++it)
    {
      // handling inheritance
      // skip the first one as it is the contract itself
      std::string target_c_name = exportedSymbolsList[*it];

      for (const auto &c_node : base_contracts)
      {
        std::string c_name = c_node["baseName"]["name"].get<std::string>();
        if (c_name != target_c_name)
          continue;

        std::string c_ctor_id;
        if (get_ctor_call_id(c_name, c_ctor_id))
          return true;
        exprt c_ctor = symbol_expr(*context.find_symbol(c_ctor_id));
        typet c_type(irept::id_symbol);
        c_type.identifier(prefix + c_name);

        std::string ctor_ins_name = "__ESBMC_ctor_" + c_name + "_tmp";
        //? do we need to set the id?
        std::string ctor_ins_id =
          "sol:@C@" + c_name + "@" + ctor_ins_name + "#";
        locationt ctor_ins_loc = context.find_symbol(ctor_id)->type.location();
        std::string ctor_ins_debug_modulename = current_fileName;
        typet ctor_Ins_typet = symbol_typet(prefix + c_name);

        symbolt ctor_ins_symbol;
        get_default_symbol(
          ctor_ins_symbol,
          ctor_ins_debug_modulename,
          ctor_Ins_typet,
          ctor_ins_name,
          ctor_ins_id,
          ctor_ins_loc);
        ctor_ins_symbol.lvalue = true;
        ctor_ins_symbol.is_extern = false;
        symbolt &added_ctor_symbol = *move_symbol_to_context(ctor_ins_symbol);

        // get value
        // search for the parameter list for the constructor
        // they could be in two places:
        // - contract DD is BB(3)
        // or
        // - constructor() BB(3)
        nlohmann::json c_param_list_node = empty_json;
        if (c_node.contains("arguments"))
          c_param_list_node = c_node;
        else if (ctor_modifier != empty_json)
        {
          for (const auto &c_mdf : ctor_modifier)
          {
            if (c_mdf["modifierName"]["name"].get<std::string>() == c_name)
            {
              c_param_list_node = c_mdf;
              break;
            }
          }
        }

        exprt val;
        if (get_new_object_ctor_call(c_name, c_ctor_id, c_param_list_node, val))
          return true;
        added_ctor_symbol.value = val;

        // insert the declaration
        code_declt decl(symbol_expr(added_ctor_symbol));
        decl.operands().push_back(val);
        sym.value.operands().insert(sym.value.operands().begin(), decl);

        // copy value e.g.  this.data = X.data
        struct_typet type_complete =
          to_struct_type(context.find_symbol(prefix + contract_name)->type);
        struct_typet c_type_complete =
          to_struct_type(context.find_symbol(prefix + c_name)->type);

        exprt lhs;
        exprt rhs;
        exprt _assign;
        for (const auto &c_comp : c_type_complete.components())
        {
          for (const auto &comp : type_complete.components())
          {
            if (c_comp.name() == comp.name())
            {
              lhs = member_exprt(this_expr, comp.name(), comp.type());
              rhs = member_exprt(
                symbol_expr(added_ctor_symbol), c_comp.name(), c_comp.type());
              _assign = side_effect_exprt("assign", comp.type());

              convert_type_expr(ns, rhs, comp.type());
              _assign.copy_to_operands(lhs, rhs);

              convert_expression_to_code(_assign);
              // insert after the object declaration
              sym.value.operands().insert(
                sym.value.operands().begin() + 1, _assign);
              break;
            }
          }
        }
      }
    }
  }
  return false;
}

// for the contract-type variable that does not have initialization
bool solidity_convertert::get_instantiation_ctor_call(
  const std::string &contract_name,
  exprt &new_expr)
{
  // 1. add the ctor function symbol
  std::string name, id;
  name = contract_name;
  id = "sol:@C@" + contract_name + "@F@" + contract_name + "#";

  code_typet type;
  typet tmp_rtn_type("constructor");
  type.return_type() = tmp_rtn_type;
  type.set("#member_name", prefix + contract_name);
  type.set("copy_cons", 1);

  locationt location_begin;

  if (current_fileName == "")
    return true;
  std::string debug_modulename = current_fileName;

  symbolt symbol;
  get_default_symbol(symbol, debug_modulename, type, name, id, location_begin);

  symbol.lvalue = true;
  symbol.is_extern = false;
  symbol.file_local = false;

  auto &added_symbol = *move_symbol_to_context(symbol);

  // add empty body
  added_symbol.value = nil_exprt();

  // add this pointer as the first function param
  get_function_this_pointer_param(
    contract_name, id, debug_modulename, location_begin, type);

  // add "int* p" as the second function param
  // as there is no var_ptr in solidity, we will not have conflict definition
  typet param_type = pointer_typet(int_type());

  // the name and id can be hard-coded since they will not be referred
  std::string p_name = "p";
  std::string p_id =
    "sol:@C@" + contract_name + "@F@" + contract_name + "@" + p_name + "#";
  symbolt param_symbol;
  get_default_symbol(
    param_symbol, debug_modulename, param_type, p_name, p_id, location_begin);
  param_symbol.lvalue = true;
  param_symbol.is_parameter = true;
  param_symbol.file_local = true;
  move_symbol_to_context(param_symbol);

  auto param = code_typet::argumentt();
  param.type() = param_type;
  param.cmt_base_name(p_name);
  param.cmt_identifier(p_id);
  param.location() = location_begin;

  // update the param
  type.arguments().push_back(param);
  added_symbol.type = type;

  // ? we do not need to populate the initializer
  // 2. construct the ctor call
  if (get_new_object_ctor_call(contract_name, id, empty_json, new_expr))
    return true;

  return false;
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

  // Check intrinsic functions
  if (check_intrinsic_function(ast_node))
    return false;

  // 3. Set current_scope_var_num, current_functionDecl and old_functionDecl
  current_scope_var_num = 1;
  const nlohmann::json *old_functionDecl = current_functionDecl;
  const std::string old_functionName = current_functionName;

  current_functionDecl = &ast_node;

  bool is_ctor = (*current_functionDecl)["name"].get<std::string>() == "" &&
                 (*current_functionDecl).contains("kind") &&
                 (*current_functionDecl)["kind"] == "constructor";

  // store constructor initialization list
  if (is_ctor && !(*current_functionDecl)["modifiers"].empty())
    ctor_modifier = (*current_functionDecl)["modifiers"];

  if (is_ctor)
  {
    // for construcotr
    if (get_current_contract_name(*current_functionDecl, current_functionName))
      return true;
  }
  else
    current_functionName = (*current_functionDecl)["name"].get<std::string>();

  // TODO: handle override: e.g.
  // contract D is B, C {
  // // D.foo() returns "C"
  // // since C is the right most parent contract with function foo()
  //     function foo() public pure override(B, C) returns (string memory) {
  //         return super.foo();
  //     }
  // }

  // 4. Return type
  code_typet type;
  if (is_ctor)
  {
    typet tmp_rtn_type("constructor");
    type.return_type() = tmp_rtn_type;
    type.set("#member_name", prefix + current_contractName);
  }
  else if (ast_node.contains("returnParameters"))
  {
    if (get_type_description(ast_node["returnParameters"], type.return_type()))
      return true;
  }
  else
  {
    type.return_type() = empty_typet();
    type.return_type().set("cpp_type", "void");
    type.set("#member_name", prefix + current_contractName);
  }

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

  if (context.find_symbol(id) != nullptr)
  {
    current_functionDecl = old_functionDecl;
    current_functionName = old_functionName;
    return false;
  }

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

  // 11.1 add this pointer as the first param
  if (ast_node["nodeType"].get<std::string>() != "EventDefinition")
    get_function_this_pointer_param(
      current_contractName, id, debug_modulename, location_begin, type);

  // 11.2 parse other params
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

  added_symbol.type = type;

  // 12. Convert body and embed the body into the same symbol
  // skip for 'unimplemented' functions which has no body,
  // e.g. asbstract/interface, the symbol value would be left as unset
  if (
    ast_node.contains("body") ||
    (ast_node.contains("implemented") && ast_node["implemented"] == true))
  {
    exprt body_exprt;
    if (get_block(ast_node["body"], body_exprt))
      return true;

    added_symbol.value = body_exprt;
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
  get_local_var_decl_name(pd, name, id);

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

      if (!current_blockDecl.operands().empty())
      {
        for (auto op : current_blockDecl.operands())
        {
          convert_expression_to_code(op);
          _block.operands().push_back(op);
        }
        current_blockDecl.clear();
      }

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
  case SolidityGrammar::BlockT::BlockTError:
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
    if (get_expr(
          stmt["expression"], stmt["expression"]["typeDescriptions"], new_expr))
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
        // hack: we need the expression block, not tuple instance
        current_lhsDecl = true;
        exprt rhs;
        if (get_expr(stmt["expression"], rhs))
          return true;
        current_lhsDecl = false;

        size_t ls = to_struct_type(lhs.type()).components().size();
        size_t rs = rhs.operands().size();
        if (ls != rs)
        {
          log_debug(
            "soldiity",
            "Handling return tuple.\nlhs = {}\nrhs = {}",
            lhs.to_string(),
            rhs.to_string());
          log_error("Internal tuple error.");
        }

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
          get_tuple_assignment(current_blockDecl, lop, rop);
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
        get_tuple_function_call(current_blockDecl, func_call);

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
          get_tuple_assignment(current_blockDecl, lop, rop);
        }
      }
      // do return in the end
      exprt return_expr = code_returnt();
      current_blockDecl.move_to_operands(return_expr);

      new_expr = code_skipt();
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
        else if (
          decl["nodeType"] == "StructDefinition" ||
          decl["nodeType"] == "ErrorDefinition" ||
          decl["nodeType"] == "EventDefinition")
        {
          if (get_noncontract_decl_ref(decl, new_expr))
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

        new_expr.type().set("#sol_type", "BYTES_LITERAL");
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

        new_expr.type().set("#sol_type", "BYTES_LITERAL");
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
        new_expr.type().set("#sol_type", "INT_CONST");
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
      new_expr.type().set("#sol_type", "ADDRESS");
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
    //                         x = [1];  x = [1,2];
    //    2. Operator:
    //        - (x+1) % 2
    //        - if( x && (y || z) )
    //    3. TupleExpr:
    //        - multiple returns: return (x, y);
    //        - swap: (x, y) = (y, x)
    //        - constant: (1, 2)

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
      inits.type().set("#sol_type", "ARRAY_LITERAL");
      inits.type().set("#sol_array_size", size.cformat().as_string());

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
      inits.id("array");
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

      if (current_lhsDecl)
      {
        // avoid nested
        assert(!current_rhsDecl);

        // we do not create struct-tuple instance for lhs
        code_blockt _block;
        exprt op;
        for (auto i : expr["components"])
        {
          if (get_expr(i, i["typeDescriptions"], op))
            return true;
          if (op.is_nil())
            continue;
          _block.operands().push_back(op);
        }
        new_expr = _block;
      }
      else
      {
        // 1. construct struct type
        if (get_tuple_definition(expr))
          return true;

        //2. construct struct_type instance
        if (get_tuple_instance(expr, new_expr))
          return true;
      }

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
  case SolidityGrammar::ExpressionT::CallExprClass:
  {
    side_effect_expr_function_callt call;
    const nlohmann::json &callee_expr_json = expr["expression"];

    // * we first do special cases handling
    // * check if it's a solidity built-in function
    if (
      !get_esbmc_builtin_ref(callee_expr_json, new_expr) ||
      !get_sol_builtin_ref(expr, new_expr))
    {
      typet type = to_code_type(new_expr.type()).return_type();
      call.function() = new_expr;
      call.type() = type;

      if (
        new_expr.type().get("#sol_name").as_string().find("revert") !=
        std::string::npos)
      {
        // Special case: revert
        // insert a bool false as the first argument.
        // drop the rest of params.
        call.arguments().push_back(false_exprt());
      }
      else if (
        new_expr.type().get("#sol_name").as_string().find("require") !=
        std::string::npos)
      {
        // Special case: require
        // __ESBMC_assume only handle one param.
        exprt single_arg;
        if (get_expr(
              expr["arguments"].at(0),
              expr["arguments"].at(0)["typeDescriptions"],
              single_arg))
          return true;
        call.arguments().push_back(single_arg);
      }
      else
      {
        if (get_function_call(new_expr, type, empty_json, expr, call))
          return true;
      }

      new_expr = call;
      break;
    }

    // * check if it's a member access call
    if (
      callee_expr_json.contains("nodeType") &&
      callee_expr_json["nodeType"] == "MemberAccess")
    {
      // ContractMemberCall
      // - x.setAddress();
      // - x.address();
      // The later one is quite special, as in Solidity variables behave like functions from the perspective of other contracts.
      // e.g. b._addr is not an address, but a function that returns an address.
      is_contract_member_access = true;

      const nlohmann::json &caller_expr_json = callee_expr_json["expression"];
      assert(callee_expr_json.contains("referencedDeclaration"));
      assert(caller_expr_json.contains("referencedDeclaration"));

      const int contract_var_id =
        caller_expr_json["referencedDeclaration"].get<int>();
      const nlohmann::json &base_expr_json =
        find_decl_ref(contract_var_id); // contract

      const int member_id =
        callee_expr_json["referencedDeclaration"].get<int>();
      const nlohmann::json &member_decl_ref =
        find_decl_ref(member_id); // methods or variables
      if (base_expr_json.empty() || member_decl_ref.empty())
        return true;

      auto elem_type =
        SolidityGrammar::get_contract_body_element_t(member_decl_ref);
      switch (elem_type)
      {
      case SolidityGrammar::VarDecl:
      {
        // e.g. x.data()
        // ==> x.data, where data is a state variable in the contract
        // in Solidity the x.data() is read-only
        exprt base;
        if (get_var_decl_ref(base_expr_json, base))
          return true;

        exprt comp;
        if (get_var_decl_ref(member_decl_ref, comp))
          return true;

        // comp can be either symbol_expr or member_expr
        const irep_idt comp_name =
          comp.name().empty() ? comp.component_name() : comp.name();

        new_expr = member_exprt(base, comp_name, comp.type());

        break;
      }
      case SolidityGrammar::FunctionDef:
      {
        // e.g. x.func()
        // x    --> base
        // func --> comp
        exprt base;
        if (get_var_decl_ref(base_expr_json, base))
          return true;

        exprt comp;
        if (get_func_decl_ref(member_decl_ref, comp))
          return true;

        // note that here is comp.identifier not comp.name
        exprt mem_access = member_exprt(base, comp.identifier(), comp.type());
        // obtain the type of return value
        code_typet t;
        if (get_type_description(
              member_decl_ref["returnParameters"], t.return_type()))
          return true;

        if (get_function_call(mem_access, t, member_decl_ref, expr, call))
          return true;

        new_expr = call;
        break;
      }
      default:
      {
        log_error(
          "Unexpected Member Access Element Type, Got {}",
          SolidityGrammar::contract_body_element_to_str(elem_type));
        return true;
      }
      }

      is_contract_member_access = false;
      break;
    }

    // wrap it in an ImplicitCastExpr to perform conversion of FunctionToPointerDecay
    nlohmann::json implicit_cast_expr =
      make_implicit_cast_expr(callee_expr_json, "FunctionToPointerDecay");
    exprt callee_expr;
    if (get_expr(implicit_cast_expr, callee_expr))
      return true;

    // * check if it's a struct call
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

    const auto &caller_expr_json =
      find_decl_ref(callee_expr_json["referencedDeclaration"].get<int>());
    std::string node_type = caller_expr_json["nodeType"].get<std::string>();

    // * check if it's a event, error function call
    if (node_type == "EventDefinition" || node_type == "ErrorDefinition")
    {
      if (get_function_call(callee_expr, type, empty_json, expr, call))
        return true;
      new_expr = call;
      break;
    }

    // * check if it's the funciton inside library node
    //TODO

    // * we had ruled out all the special cases
    // * we now confirm it is called by aother contract inside current contract
    // * func() ==> current_func_this.func(&current_func_this);
    exprt base;
    assert(current_functionDecl);
    if (get_func_decl_this_ref(*current_functionDecl, base))
      return true;

    exprt mem_access =
      member_exprt(base, callee_expr.identifier(), callee_expr.type());

    if (get_function_call(mem_access, type, caller_expr_json, expr, call))
      return true;

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
    const nlohmann::json &base_json = expr["baseExpression"];
    const nlohmann::json &index_json = expr["indexExpression"];

    // 1. get type, this is the base type of array
    typet t;
    if (get_type_description(expr["typeDescriptions"], t))
      return true;

    // for MAPPING
    typet base_t;
    if (get_type_description(base_json["typeDescriptions"], base_t))
      return true;
    if (base_t.get("#sol_type").as_string() == "MAPPING")
    {
      // this could be set or get
      // e.g. y = map[x] or map[x] = y

      // find mapping definition
      assert(base_json.contains("referencedDeclaration"));
      nlohmann::json map_node =
        find_decl_ref(base_json["referencedDeclaration"].get<int>());

      // add mapping key linked list
      // 1. get mapping id
      std::string m_name, m_id;
      if (get_var_decl_name(map_node, m_name, m_id))
        return true;
      // 2. get mapping symbol
      if (context.find_symbol(m_id) == nullptr)
        return true;
      symbolt &m_sym = *context.find_symbol(m_id);
      // 3. get mapping key type
      typet key_t;
      if (get_type_description(
            map_node["typeName"]["keyType"]["typeDescriptions"], key_t))
        return true;

      // get mapping key
      std::string sol_type = key_t.get("#sol_type").as_string();
      std::string postfix = "U"; // uint
      if (sol_type.compare(0, 3, "INT") == 0)
        postfix = "I"; // int
      exprt key_node_instance;
      if (get_mapping_key_expr(m_sym, postfix, key_node_instance))
        return true;

      // get base
      exprt base;
      if (get_expr(base_json, base_json["typeDescriptions"], base))
        return true;

      // if it's string, conert it to member access
      // e.g. (_ESBMC_MAPPING_STRING)x.value()[i]
      if (base.type().subtype().get("#sol_type") == "_ESBMC_MAPPING_STRING")
      {
        struct_typet st = to_struct_type(
          (*context.find_symbol("tag-struct _ESBMC_MAPPING_STRING")).type);
        exprt comp = st.components().at(0);
        base = member_exprt(base, comp.name(), comp.type());
      }

      // get pos
      exprt pos;
      if (get_expr(index_json, expr["typeDescriptions"], pos))
        return true;

      // typecast: convert xx => uint
      if (sol_type == "STRING" || sol_type == "STRING_LITERAL")
      {
        // convert string hex literal
        // if there is already a hexValue in the node
        if (index_json.contains("hexValue"))
        {
          std::string hex_val = index_json["hexValue"].get<std::string>();
          if (convert_hex_literal(hex_val, pos))
            return true;
        }
        // otherwise we need to convert a string to decimal ASCII values
        else
        {
          // signed char * c:@F@str2int
          // e.g. "Geek" => "1197827435"
          // pos => str2int(pos)
          side_effect_expr_function_callt _call;
          get_library_function_call(
            "str2int",
            "c:@F@str2int",
            unsignedbv_typet(256),
            base.location(),
            _call);

          // insert arguments
          _call.arguments().push_back(pos);
          pos = _call;
        }
      }

      // obtain key from the key linked list
      // e.g. x[10] ==> x[findKey(&key_x, 10)]
      side_effect_expr_function_callt _findkey;
      if (postfix == "I")
        get_library_function_call(
          "findKeyI",
          "c:@F@findKeyI",
          signedbv_typet(256),
          base.location(),
          _findkey);
      else
        get_library_function_call(
          "findKeyU",
          "c:@F@findKeyU",
          unsignedbv_typet(256),
          base.location(),
          _findkey);

      // this->key_x
      exprt this_ptr = base.op0();
      exprt mem_key = member_exprt(
        this_ptr, key_node_instance.name(), key_node_instance.type());

      // findKey(this->key_x, this->pos);
      _findkey.arguments().push_back(mem_key);
      _findkey.arguments().push_back(pos);

      new_expr = index_exprt(base, _findkey);
      break;
    }

    // for BYTESN, where the index access is read-only
    if (is_bytes_type(t))
    {
      // this means we are dealing with bytes type
      // jump out if it's "bytes[]" or "bytesN[]" or "func()[]"
      SolidityGrammar::TypeNameT tname =
        SolidityGrammar::get_type_name_t(base_json["typeDescriptions"]);
      if (
        !(tname == SolidityGrammar::ArrayTypeName ||
          tname == SolidityGrammar::DynArrayTypeName) &&
        base_json.contains("referencedDeclaration"))
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

        const nlohmann::json &decl =
          find_decl_ref(base_json["referencedDeclaration"].get<int>());
        if (decl == empty_json)
          return true;

        if (get_var_decl_ref(decl, src_val))
          return true;

        if (get_expr(index_json, expr["typeDescriptions"], src_offset))
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
    if (base_json.contains("referencedDeclaration"))
    {
      if (get_expr(base_json, literal_type, array))
        return true;
    }
    else
    {
      // 2.2 func()[n]
      const nlohmann::json &decl = base_json;
      nlohmann::json implicit_cast_expr =
        make_implicit_cast_expr(decl, "ArrayToPointerDecay");
      if (get_expr(implicit_cast_expr, literal_type, array))
        return true;
    }

    // 3. get the position index
    exprt pos;
    if (get_expr(index_json, expr["typeDescriptions"], pos))
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
    //    uint[] memory a = new uint[](len);
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
    exprt call;
    if (get_new_object_ctor_call(expr, call))
      return true;

    new_expr = call;
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

// get the initial value for the variable declaration
bool solidity_convertert::get_init_expr(
  const nlohmann::json &ast_node,
  const typet &dest_type,
  exprt &new_expr)
{
  nlohmann::json init_value =
    ast_node.contains("value") ? ast_node["value"] : ast_node["initialValue"];
  nlohmann::json literal_type = ast_node["typeDescriptions"];

  if (literal_type == nullptr)
    return true;

  if (get_expr(init_value, literal_type, new_expr))
    return true;

  convert_type_expr(ns, new_expr, dest_type);
  return false;
}

// get the contract name that contains the ast_node
// note that the contract_name might be empty
bool solidity_convertert::get_current_contract_name(
  const nlohmann::json &ast_node,
  std::string &contract_name)
{
  // we set it as empty first
  contract_name = "";

  // implicit constructor
  if (ast_node.empty())
  {
    contract_name = current_contractName;
    return false;
  }

  if (!is_contract_member_access)
  {
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
        else
          // for definitions that are outside contract
          contract_name = "";
      }
      else
      {
        std::string tmp = "";
        find_decl_ref(ref_id, tmp);
        if (tmp != "")
          contract_name = current_contractName;
      }
      return false;
    }
    return true;
  }
  else
  {
    if (ast_node.contains("scope"))
    {
      int scope_id = ast_node["scope"];

      if (exportedSymbolsList.count(scope_id))
      {
        std::string c_name = exportedSymbolsList[scope_id];
        if (linearizedBaseList.count(c_name))
        {
          // this is to make sure it's indeed a contract
          // as only contract has inheritance
          contract_name = c_name;
          return false;
        }
      }
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
        else
          // for definitions that are outside contract
          contract_name = "";
      }
      else
        find_decl_ref(ref_id, contract_name);
      return false;
    }

    // unexpected
    return true;
  }
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

    current_lhsDecl = true;
    if (get_expr(expr["leftHandSide"], lhs))
      return true;
    current_lhsDecl = false;

    current_rhsDecl = true;
    if (get_expr(expr["rightHandSide"], literalType, rhs))
      return true;
    current_rhsDecl = false;
  }
  else if (expr.contains("leftExpression"))
  {
    nlohmann::json literalType_l = expr["leftExpression"]["typeDescriptions"];
    nlohmann::json literalType_r = expr["rightExpression"]["typeDescriptions"];

    current_lhsDecl = true;
    if (get_expr(expr["leftExpression"], literalType_l, lhs))
      return true;
    current_lhsDecl = false;

    current_rhsDecl = true;
    if (get_expr(expr["rightExpression"], literalType_r, rhs))
      return true;
    current_rhsDecl = false;
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
    typet lt = lhs.type();
    typet rt = rhs.type();
    std::string lt_sol = lt.get("#sol_type").as_string();
    std::string rt_sol = rt.get("#sol_type").as_string();

    // special handling for tuple-type assignment;
    //TODO: handle nested tuple
    if (rt_sol == "TUPLE_INSTANCE" || rt_sol == "TUPLE")
    {
      log_debug("solidity", "Handling tuple assignment.");

      assert(lt.is_code() && to_code(lhs).statement() == "block");
      if (rt_sol == "TUPLE")
      {
        // e.g. (x,y) = func(); (x,y) = func(func2()); (x, (x,y)) = (x, func());
        // ==>
        //    func(); // this initializes the tuple instance
        //    x = tuple.mem0;
        //    y = tuple.mem1;
        exprt new_rhs;
        if (get_tuple_function_ref(
              expr["rightHandSide"]["expression"], new_rhs))
          return true;

        // add function call
        get_tuple_function_call(current_blockDecl, rhs);
        rhs = new_rhs;
      }

      // e.g. (x,y) = (1,2); (x,y) = (func(),x);
      // =>
      //  t.mem0 = 1; #1
      //  t.mem1 = 2; #2
      //  x = t.mem0; #3
      //  y = t.mem1; #4
      // where #1 and #2 are already in the current_blockdecl

      // do #3 #4
      std::set<exprt> assigned_symbol;
      for (size_t i = 0; i < lhs.operands().size(); i++)
      {
        // e.g. (, x) = (1, 2)
        //      null <=> tuple2.mem0
        //         x <=> tuple2.mem1
        exprt lop = lhs.operands().at(i);
        if (!lop.is_nil() && assigned_symbol.count(lop))
          // e.g. (x,x) = (1, 2); assert(x==1) hold
          // we skip the variable that has been assigned
          continue;
        assigned_symbol.insert(lop);

        exprt rop;
        if (get_tuple_member_call(
              rhs.identifier(),
              to_struct_type(rhs.type()).components().at(i),
              rop))
          return true;

        get_tuple_assignment(current_blockDecl, lop, rop);
      }

      new_expr = code_skipt();
      current_BinOp_type.pop();
      return false;
    }
    else if (rt_sol == "ARRAY_LITERAL" || rt_sol == "ARRAY")
    {
      std::string arr_size = "0";
      if (!rt.get("#sol_array_size").empty())
        arr_size = rt.get("#sol_array_size").as_string();
      else if (rt.has_subtype() && !rt.subtype().get("#sol_array_size").empty())
        arr_size = rt.subtype().get("#sol_array_size").as_string();
      else
      {
        log_error("cannot get the size of the fixed array");
        return true;
      }
      exprt size_expr = constant_exprt(
        integer2binary(string2integer(arr_size), bv_width(uint_type())),
        arr_size,
        uint_type());

      // get sizeof
      exprt size_of_expr;
      get_size_of_expr(rt.subtype(), size_of_expr);

      // do array copy
      side_effect_expr_function_callt acpy_call;
      get_arrcpy_function_call(lhs.location(), acpy_call);
      acpy_call.arguments().push_back(rhs);
      acpy_call.arguments().push_back(size_expr);
      acpy_call.arguments().push_back(size_of_expr);
      solidity_gen_typecast(ns, acpy_call, lt);

      rhs = acpy_call;
    }
    else if (rt_sol == "DYNARRAY")
    {
      // get size
      // TODO: set dynarray as a struct to store size
      std::string arr_size = "0";
      exprt size_expr = constant_exprt(
        integer2binary(string2integer(arr_size), bv_width(uint_type())),
        arr_size,
        uint_type());

      // get sizeof
      exprt size_of_expr;
      get_size_of_expr(rt.subtype(), size_of_expr);

      // do array copy
      side_effect_expr_function_callt acpy_call;
      get_arrcpy_function_call(lhs.location(), acpy_call);
      acpy_call.arguments().push_back(rhs);
      acpy_call.arguments().push_back(size_expr);
      acpy_call.arguments().push_back(size_of_expr);
      solidity_gen_typecast(ns, acpy_call, lt);

      rhs = acpy_call;
      // fall through to do assignment
    }
    else if (lt_sol == "STRING")
    {
      get_string_assignment(lhs, rhs, new_expr);
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
  if (get_var_decl_name(decl, name, id))
    return true;

  if (context.find_symbol(id) != nullptr)
    new_expr = symbol_expr(*context.find_symbol(id));
  else
  {
    typet type;
    if (is_mapping(decl))
    {
      exprt map;
      if (get_var_decl(decl, map))
        return true;
      assert(to_code(map).operands().size() >= 1);
      type = to_code(map).op0().type();
    }
    else
    {
      const nlohmann::json *old_typeName = current_typeName;
      current_typeName = &decl["typeName"];
      if (get_type_description(decl["typeName"]["typeDescriptions"], type))
        return true;
      current_typeName = old_typeName;
    }

    // variable with no value
    new_expr = exprt("symbol", type);
    new_expr.identifier(id);
    new_expr.cmt_lvalue(true);
    new_expr.name(name);
    new_expr.pretty_name(name);
  }

  if (decl["stateVariable"] && current_functionDecl)
  {
    // this means we are parsing function body
    // and the variable is a state var
    // data = _data ==> this->data = _data;

    // get function this pointer
    exprt this_ptr;
    if (get_func_decl_this_ref(*current_functionDecl, this_ptr))
      return true;

    // construct member access this->data
    new_expr = member_exprt(this_ptr, new_expr.name(), new_expr.type());
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
bool solidity_convertert::get_func_decl_this_ref(
  const nlohmann::json &decl,
  exprt &new_expr)
{
  assert(!decl.empty());
  std::string func_name, func_id;
  get_function_definition_name(decl, func_name, func_id);

  return get_func_decl_this_ref(func_id, new_expr);
}

bool solidity_convertert::get_func_decl_this_ref(
  const std::string &func_id,
  exprt &new_expr)
{
  std::string this_id = func_id + "#this";

  if (context.find_symbol(this_id) == nullptr)
    return true;

  new_expr = symbol_expr(*context.find_symbol(this_id));

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
  if (
    decl.contains("referencedDeclaration") &&
    decl["referencedDeclaration"].get<int>() >= 0)
    return true;

  if (!decl.contains("name"))
    return get_sol_builtin_ref(decl, new_expr);

  const std::string blt_name = decl["name"].get<std::string>();
  std::string name, id;

  // "require" keyword is virtually identical to "assume"
  if (blt_name == "require" || blt_name == "revert")
    name = "__ESBMC_assume";
  else if (
    blt_name == "assert" || name == "__ESBMC_assert" ||
    name == "__VERIFIER_asert" || name == "__ESBMC_assume" ||
    name == "__VERIFIER_assume")
    name = blt_name;
  else
    //!assume it's a solidity built-in func
    return get_sol_builtin_ref(decl, new_expr);
  id = name;

  // manually unrolled recursion here
  // type config for Builtin && Int
  typet type;
  // Creat a new code_typet, parse the return_type and copy the code_typet to typet
  code_typet convert_type;
  typet return_type;

  // clang's assert(.) uses "signed_int" as assert(.) type (NOT the argument type),
  // while Solidity's assert uses "bool" as assert(.) type (NOT the argument type).
  return_type = bool_type();
  std::string c_type = "bool";
  return_type.set("#cpp_type", c_type);
  convert_type.return_type() = return_type;

  if (!convert_type.arguments().size())
    convert_type.make_ellipsis();

  type = convert_type;
  type.set("#sol_name", blt_name);

  new_expr = exprt("symbol", type);
  new_expr.identifier(id);
  new_expr.cmt_lvalue(true);
  new_expr.name(name);

  locationt loc;
  get_location_from_decl(decl, loc);
  new_expr.location() = loc;
  if (current_functionDecl)
    new_expr.location().function(current_functionName);

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

  std::string typeIdentifier;
  std::string typeString;

  if (type_name.contains("typeIdentifier"))
    typeIdentifier = type_name["typeIdentifier"].get<std::string>();
  if (type_name.contains("typeString"))
    typeString = type_name["typeString"].get<std::string>();

  switch (type)
  {
  case SolidityGrammar::TypeNameT::ElementaryTypeName:
  {
    // rule state-variable-declaration
    if (get_elementary_type_name(type_name, new_type))
      return true;
    break;
  }
  case SolidityGrammar::TypeNameT::ParameterList:
  {
    // rule parameter-list
    // Used for Solidity function parameter or return list
    if (get_parameter_list(type_name, new_type))
      return true;
    break;
  }
  case SolidityGrammar::TypeNameT::Pointer:
  {
    // auxiliary type: pointer (FuncToPtr decay)
    // This part is for FunctionToPointer decay only
    assert(
      typeString.find("function") != std::string::npos ||
      typeString.find("contract") != std::string::npos);

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
    assert(typeIdentifier.find("ArrayToPtr") != std::string::npos);

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
  case SolidityGrammar::TypeNameT::DynArrayTypeName:
  {
    // Deal with array with constant size, e.g., int a[2]; Similar to clang::Type::ConstantArray
    // array's typeDescription is in a compact form, e.g.:
    //    "typeIdentifier": "t_array$_t_uint8_$2_storage_ptr",
    //    "typeString": "uint8[2]"
    // We need to extract the elementary type of array from the information provided above
    // We want to make it like ["baseType"]["typeDescriptions"]

    typet the_type;
    exprt the_size;
    if (current_typeName != nullptr)
    {
      // access from get_var_decl
      assert((*current_typeName).contains("baseType"));
      if (get_type_description(
            (*current_typeName)["baseType"]["typeDescriptions"], the_type))
        return true;

      new_type = gen_pointer_type(the_type);
      if ((*current_typeName).contains("length"))
      {
        assert(type == SolidityGrammar::TypeNameT::ArrayTypeName);
        std::string length =
          (*current_typeName)["length"]["value"].get<std::string>();
        new_type.set("#sol_array_size", length);
        new_type.set("#sol_type", "ARRAY");
      }
      else
      {
        assert(type == SolidityGrammar::TypeNameT::DynArrayTypeName);
        new_type.set("#sol_type", "DYNARRAY");
      }
    }
    else if (type == SolidityGrammar::TypeNameT::ArrayTypeName)
    {
      // for tuple array
      nlohmann::json array_elementary_type =
        make_array_elementary_type(type_name);

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
      new_type.set("#sol_array_size", the_size);
      new_type.set("#sol_type", "ARRAY_LITERAL");
    }
    else
    {
      // e.g.
      // "typeDescriptions": {
      //     "typeIdentifier": "t_array$_t_uint256_$dyn_memory_ptr",
      //     "typeString": "uint256[]"

      // 1. rebuild baseType
      nlohmann::json new_json;
      std::string temp = typeString;
      auto pos = temp.find("[]"); // e.g. "uint256[] memory"
      const std::string new_typeString = temp.substr(0, pos);
      const std::string new_typeIdentifier = "t_" + new_typeString;
      new_json["typeString"] = new_typeString;
      new_json["typeIdentifier"] = new_typeIdentifier;

      // 2. get subType
      typet sub_type;
      if (get_type_description(new_json, sub_type))
        return true;

      // 3. make pointer
      new_type = gen_pointer_type(sub_type);
      new_type.set("#sol_type", "DYNARRAY");
    }

    break;
  }
  case SolidityGrammar::TypeNameT::ContractTypeName:
  {
    // e.g. ContractName tmp = new ContractName(Args);

    std::string constructor_name = typeString;
    size_t pos = constructor_name.find(" ");
    std::string id = prefix + constructor_name.substr(pos + 1);

    if (context.find_symbol(id) == nullptr)
      return true;

    new_type = symbol_typet(id);
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

    // convert it back to ElementaryTypeName by removing the "type" prefix
    std::size_t begin = typeIdentifier.find("$_");
    std::size_t end = typeIdentifier.rfind("_$");
    std::string new_typeIdentifier =
      typeIdentifier.substr(begin + 2, end - begin - 2);

    begin = typeString.find("type(");
    end = typeString.rfind(")");
    std::string new_typeString = typeString.substr(begin + 5, end - begin - 5);

    new_json["typeIdentifier"] = new_typeIdentifier;
    new_json["typeString"] = new_typeString;

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
    std::string delimiter = " ";

    int cnt = 1;
    std::string token;
    std::string _typeString = typeString;

    // extract the seconde string
    while (cnt >= 0)
    {
      if (_typeString.find(delimiter) == std::string::npos)
      {
        token = _typeString;
        break;
      }
      size_t pos = _typeString.find(delimiter);
      token = _typeString.substr(0, pos);
      _typeString.erase(0, pos + delimiter.length());
      cnt--;
    }

    const std::string id = prefix + "struct " + token;

    if (context.find_symbol(id) == nullptr)
    {
      // if struct is not parsed, handle the struct first
      // extract the decl ref id
      std::string new_typeIdentifier = typeIdentifier;
      new_typeIdentifier.replace(
        new_typeIdentifier.find("t_struct$_"), sizeof("t_struct$_") - 1, "");

      auto pos_1 = new_typeIdentifier.find("$");
      auto pos_2 = new_typeIdentifier.find("_storage");

      const int ref_id = stoi(new_typeIdentifier.substr(pos_1 + 1, pos_2));
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

  // set data location
  if (typeIdentifier.find("_memory_ptr") != std::string::npos)
    new_type.set("#sol_data_loc", "memory");
  else if (typeIdentifier.find("_storage_ptr") != std::string::npos)
    new_type.set("#sol_data_loc", "storage");
  else if (typeIdentifier.find("_calldata_ptr") != std::string::npos)
    new_type.set("#sol_data_loc", "calldata");

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

  log_debug(
    "solidity",
    "\t@@@ got SolidityGrammar::FunctionDeclRefT = {}",
    SolidityGrammar::func_decl_ref_to_str(type));

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
  symbol.static_lifetime = true;
  symbol.file_local = true;
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
    // follow the naming rule defined in get_local_var_decl_name
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

  // get type
  typet t = context.find_symbol(id)->type;
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
  symbol.static_lifetime = true;
  symbol.file_local = true;

  symbol.value = gen_zero(get_complete_type(t, ns), true);
  symbol.value.zero_initializer(true);
  symbolt &added_symbol = *move_symbol_to_context(symbol);
  new_expr = symbol_expr(added_symbol);
  new_expr.identifier(id);

  if (!ast_node.contains("components"))
  {
    // assume it's function return parameter list
    // therefore no initial value
    return false;
  }

  // do assignment
  auto &args = ast_node["components"];

  size_t i = 0;
  size_t j = 0;
  unsigned is = to_struct_type(t).components().size();
  unsigned as = args.size();
  assert(is <= as);

  exprt comp;
  exprt member_access;
  while (i < is && j < as)
  {
    if (args.at(j).is_null())
    {
      ++j;
      continue;
    }

    comp = to_struct_type(t).components().at(i);
    if (get_tuple_member_call(id, comp, member_access))
      return true;

    exprt init;
    const nlohmann::json &litera_type = args.at(j)["typeDescriptions"];

    if (get_expr(args.at(j), litera_type, init))
      return true;

    get_tuple_assignment(current_blockDecl, member_access, init);

    // update
    ++i;
    ++j;
  }

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

// Knowing that there is a component x in the struct_tuple_instance A, we construct A.x
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

void solidity_convertert::get_string_assignment(
  const exprt &lhs,
  const exprt &rhs,
  exprt &new_expr)
{
  side_effect_expr_function_callt call;
  get_streq_function_call(lhs.location(), call);
  call.arguments().push_back(lhs);
  call.arguments().push_back(rhs);
  new_expr = call;
}

void solidity_convertert::get_tuple_assignment(
  code_blockt &_block,
  const exprt &lop,
  exprt rop)
{
  exprt assign_expr;
  if (lop.type().get("#sol_type") == "STRING")
    get_string_assignment(lop, rop, assign_expr);
  else
  {
    assign_expr = side_effect_exprt("assign", lop.type());
    convert_type_expr(ns, rop, lop.type());
    assign_expr.copy_to_operands(lop, rop);
  }
  convert_expression_to_code(assign_expr);
  _block.move_to_operands(assign_expr);
}

bool solidity_convertert::get_mapping_type(
  const nlohmann::json &ast_node,
  typet &t)
{
  // value type:
  // 1. int/uint
  // 2. string => struct _ESBMC_MAPPING_STRING
  // 3. address => uint160
  // 4. contract/struct => struct type
  // 4. bool
  //TODO: support nested structure

  // get element type
  typet elem_type;
  if (get_type_description(
        ast_node["typeName"]["valueType"]["typeDescriptions"], elem_type))
    return true;

  if (elem_type.get("#sol_type") == "STRING")
  {
    // TODO: FIXME! We treat string as uint256
    elem_type = unsignedbv_typet(256);
    elem_type.set("#sol_type", "STRING_UINT");
  }

  //TODO set as infinite array. E.g.
  //   array
  //    * size: infinity
  //        * type: unsignedbv
  //            * width: 64
  //    * subtype: bool
  //        * #cpp_type: bool
  // t = array_typet(elem_type, exprt("infinity"));
  // t.set("#sol_type", "MAPPING");

  // For now, we set it as a relatively large array
  // if the value_length is too large, the efficiency will be affected.
  // BigInt value_length = 50;
  // t = array_typet(
  //   elem_type,
  //   constant_exprt(
  //     integer2binary(value_length, bv_width(unsignedbv_typet(8))),
  //     integer2string(value_length),
  //     unsignedbv_typet(8)));

  t = pointer_typet(elem_type);
  // ? MAPPING_INSTANCE?
  t.set("#sol_type", "MAPPING");

  return false;
}

bool solidity_convertert::get_mapping_key_expr(
  const symbolt &sym,
  const std::string &postfix,
  exprt &new_expr)
{
  std::string name, id;
  get_mapping_key_name(sym.name.as_string(), sym.id.as_string(), name, id);
  if (context.find_symbol(id) != nullptr)
  {
    new_expr = symbol_expr(*context.find_symbol(id));
    return false;
  }

  std::string struct_node_id = "tag-struct Node" + postfix;
  if (context.find_symbol(struct_node_id) == nullptr)
    return true;
  exprt node = symbol_expr(*context.find_symbol(struct_node_id));

  // struct Node *
  typet t = pointer_typet(node.type());

  symbolt symbol;
  get_default_symbol(symbol, sym.module.as_string(), t, name, id, sym.location);
  symbol.static_lifetime = false;
  symbol.lvalue = true;
  symbol.file_local = false;
  symbol.is_extern = true;

  // set default value
  // e.g. struct Node *x = NULL;
  symbol.value = gen_zero(get_complete_type(t, ns), true);
  symbol.value.zero_initializer(true);

  // add to symbol table
  symbolt &added_sym = *move_symbol_to_context(symbol);

  // move it to the struct
  assert(!current_contractName.empty());
  std::string struct_id = prefix + current_contractName;
  if (context.find_symbol(struct_id) == nullptr)
    return true;
  auto &struct_sym = *context.find_symbol(struct_id);

  //? check duplicate?
  exprt tmp_expr = exprt("symbol", symbol_expr(added_sym).type());
  tmp_expr.identifier(id);
  tmp_expr.cmt_lvalue(true);
  tmp_expr.name(name);
  tmp_expr.pretty_name(name);
  new_expr = tmp_expr;

  struct_typet::componentt comp;
  comp.swap(tmp_expr);
  comp.id("component");
  comp.type().set("#member_name", struct_sym.type.tag());
  comp.set_access("private");

  to_struct_type(struct_sym.type).components().push_back(comp);

  // move it to the initialize list
  initializers.push_back(&added_sym);

  return false;
}

void solidity_convertert::get_mapping_key_name(
  const std::string &m_name,
  const std::string &m_id,
  std::string &k_name,
  std::string &k_id)
{
  k_name = m_name + "_key";
  k_id = m_id + "#key";
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

void solidity_convertert::get_malloc_function_call(
  const locationt &loc,
  side_effect_expr_function_callt &malc_call)
{
  const std::string malc_name = "malloc";
  const std::string malc_id = "c:@F@malloc";
  const symbolt &malc_sym = *context.find_symbol(malc_id);
  get_library_function_call(
    malc_name, malc_id, symbol_expr(malc_sym).type(), loc, malc_call);
}

void solidity_convertert::get_calloc_function_call(
  const locationt &loc,
  side_effect_expr_function_callt &calc_call)
{
  const std::string calc_name = "calloc";
  const std::string calc_id = "c:@F@calloc";
  const symbolt &calc_sym = *context.find_symbol(calc_id);
  get_library_function_call(
    calc_name, calc_id, symbol_expr(calc_sym).type(), loc, calc_call);
}

void solidity_convertert::get_arrcpy_function_call(
  const locationt &loc,
  side_effect_expr_function_callt &calc_call)
{
  const std::string calc_name = "arrcpy";
  const std::string calc_id = "c:@F@arrcpy";
  const symbolt &calc_sym = *context.find_symbol(calc_id);
  get_library_function_call(
    calc_name, calc_id, symbol_expr(calc_sym).type(), loc, calc_call);
}

void solidity_convertert::get_strcpy_function_call(
  const locationt &loc,
  side_effect_expr_function_callt &stry_call)
{
  const std::string stry_name = "strcpy";
  const std::string stry_id = "c:@F@strcpy";
  const symbolt &stry_sym = *context.find_symbol(stry_id);
  get_library_function_call(
    stry_name, stry_id, symbol_expr(stry_sym).type(), loc, stry_call);
}

void solidity_convertert::get_streq_function_call(
  const locationt &loc,
  side_effect_expr_function_callt &_call)
{
  const std::string func_name = "_streq";
  const std::string func_id = "c:@F@_streq#&$@N@std@S@string#*1C#";
  const symbolt &func_sym = *context.find_symbol(func_id);
  get_library_function_call(
    func_name, func_id, symbol_expr(func_sym).type(), loc, _call);
}

void solidity_convertert::get_memcpy_function_call(
  const locationt &loc,
  side_effect_expr_function_callt &memc_call)
{
  const std::string memc_name = "memcpy";
  const std::string memc_id = "c:@F@memcpy";
  const symbolt &memc_sym = *context.find_symbol(memc_id);
  get_library_function_call(
    memc_name, memc_id, symbol_expr(memc_sym).type(), loc, memc_call);
}

// check if the function is a library function (defined in solidity.h)
bool solidity_convertert::is_library_function(const std::string &id)
{
  if (context.find_symbol(id) == nullptr)
    return false;
  if (id.compare(0, 3, "c:@") == 0)
    return true;
  return false;
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
    // cpp: std::string str;
    new_type = context.find_symbol("tag-std::string")->type;
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

  //TODO set #extint
  // switch (type)
  // {
  // case SolidityGrammar::ElementaryTypeNameT::BOOL:
  // case SolidityGrammar::ElementaryTypeNameT::STRING:
  // {
  //   break;
  // }
  // default:
  // {
  //   new_type.set("#extint", true);
  //   break;
  // }
  // }

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
    assert(type_name["parameters"].size() == 1);

    const nlohmann::json &rtn_type = type_name["parameters"].at(0);
    if (rtn_type.contains("typeName"))
    {
      const nlohmann::json *old_typeName = current_typeName;
      current_typeName = &rtn_type["typeName"];
      if (get_type_description(
            rtn_type["typeName"]["typeDescriptions"], new_type))
        return true;
      current_typeName = old_typeName;
    }
    else
    {
      if (get_type_description(rtn_type["typeDescriptions"], new_type))
        return true;
    }

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

bool solidity_convertert::get_var_decl_name(
  const nlohmann::json &decl,
  std::string &name,
  std::string &id)
{
  if (decl["stateVariable"])
    get_state_var_decl_name(decl, name, id);
  else
  {
    std::string c_name;
    if (get_current_contract_name(decl, c_name))
      return true;
    if (c_name.empty() && decl["mutability"] == "constant")
      // global variable
      get_state_var_decl_name(decl, name, id);
    else
      get_local_var_decl_name(decl, name, id);
  }

  return false;
}

// parse the non-state variable
void solidity_convertert::get_local_var_decl_name(
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
    //TODO: add current_StructDecl/ current_ErrorDecl
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
    // In solidity
    // - constructor does not have a name
    // - there can be only one constructor in each contract
    // we, however, mimic the C++ grammar to manually assign it with a name
    // whichi is identical to the contract name
    // we also allows multiple constructor where the added ctor has no  `id`
    name = contract_name;
    id = "sol:@C@" + contract_name + "@F@" + name + "#" +
         i2string(ast_node["id"].get<std::int16_t>());
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

// get the constructor symbol id
bool solidity_convertert::get_ctor_call_id(
  const std::string &contract_name,
  std::string &ctor_id)
{
  // we first try to find the explicit constructor defined in the source file.
  ctor_id = get_explicit_ctor_call_id(contract_name);
  if (ctor_id.empty())
    // then we try to find the implicit constructor we manually added
    ctor_id = get_implict_ctor_call_id(contract_name);

  if (context.find_symbol(ctor_id) == nullptr)
  {
    // this means the neither explicit nor implicit constructor is found
    return true;
  }
  return false;
}

// get the explicit constructor symbol id
// retrun empty string if no explicit ctor
std::string
solidity_convertert::get_explicit_ctor_call_id(const std::string &contract_name)
{
  if (!exportedSymbolsList.empty())
  {
    // get contract id from exportedSymbolsList
    for (auto i = exportedSymbolsList.begin(); i != exportedSymbolsList.end();
         i++)
    {
      if (i->second == contract_name)
      {
        int contract_ref_id = i->first;
        // get the constructor
        auto ctor_ref = find_constructor_ref(contract_ref_id);
        if (!ctor_ref.empty())
        {
          int id = ctor_ref["id"].get<int>();
          return "sol:@C@" + contract_name + "@F@" + contract_name + "#" +
                 std::to_string(id);
        }
      }
    }
  }

  // not found
  return "";
}

// get the implicit constructor symbol id
std::string
solidity_convertert::get_implict_ctor_call_id(const std::string &contract_name)
{
  // for implicit ctor, the id is manually set as 0
  return "sol:@C@" + contract_name + "@F@" + contract_name + "#0";
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
    log_error("Unsupported node type when getting src from JSON");
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
    ast_node["stateVariable"] == false && current_functionDecl)
  {
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
    // builtin functions such as "assert"
    return empty_json;

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

  if (
    current_functionDecl != nullptr && (*current_functionDecl).contains("body"))
  {
    // Then search "declarations" in current function scope
    const nlohmann::json &current_func = *current_functionDecl;

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

// return construcor node based on the *contract* id
const nlohmann::json &solidity_convertert::find_constructor_ref(int contract_id)
{
  nlohmann::json &nodes = src_ast_json["nodes"];
  unsigned index = 0;
  for (nlohmann::json::iterator itr = nodes.begin(); itr != nodes.end();
       ++itr, ++index)
  {
    if (
      (*itr)["id"].get<int>() == contract_id &&
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

void solidity_convertert::get_size_of_expr(const typet &t, exprt &size_of_expr)
{
  size_of_expr = exprt("sizeof", size_type());
  typet elem_type = t;
  if (elem_type.is_struct())
  {
    struct_union_typet st = to_struct_union_type(elem_type);
    elem_type = symbol_typet(prefix + st.tag().as_string());
  }
  size_of_expr.set("#c_sizeof_type", elem_type);
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

/** 
    * @param type: return type
    * @param decl_ref: the function declaration node
    * @param expr: the function caller node which contains the arguments
    For this pointer:
    - if the function is called by aother contract inside current contract
      func() ==> this_func.func(&this_func,)
    - if the function is called via temporary object in another contract 
      x.func() ==> x.func(&x,)
    TODO: if the paramenter is a 'memory' type, we need to create
    a copy. E.g. string memory x => char *x => char * x_cpy
    this could be done by memcpy. However, for dyn_array, we do not have 
    the size info. Thus in the future we need to convert the dyn array to
    a struct which record both array and size. This will also help us to support
    array.length, .push and .pop 
**/
bool solidity_convertert::get_function_call(
  const exprt &func,
  const typet &t,
  const nlohmann::json &decl_ref,
  const nlohmann::json &expr,
  side_effect_expr_function_callt &call)
{
  call.function() = func;
  call.type() = t;
  call.location() = func.location();
  if (current_functionDecl)
    call.location().function(current_functionName);

  // populate params
  if (decl_ref.empty() && !expr.empty())
  {
    // * Assume it is a solidity built in function, which we cannot find the declartion
    // * in the json_node, thus the decl_ref left emtpy

    nlohmann::json param = nullptr;
    // for builtin functions which have no this pointer
    for (const auto &arg : expr["arguments"].items())
    {
      exprt single_arg;
      if (arg.value().contains("commonType"))
        param = arg.value()["commonType"];
      else if (arg.value().contains("typeDescriptions"))
        param = arg.value()["typeDescriptions"];

      if (get_expr(arg.value(), param, single_arg))
        return true;

      call.arguments().push_back(single_arg);
      param = nullptr;
    }
  }
  else if (!decl_ref.empty() && !expr.empty())
  {
    // * Assume it is a normal funciton call, including ctor call with params

    // set caller object as the first argument
    exprt this_object;
    if ((func.id() == "member"))
      this_object = func.op0();
    else if (
      func.type().is_code() && to_code_type(func.type()).arguments().size() > 0)
    {
      const auto tmp_arg = to_code_type(func.type()).arguments().at(0);
      assert(tmp_arg.get("#base_name").as_string() == "this");
      exprt temporary = exprt("new_object");
      temporary.set("#lvalue", true);
      temporary.type() = tmp_arg.type().subtype();
      this_object = temporary;
    }
    else
    {
      log_error("Unexpected function call scheme\n{}", func.to_string());
      return true;
    }
    call.arguments().push_back(this_object);

    nlohmann::json param_nodes = decl_ref["parameters"]["parameters"];
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
  }
  else if (decl_ref.empty() && expr.empty())
  {
    // * assume this is constructor call we manually setup
    // * during the multi-transaction verification

    /*
      get this pointer
        0: address_of
          * type: pointer
            * subtype: symbol
                * identifier: tag-BB
          * operands: 
          0: new_object
              * type: symbol
                  * identifier: tag-BB
              * #lvalue: 1
    */

    code_typet tmp = to_code_type(func.type());
    // populate nil arguements
    call.arguments().resize(
      tmp.arguments().size(), static_cast<const exprt &>(get_nil_irep()));

    if ((func.id() == "member"))
    {
      auto this_object = func.op0();
      // set this
      call.arguments().at(0) = this_object;
    }
    else
    {
      const auto tmp_arg = to_code_type(func.type()).arguments().at(0);
      assert(tmp_arg.get("#base_name").as_string() == "this");
      exprt temporary = exprt("new_object");
      temporary.set("#lvalue", true);
      temporary.type() = tmp_arg.type().subtype();
      address_of_exprt this_object(temporary);
      // set this
      call.arguments().at(0) = this_object;
    }
  }
  else
  {
    log_error("Unexpected function call scheme");
    return true;
  }

  return false;
}

bool solidity_convertert::get_new_object_ctor_call(
  const nlohmann::json &ast_node,
  exprt &new_expr)
{
  // we need to obtain the real contract name
  is_contract_member_access = true;

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

  // get the constuctor symbol
  if (get_func_decl_ref(constructor_ref, callee))
    return true;

  // obtain the type info
  // e.g.
  //  * type: symbol
  //    * identifier: tag-Base
  std::string id = prefix + contract_name;
  typet type = symbol_typet(id);

  // setup initializer
  side_effect_expr_function_callt call;
  if (get_function_call(callee, type, constructor_ref, ast_node, call))
    return true;
  call.function().set("constructor", 1);

  // construct temporary object
  get_temporary_object(call, new_expr);

  // reset
  is_contract_member_access = false;

  return false;
}

bool solidity_convertert::get_new_object_ctor_call(
  const std::string &contract_name,
  const std::string &ctor_id,
  const nlohmann::json param_list,
  exprt &new_expr)
{
  assert(linearizedBaseList.count(contract_name) && !contract_name.empty());
  exprt ctor = symbol_expr(*context.find_symbol(ctor_id));
  std::string id = prefix + contract_name;
  typet type(irept::id_symbol);
  type.identifier(id);

  // setup initializer
  side_effect_expr_function_callt call;
  if (get_function_call(ctor, type, empty_json, empty_json, call))
    return true;
  if (param_list != empty_json)
  {
    // two round funciton call:
    // first round: add this pointer dereference
    // second round: add params
    if (get_function_call(ctor, type, empty_json, param_list, call))
      return true;
  }
  call.function().set("constructor", 1);

  // construct temporary object
  get_temporary_object(call, new_expr);
  return false;
}

bool solidity_convertert::get_implicit_ctor_ref(
  exprt &new_expr,
  const std::string &contract_name)
{
  // to obtain the type info
  std::string name, id;
  name = contract_name;
  id = get_implict_ctor_call_id(contract_name);
  if (context.find_symbol(id) == nullptr)
    return true;

  if (get_new_object_ctor_call(contract_name, id, empty_json, new_expr))
    return true;

  return false;
}

/*
  construct a void function with empty function body
  then add this function to symbol table
*/
bool solidity_convertert::get_default_function(
  const std::string name,
  const std::string id,
  symbolt &added_symbol)
{
  code_typet type;
  type.return_type() = empty_typet();
  type.return_type().set("cpp_type", "void");

  locationt location_begin;

  if (current_fileName == "")
    return true;
  std::string debug_modulename = current_fileName;

  symbolt symbol;
  get_default_symbol(symbol, debug_modulename, type, name, id, location_begin);

  symbol.lvalue = true;
  symbol.is_extern = false;
  symbol.file_local = false;

  auto &sym = *move_symbol_to_context(symbol);

  code_blockt body_exprt = code_blockt();
  sym.value = body_exprt;

  // add this pointer
  get_function_this_pointer_param(
    current_contractName, id, debug_modulename, location_begin, type);

  sym.type = type;
  added_symbol = sym;

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
  typet src_type = src_expr.type();
  if (src_type != dest_type)
  {
    std::string src_sol_type = src_type.get("#sol_type").as_string();
    std::string dest_sol_type = dest_type.get("#sol_type").as_string();

    // only do conversion when the src.type != dest.type
    if (is_bytes_type(src_type) && is_bytes_type(dest_type))
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
      sub_bswap_expr = exprt("bswap", src_type);
      sub_bswap_expr.operands().push_back(src_expr);

      // 2. typecast
      solidity_gen_typecast(ns, sub_bswap_expr, dest_type);

      // 3. bswap back
      bswap_expr = exprt("bswap", sub_bswap_expr.type());
      bswap_expr.operands().push_back(sub_bswap_expr);

      src_expr = bswap_expr;
    }
    else if (
      (src_sol_type == "ARRAY_LITERAL") && src_type.id() == typet::id_array)
    {
      // this means we are handling a src constant array
      // which should be assigned to an array pointer
      if (dest_type.id() != typet::id_pointer)
      {
        log_error(
          "Expecting dest_type to be pointer type, got = {}",
          dest_type.id().as_string());
        abort();
      }

      // dynamic: uint x[] = [1,2]
      // fixed:   uint x[3] = [1,2], whose rhs array is incomplete and need to add zero element
      // the goal is to convert the rhs constant array to a static global var

      // get rhs constant array size
      const std::string src_size = src_type.get("#sol_array_size").as_string();
      if (src_size.empty())
      {
        // e.g. a = new uint[](len);
        // we have already populate the auxiliary state var so
        // skip the rest of the process
        // ? solidity_gen_typecast(ns, src_expr, dest_type);
        return;
      }
      unsigned z_src_size = std::stoul(src_size, nullptr);

      // get lhs array size
      std::string dest_size;
      if (dest_sol_type == "ARRAY")
      {
        dest_size = dest_type.get("#sol_array_size").as_string();
        assert(!dest_size.empty());
      }
      else
      {
        // the dynamic array does not have a fixed length
        // therefore set it as the rhs length
        dest_size = src_size;
      }
      unsigned z_dest_size = std::stoul(dest_size, nullptr);
      constant_exprt dest_array_size = constant_exprt(
        integer2binary(z_dest_size, bv_width(int_type())),
        integer2string(z_dest_size),
        int_type());

      if (src_expr.id() == irept::id_member)
      {
        // e.g. uint[3] x;  (x, y) = ([1,z], ...)
        // where [1,2] ==> uint8[] ==> tuple_instance.mem0
        // ==>
        //  x  = [（uint256)tuple_instance.mem0[0], （uint256)tuple_instance.mem0[1], 0]
        // - src_expr: [1, z]
        // - dest_type: uint*
        array_typet arr_t = array_typet(dest_type.subtype(), dest_array_size);
        exprt new_arr = exprt(irept::id_array, arr_t);

        exprt arr_comp;
        for (unsigned i = 0; i < z_src_size; i++)
        {
          // do array index
          exprt idx = constant_exprt(
            integer2binary(i, bv_width(size_type())),
            integer2string(i),
            size_type());
          exprt op = index_exprt(src_expr, idx, src_type.subtype());

          arr_comp = typecast_exprt(op, dest_type.subtype());
          new_arr.operands().push_back(arr_comp);
        }

        src_expr = new_arr;
        src_type = new_arr.type();
      }

      // allow fall-through
      if (src_expr.id() == irept::id_array)
      {
        // e.g. uint[3] x = [1] ==> uint[3] x == [1,0,0]
        unsigned s_size = src_expr.operands().size();
        if (s_size != z_src_size)
        {
          log_error(
            "Expecting equivalent array size, got {} and {}",
            std::to_string(s_size),
            std::to_string(z_src_size));
          abort();
        }
        if (z_dest_size > s_size)
        {
          for (unsigned i = 0; i < s_size; i++)
          {
            exprt &op = src_expr.operands().at(i);
            solidity_gen_typecast(ns, op, dest_type.subtype());
          }
          exprt _zero =
            gen_zero(get_complete_type(dest_type.subtype(), ns), true);
          _zero.location() = src_expr.location();
          _zero.set("#cformat", 0);
          // push zero
          for (unsigned i = s_size; i < z_dest_size; i++)
            src_expr.operands().push_back(_zero);

          // reset size
          assert(src_expr.type().is_array());
          to_array_type(src_expr.type()).size() = dest_array_size;
        }
      }

      // since it's a array-constant/string-constant, we could safely make it to a local var
      // this local var will not be referred again so the name could be random.
      // e.g.
      // int[3] p = [1,2];
      // => int *p = [1,2,3];
      // => static int[3] tmp1 = [1,2,3];
      // return: src_expr = symbol_expr(tmp1)
      exprt new_expr;
      get_aux_array(src_expr, new_expr);
      src_expr = new_expr;
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

void solidity_convertert::get_aux_array_name(
  std::string &aux_name,
  std::string &aux_id)
{
  do
  {
    aux_name = "__ESBMC_aux_array" + std::to_string(aux_counter);
    aux_id = "sol:@" + aux_name;
    ++aux_counter;
  } while (context.find_symbol(aux_id) != nullptr);
}

void solidity_convertert::get_aux_array(const exprt &src_expr, exprt &new_expr)
{
  if (
    src_expr.name().as_string().find("__ESBMC_aux_array") != std::string::npos)
  {
    // skip if it's already a aux array
    new_expr = src_expr;
    return;
  }
  std::string aux_name;
  std::string aux_id;
  get_aux_array_name(aux_name, aux_id);

  locationt loc = src_expr.location();
  std::string debug_modulename = "C++";
  symbolt sym;
  get_default_symbol(
    sym, debug_modulename, src_expr.type(), aux_name, aux_id, loc);
  sym.static_lifetime = true;
  sym.is_extern = false;
  sym.lvalue = false;

  symbolt &added_symbol = *move_symbol_to_context(sym);

  added_symbol.value = src_expr;
  new_expr = symbol_expr(added_symbol);
}

// convert new array rhs
// e.g. uint[] x = new uint(len + 1);
bool solidity_convertert::get_empty_array_ref(
  const nlohmann::json &expr,
  exprt &new_expr)
{
  // Get Name
  nlohmann::json callee_expr_json = expr["expression"];
  nlohmann::json callee_arg_json = expr["arguments"][0];

  // Get name, id;
  std::string name, id;
  get_aux_array_name(name, id);

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
  side_effect_expr_function_callt calc_call;
  get_calloc_function_call(location_begin, calc_call);

  exprt size_of_expr;
  get_size_of_expr(elem_type, size_of_expr);

  calc_call.arguments().push_back(size);
  calc_call.arguments().push_back(size_of_expr);
  new_expr = calc_call;
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
  log_debug(
    "Solidity",
    "@@@ performs transaction verification on contract {}",
    contractName);

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

  // 1.1 get contract symbol ("tag-contractName")
  auto it = id_list.begin();
  std::string c_name = exportedSymbolsList[*it];
  const std::string id = prefix + c_name;
  if (context.find_symbol(id) == nullptr)
    return true;
  const symbolt &contract = *context.find_symbol(id);
  assert(contract.type.is_struct() && "A contract should be a struct");

  // 1.2 construct a temporary object and move to func_body
  // e.g. Base x = new Base();

  std::string ctor_ins_name = "__ESBMC_tmp";
  std::string ctor_ins_id = "sol:@C@" + c_name + "@" + ctor_ins_name + "#";
  locationt ctor_ins_loc;
  std::string ctor_ins_debug_modulename = current_fileName;
  typet ctor_Ins_typet = symbol_typet(prefix + c_name);

  symbolt ctor_ins_symbol;
  get_default_symbol(
    ctor_ins_symbol,
    ctor_ins_debug_modulename,
    ctor_Ins_typet,
    ctor_ins_name,
    ctor_ins_id,
    ctor_ins_loc);
  ctor_ins_symbol.lvalue = true;
  ctor_ins_symbol.is_extern = false;

  symbolt &added_ctor_symbol = *move_symbol_to_context(ctor_ins_symbol);

  // get value
  std::string ctor_id;
  if (get_ctor_call_id(c_name, ctor_id))
    return true;

  exprt ctor;
  if (get_new_object_ctor_call(c_name, ctor_id, empty_json, ctor))
    return true;

  code_declt decl(symbol_expr(added_ctor_symbol));
  added_ctor_symbol.value = ctor;
  decl.operands().push_back(ctor);

  // move to "sol_main" body
  func_body.move_to_operands(decl);

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
    // get func_decl_ref
    if (context.find_symbol(func_id) == nullptr)
      return true;
    const exprt func = symbol_expr(*context.find_symbol(func_id));

    // get __ESBMC_tmp_ ref
    const exprt contract_var = symbol_expr(added_ctor_symbol);

    // do member access
    exprt mem_access =
      member_exprt(contract_var, func.identifier(), func.type());

    side_effect_expr_function_callt then_expr;
    if (get_function_call(
          mem_access,
          to_code_type(func.type()).return_type(),
          empty_json,
          empty_json,
          then_expr))
      return true;

    // set &__ESBMC_tmp as the first argument
    // which overwrite the this pointer
    then_expr.arguments().at(0) = contract_var;
    convert_expression_to_code(then_expr);

    // ifthenelse-statement:
    codet if_expr("ifthenelse");
    if_expr.copy_to_operands(guard_expr, then_expr);

    // move to while-loop body
    while_body.move_to_operands(if_expr);
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
  const symbolt &_contract = *context.find_symbol(prefix + contractName);
  new_symbol.location = _contract.location;
  std::string debug_modulename =
    get_modulename_from_path(_contract.location.file().as_string());
  get_default_symbol(
    new_symbol,
    debug_modulename,
    main_type,
    sol_name,
    sol_id,
    _contract.location);

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
    {
      //! Assume is not a contract (e.g. error type)
      continue;
    }

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