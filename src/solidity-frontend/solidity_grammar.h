#ifndef SOLIDITY_GRAMMAR_H_
#define SOLIDITY_GRAMMAR_H_

#include <map>
#include <string>
#include <nlohmann/json.hpp>

// Anything auxiliary means it's not in Solidity grammar, but we need it to work with
// ESBMC's irept
namespace SolidityGrammar
{
// rule contract-body-element
enum ContractBodyElementT
{
  VarDecl = 0, // rule variable-declaration
  FunctionDef, // rule function-definition
  StructDef,   // rule struct-definition
  EnumDef,     // rule enum-definition
  ErrorDef,    // rule error-definition
  EventDef,    // rule event-definition
  ContractBodyElementTError
};
ContractBodyElementT get_contract_body_element_t(const nlohmann::json &element);
const char *contract_body_element_to_str(ContractBodyElementT type);

// rule type-name
enum TypeNameT
{
  // rule elementary-type-name
  ElementaryTypeName = 0,

  // rule parameter-list. Strictly, this should not be here. Just a workaround
  ParameterList,

  // auxiliary type for FunctionToPointer decay in CallExpr when making a function call
  Pointer, // TODO: Fix me. Rename it to PointerFuncToPtr

  // auxiliary type for ArrayToPointer when dereferencing array, e.g. a[0]
  PointerArrayToPtr,

  // static array type
  ArrayTypeName,

  // dynamic array type
  DynArrayTypeName,

  // contract type
  ContractTypeName,

  // Address type
  AddressTypeName,

  // typecast
  TypeConversionName,

  // enum
  EnumTypeName,

  // struct
  StructTypeName,

  // tuple
  TupleTypeName,

  // mapping
  MappingTypeName,

  // built-in member
  BuiltinTypeName,

  TypeNameTError
};
TypeNameT get_type_name_t(const nlohmann::json &type_name);
const char *type_name_to_str(TypeNameT type);

// rule elementary-type-name
enum ElementaryTypeNameT
{
  // rule unsigned-integer-type
  UINT8,
  UINT16,
  UINT24,
  UINT32,
  UINT40,
  UINT48,
  UINT56,
  UINT64,
  UINT72,
  UINT80,
  UINT88,
  UINT96,
  UINT104,
  UINT112,
  UINT120,
  UINT128,
  UINT136,
  UINT144,
  UINT152,
  UINT160,
  UINT168,
  UINT176,
  UINT184,
  UINT192,
  UINT200,
  UINT208,
  UINT216,
  UINT224,
  UINT232,
  UINT240,
  UINT248,
  UINT256,

  INT_LITERAL,
  // rule signed-integer-type
  INT8,
  INT16,
  INT24,
  INT32,
  INT40,
  INT48,
  INT56,
  INT64,
  INT72,
  INT80,
  INT88,
  INT96,
  INT104,
  INT112,
  INT120,
  INT128,
  INT136,
  INT144,
  INT152,
  INT160,
  INT168,
  INT176,
  INT184,
  INT192,
  INT200,
  INT208,
  INT216,
  INT224,
  INT232,
  INT240,
  INT248,
  INT256,

  // rule bool
  BOOL,

  // rule address
  ADDRESS,
  ADDRESS_PAYABLE,

  // rule string
  STRING,
  STRING_LITERAL,

  // rule bytes
  BYTES,
  BYTES1,
  BYTES2,
  BYTES3,
  BYTES4,
  BYTES5,
  BYTES6,
  BYTES7,
  BYTES8,
  BYTES9,
  BYTES10,
  BYTES11,
  BYTES12,
  BYTES13,
  BYTES14,
  BYTES15,
  BYTES16,
  BYTES17,
  BYTES18,
  BYTES19,
  BYTES20,
  BYTES21,
  BYTES22,
  BYTES23,
  BYTES24,
  BYTES25,
  BYTES26,
  BYTES27,
  BYTES28,
  BYTES29,
  BYTES30,
  BYTES31,
  BYTES32,

  // TODO: rule signed-integer-type
  // TODO: rule e
  // TODO: fixed-bytes
  // TODO: fixed
  // TODO: ufixed
  ElementaryTypeNameTError
};
ElementaryTypeNameT get_elementary_type_name_t(const nlohmann::json &type_name);
const char *elementary_type_name_to_str(ElementaryTypeNameT type);
unsigned int uint_type_name_to_size(ElementaryTypeNameT);

unsigned int uint_type_name_to_size(ElementaryTypeNameT);
unsigned int int_type_name_to_size(ElementaryTypeNameT);
unsigned int bytesn_type_name_to_size(ElementaryTypeNameT);

// rule parameter-list
enum ParameterListT
{
  EMPTY = 0, // In Solidity, "void" means an empty parameter list
  ONE_PARAM,
  MORE_THAN_ONE_PARAM,
  ParameterListTError
};
ParameterListT get_parameter_list_t(const nlohmann::json &type_name);
const char *parameter_list_to_str(ParameterListT type);

// rule block
enum BlockT
{
  Statement = 0,
  BlockForStatement,
  BlockIfStatement,
  BlockWhileStatement,
  BlockExpressionStatement,
  UncheckedBlock,
  BlockTError
};
BlockT get_block_t(const nlohmann::json &block);
const char *block_to_str(BlockT type);

// rule statement
enum StatementT
{
  Block = 0,             // rule block (mutual inclusion)
  ExpressionStatement,   // rule expression-statement
  VariableDeclStatement, // rule variable-declaration-statement
  ReturnStatement,       // rule return-statement
  ForStatement,          // rule for-statement
  IfStatement,           // rule if-statement
  WhileStatement,
  StatementTError,
  ContinueStatement, // rule continue
  BreakStatement,    // rule break
  RevertStatement,   // rule revert
  EmitStatement      // rule emit
};
StatementT get_statement_t(const nlohmann::json &stmt);
const char *statement_to_str(StatementT type);

// rule expression-statement
//  - Skipped since it just contains 1 type: "expression + ;"

// rule expression
// these are used to identify the type of the expression
enum ExpressionT
{
  // BinaryOperator
  BinaryOperatorClass =
    0, // This type covers all binary operators in Solidity, such as =, +, - .etc
  BO_Assign, // =
  BO_Add,    // +
  BO_Sub,    // -
  BO_Mul,    // *
  BO_Div,    // /
  BO_Rem,    // %

