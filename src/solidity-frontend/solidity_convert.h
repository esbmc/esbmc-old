#ifndef SOLIDITY_FRONTEND_SOLIDITY_CONVERT_H_
#define SOLIDITY_FRONTEND_SOLIDITY_CONVERT_H_

#include <memory>
#include <stack>
#include <vector>
#include <map>
#include <queue>
#include <util/context.h>
#include <util/namespace.h>
#include <util/std_types.h>
#include <util/std_code.h>
#include <nlohmann/json.hpp>
#include <solidity-frontend/solidity_grammar.h>
#include <solidity-frontend/pattern_check.h>

class solidity_convertert
{
public:
  solidity_convertert(
    contextt &_context,
    nlohmann::json &_ast_json,
    const std::string &_sol_func,
    const std::string &_contract_path);
  virtual ~solidity_convertert() = default;

  bool convert();

protected:
  void multi_json_file();
  std::vector<nlohmann::json> topological_sort(
    std::unordered_map<std::string, std::unordered_set<std::string>> &graph,
    std::unordered_map<std::string, nlohmann::json> &path_to_json);
  bool convert_ast_nodes(const nlohmann::json &contract_def);

  // conversion functions
  // get decl in rule contract-body-element
  bool get_non_function_decl(const nlohmann::json &ast_node, exprt &new_expr);
  bool get_function_decl(const nlohmann::json &ast_node);
  // get decl in rule variable-declaration-statement, e.g. function local declaration
  bool get_var_decl_stmt(const nlohmann::json &ast_node, exprt &new_expr);
  bool get_var_decl(const nlohmann::json &ast_node, exprt &new_expr);
  bool get_function_definition(const nlohmann::json &ast_node);
  bool get_function_params(const nlohmann::json &pd, exprt &param);
  bool get_default_function(const std::string name, const std::string id);

  // handle the non-contract definition, including struct/enum/error/event/abstract/...
  bool get_noncontract_defition(nlohmann::json &ast_node);
  bool get_struct_class(const nlohmann::json &ast_node);
  void add_enum_member_val(nlohmann::json &ast_node);
  bool get_error_definition(const nlohmann::json &ast_node);
  void add_empty_function_body(nlohmann::json &ast_node);

  // handle the implicit constructor
  bool add_implicit_constructor();
  bool get_implicit_ctor_ref(exprt &new_expr, const std::string &contract_name);
  bool
  get_struct_class_fields(const nlohmann::json &ast_node, struct_typet &type);
  bool
  get_struct_class_method(const nlohmann::json &ast_node, struct_typet &type);
  bool get_access_from_decl(
    const nlohmann::json &ast_node,
    struct_typet::componentt &comp);
  bool get_block(
    const nlohmann::json &expr,
    exprt &
      new_expr); // For Solidity's mutually inclusive: rule block and rule statement
  bool get_statement(const nlohmann::json &block, exprt &new_expr);
  bool get_expr(const nlohmann::json &expr, exprt &new_expr);
  bool get_expr(
    const nlohmann::json &expr,
    const nlohmann::json &expr_common_type,
    exprt &new_expr);
  bool get_binary_operator_expr(const nlohmann::json &expr, exprt &new_expr);
  bool get_compound_assign_expr(const nlohmann::json &expr, exprt &new_expr);
  bool get_unary_operator_expr(
    const nlohmann::json &expr,
    const nlohmann::json &literal_type,
    exprt &new_expr);
  bool
  get_conditional_operator_expr(const nlohmann::json &expr, exprt &new_expr);
  bool get_cast_expr(
    const nlohmann::json &cast_expr,
    exprt &new_expr,
    const nlohmann::json literal_type = nullptr);
  bool get_var_decl_ref(const nlohmann::json &decl, exprt &new_expr);
  bool get_func_decl_ref(const nlohmann::json &decl, exprt &new_expr);
  bool get_enum_member_ref(const nlohmann::json &decl, exprt &new_expr);
  bool get_esbmc_builtin_ref(const nlohmann::json &decl, exprt &new_expr);
  bool get_type_description(const nlohmann::json &type_name, typet &new_type);
  bool get_func_decl_ref_type(const nlohmann::json &decl, typet &new_type);
  bool get_array_to_pointer_type(const nlohmann::json &decl, typet &new_type);
  bool
  get_elementary_type_name(const nlohmann::json &type_name, typet &new_type);
  bool get_parameter_list(const nlohmann::json &type_name, typet &new_type);
  void get_state_var_decl_name(
    const nlohmann::json &ast_node,
    std::string &name,
    std::string &id);
  void get_var_decl_name(
    const nlohmann::json &ast_node,
    std::string &name,
    std::string &id);
  void get_function_definition_name(
    const nlohmann::json &ast_node,
    std::string &name,
    std::string &id);
  bool get_constructor_call(const nlohmann::json &ast_node, exprt &new_expr);
  bool get_current_contract_name(
    const nlohmann::json &ast_node,
    std::string &contract_name);
  void get_library_function_call(
    const std::string &func_name,
    const std::string &func_id,
    const typet &t,
    const locationt &l,
    exprt &new_expr);
  bool get_empty_array_ref(const nlohmann::json &ast_node, exprt &new_expr);