  BO_Shl, // <<
  BO_Shr, // >>
  BO_And, // &
  BO_Xor, // ^
  BO_Or,  // |

  BO_GT,   // >
  BO_LT,   // <
  BO_GE,   // >=
  BO_LE,   // <=
  BO_NE,   // !=
  BO_EQ,   // ==
  BO_LAnd, // &&
  BO_LOr,  // ||

  BO_AddAssign, // +=
  BO_SubAssign, // -=
  BO_MulAssign, // *=
  BO_DivAssign, // /=
  BO_RemAssign, // %=
  BO_ShlAssign, // <<=
  BO_ShrAssign, // >>=
  BO_AndAssign, // &=
  BO_XorAssign, // ^=
  BO_OrAssign,  // |=
  BO_Pow,       // **

  // UnaryOperator
  UnaryOperatorClass,
  UO_PreDec,  // --
  UO_PreInc,  // ++
  UO_PostDec, // --
  UO_PostInc, // ++
  UO_Minus,   // -
  UO_Not,     // ~
  UO_LNot,    // !

  //ternaryOperator
  ConditionalOperatorClass, // ?...:...

  // rule identifier
  DeclRefExprClass,

  // rule literal
  Literal,

  // rule Tuple
  Tuple,

  // rule Mapping
  Mapping,

  // FunctionCall
  CallExprClass,

  // FunctionCallOptions
  CallOptionsExprClass,

  // auxiliary type for implicit casting in Solidity, e.g. function return value
  // Solidity does NOT provide such information.
  ImplicitCastExprClass,

  // auxiliary type for array's "[]" operator
  // equivalent to clang::Stmt::ArraySubscriptExprClass
  // Solidity does NOT provide such rule
  IndexAccess,

  // Create a temporary object by keywords 'ew'
  // equivalent to clang::Stmt::CXXTemporaryObjectExprClass
  // i.e. Base x = new Base(args);
  NewExpression,

  // Call member functions
  // equivalent toclang::Stmt::CXXMemberCallExprClass
  // i.e. x.caller();
  ContractMemberCall,

  // Members of Address Types
  // see https://docs.soliditylang.org/en/v0.8.23/units-and-global-variables.html#members-of-address-types
  AddressMemberCall,

  // Type Converion
  TypeConversionExpression,

  // Struct Member Access
  StructMemberCall,

  // Enum Member Access
  EnumMemberCall,

  // Built-in Member Access
  BuiltinMemberCall,

  // Null Expression
  NullExpr,

  ExpressionTError
};
ExpressionT get_expression_t(const nlohmann::json &expr);
ExpressionT get_expr_operator_t(const nlohmann::json &expr);
ExpressionT
get_unary_expr_operator_t(const nlohmann::json &expr, bool uo_pre = true);
const char *expression_to_str(ExpressionT type);

// rule variable-declaration-statement
enum VarDeclStmtT
{
  VariableDecl,      // rule variable-declaration
  VariableDeclTuple, // rule variable-declaration-tuple
  VarDeclStmtTError
};
VarDeclStmtT get_var_decl_stmt_t(const nlohmann::json &stmt);
const char *var_decl_statement_to_str(VarDeclStmtT type);

// auxiliary type to convert function call
// No corresponding Solidity rules
enum FunctionDeclRefT
{
  FunctionProto = 0,
  FunctionNoProto,
  FunctionDeclRefTError
};
FunctionDeclRefT get_func_decl_ref_t(const nlohmann::json &decl);
const char *func_decl_ref_to_str(FunctionDeclRefT type);

// auxiliary type for implicit casting
enum ImplicitCastTypeT
{
  // for return value casting
  LValueToRValue = 0,

  // for ImplicitCastExpr<FunctionToPointerDecay> as in CallExpr when making a function call
  FunctionToPointerDecay,

  // for ImplicitCastExpr<ArrayToPointerDecay> as in IndexAccess
  ArrayToPointerDecay,

  ImplicitCastTypeTError
};
ImplicitCastTypeT get_implicit_cast_type_t(std::string cast);
const char *implicit_cast_type_to_str(ImplicitCastTypeT type);

// the function visibility
enum VisibilityT
{
  // any contract and account can call
  PublicT,

  // only inside the contract that defines the function
  PrivateT,

  // only other contracts and accounts can call
  ExternalT,

  // only inside contract that inherits an internal function
  InternalT,

  UnknownT
};
VisibilityT get_access_t(const nlohmann::json &ast_node);

}; // namespace SolidityGrammar

#endif /* SOLIDITY_GRAMMAR_H_ */