  // tuple
  bool get_tuple_definition(const nlohmann::json &ast_node);
  bool get_tuple_instance(const nlohmann::json &ast_node, exprt &new_expr);
  void get_tuple_name(
    const nlohmann::json &ast_node,
    std::string &name,
    std::string &id);
  bool get_tuple_instance_name(
    const nlohmann::json &ast_node,
    std::string &name,
    std::string &id);
  bool get_tuple_function_ref(const nlohmann::json &ast_node, exprt &new_expr);
  bool get_tuple_member_call(
    const irep_idt instance_id,
    const exprt &comp,
    exprt &new_expr);
  void get_tuple_assignment(code_blockt &_block, const exprt &lop, exprt rop);
  void get_tuple_function_call(code_blockt &_block, const exprt &op);

  // mapping
  bool get_mapping_definition(const nlohmann::json &ast_node, exprt &new_expr);
  bool get_mapping_value_type(const typet &val_type, std::string &_val);
  bool get_mapping_key(const nlohmann::json &ast_node, exprt &new_expr);
  bool move_mapping_to_ctor();

  // line number and locations
  void
  get_location_from_decl(const nlohmann::json &ast_node, locationt &location);
  void get_start_location_from_stmt(
    const nlohmann::json &ast_node,
    locationt &location);
  void get_final_location_from_stmt(
    const nlohmann::json &ast_node,
    locationt &location);
  unsigned int
  get_line_number(const nlohmann::json &ast_node, bool final_position = false);
  unsigned int add_offset(const std::string &src, unsigned int start_position);
  std::string get_src_from_json(const nlohmann::json &ast_node);

  symbolt *move_symbol_to_context(symbolt &symbol);
  bool multi_transaction_verification(const std::string &contractName);
  bool multi_contract_verification();

  // auxiliary functions
  std::string get_modulename_from_path(std::string path);
  std::string get_filename_from_path(std::string path);
  const nlohmann::json &find_decl_ref(int ref_decl_id);
  const nlohmann::json &
  find_decl_ref(int ref_decl_id, std::string &contract_name);
  const nlohmann::json &find_constructor_ref(int ref_decl_id);
  void convert_expression_to_code(exprt &expr);
  bool check_intrinsic_function(const nlohmann::json &ast_node);
  nlohmann::json make_implicit_cast_expr(
    const nlohmann::json &sub_expr,
    std::string cast_type);
  nlohmann::json make_return_type_from_typet(typet type);
  nlohmann::json make_pointee_type(const nlohmann::json &sub_expr);
  nlohmann::json make_array_elementary_type(const nlohmann::json &type_descrpt);
  nlohmann::json make_array_to_pointer_type(const nlohmann::json &type_descrpt);
  std::string get_array_size(const nlohmann::json &type_descrpt);
  bool is_dyn_array(const nlohmann::json &json_in);
  nlohmann::json add_dyn_array_size_expr(
    const nlohmann::json &type_descriptor,
    const nlohmann::json &dyn_array_node);
  bool is_mapping(const nlohmann::json &ast_node);

  void get_default_symbol(
    symbolt &symbol,
    std::string module_name,
    typet type,
    std::string name,
    std::string id,
    locationt location);

  std::string get_ctor_call_id(const std::string &contract_name);
  bool get_sol_builtin_ref(const nlohmann::json expr, exprt &new_expr);

  // literal conversion functions
  bool convert_integer_literal(
    const nlohmann::json &integer_literal,
    std::string the_value,
    exprt &dest);
  bool convert_bool_literal(
    const nlohmann::json &bool_literal,
    std::string the_value,
    exprt &dest);
  bool convert_string_literal(std::string the_value, exprt &dest);
  void convert_type_expr(const namespacet &ns, exprt &dest, const typet &type);
  bool
  convert_hex_literal(std::string the_value, exprt &dest, const int n = 256);
  // check if it's a bytes type
  bool is_bytes_type(const typet &t);

  contextt &context;
  namespacet ns;
  // json for Solidity AST. Use vector for multiple contracts
  nlohmann::json src_ast_json_array = nlohmann::json::array();
  // json for Solidity AST. Use object for single contract
  nlohmann::json src_ast_json;
  // Solidity function to be verified
  const std::string &sol_func;
  //smart contract source file
  const std::string &contract_path;

  std::string absolute_path;
  std::string contract_contents = "";
  // scope id of "ContractDefinition"
  int global_scope_id;

  unsigned int current_scope_var_num;
  const nlohmann::json *current_functionDecl;
  const nlohmann::json *current_forStmt;
  // Use current level of BinOp type as the "anchor" type for numerical literal conversion:
  // In order to remove the unnecessary implicit IntegralCast. We need type of current level of BinaryOperator.
  // All numeric literals will be implicitly converted to this type. Pop it when finishing the current level of BinaryOperator.
  // TODO: find a better way to deal with implicit type casting if it's not able to cope with compelx rules
  std::stack<const nlohmann::json *> current_BinOp_type;
  std::string current_functionName;

  //! Be careful of using 'current_contractName'. This might lead to trouble in inheritance.
  //! If you are not sure, use 'get_current_contract_name' instead.
  std::string current_contractName;
  std::string current_fileName;

  // Auxiliary data structures:
  // Mapping from the node 'id' to the exported symbol (i.e. contract, error, ....)
  std::unordered_map<int, std::string> exportedSymbolsList;
  // Inheritance Order Record <contract_name, Contract_id>
  std::unordered_map<std::string, std::vector<int>> linearizedBaseList;
  // Store the ast_node["id"] of contract/struct/function/...
  std::unordered_map<int, std::string> scope_map;

  static constexpr const char *mode = "C++";

  // The prefix for the id of each class
  std::string prefix = "tag-";

  // json nodes that always empty
  // used as the return value for find_constructor_ref when
  // dealing with the implicit constructor call
  // this is to avoid reference to stack memory associated with local variable returned
  const nlohmann::json empty_json;

  // this block stores the map_init function calls and
  // will merge to the constructor later on
  code_blockt map_init_block;

  // --function
  std::string tgt_func;
  // --contract
  std::string tgt_cnt;

private:
  bool get_elementary_type_name_uint(
    SolidityGrammar::ElementaryTypeNameT &type,
    typet &out);
  bool get_elementary_type_name_int(
    SolidityGrammar::ElementaryTypeNameT &type,
    typet &out);
  bool get_elementary_type_name_bytesn(
    SolidityGrammar::ElementaryTypeNameT &type,
    typet &out);
};

#endif /* SOLIDITY_FRONTEND_SOLIDITY_CONVERT_H_ */
