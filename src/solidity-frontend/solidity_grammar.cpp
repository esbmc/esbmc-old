#include <fmt/core.h>
#include <solidity-frontend/solidity_grammar.h>
#include <set>
#include <util/message.h>

#define ENUM_TO_STR(s)                                                         \
  case s:                                                                      \
  {                                                                            \
    return #s;                                                                 \
  }

namespace SolidityGrammar
{
const std::unordered_map<std::string, ElementaryTypeNameT>
  uint_string_to_type_map = {
    {"uint8", UINT8},     {"uint16", UINT16},   {"uint24", UINT24},
    {"uint32", UINT32},   {"uint40", UINT40},   {"uint48", UINT48},
    {"uint56", UINT56},   {"uint64", UINT64},   {"uint72", UINT72},
    {"uint80", UINT80},   {"uint88", UINT88},   {"uint96", UINT96},
    {"uint104", UINT104}, {"uint112", UINT112}, {"uint120", UINT120},
    {"uint128", UINT128}, {"uint136", UINT136}, {"uint144", UINT144},
    {"uint152", UINT152}, {"uint160", UINT160}, {"uint168", UINT168},
    {"uint176", UINT176}, {"uint184", UINT184}, {"uint192", UINT192},
    {"uint200", UINT200}, {"uint208", UINT208}, {"uint216", UINT216},
    {"uint224", UINT224}, {"uint232", UINT232}, {"uint240", UINT240},
    {"uint248", UINT248}, {"uint256", UINT256},
};

const std::unordered_map<ElementaryTypeNameT, unsigned int> uint_size_map = {
  {UINT8, 8},     {UINT16, 16},   {UINT24, 24},   {UINT32, 32},
  {UINT40, 40},   {UINT48, 48},   {UINT56, 56},   {UINT64, 64},
  {UINT72, 72},   {UINT80, 80},   {UINT88, 88},   {UINT96, 96},
  {UINT104, 104}, {UINT112, 112}, {UINT120, 120}, {UINT128, 128},
  {UINT136, 136}, {UINT144, 144}, {UINT152, 152}, {UINT160, 160},
  {UINT168, 168}, {UINT176, 176}, {UINT184, 184}, {UINT192, 192},
  {UINT200, 200}, {UINT208, 208}, {UINT216, 216}, {UINT224, 224},
  {UINT232, 232}, {UINT240, 240}, {UINT248, 248}, {UINT256, 256},
};
const std::map<std::string, ElementaryTypeNameT> int_string_to_type_map = {
  {"int8", INT8},     {"int16", INT16},   {"int24", INT24},
  {"int32", INT32},   {"int40", INT40},   {"int48", INT48},
  {"int56", INT56},   {"int64", INT64},   {"int72", INT72},
  {"int80", INT80},   {"int88", INT88},   {"int96", INT96},
  {"int104", INT104}, {"int112", INT112}, {"int120", INT120},
  {"int128", INT128}, {"int136", INT136}, {"int144", INT144},
  {"int152", INT152}, {"int160", INT160}, {"int168", INT168},
  {"int176", INT176}, {"int184", INT184}, {"int192", INT192},
  {"int200", INT200}, {"int208", INT208}, {"int216", INT216},
  {"int224", INT224}, {"int232", INT232}, {"int240", INT240},
  {"int248", INT248}, {"int256", INT256},
};
const std::map<ElementaryTypeNameT, unsigned int> int_size_map = {
  {INT8, 8},     {INT16, 16},   {INT24, 24},   {INT32, 32},   {INT40, 40},
  {INT48, 48},   {INT56, 56},   {INT64, 64},   {INT72, 72},   {INT80, 80},
  {INT88, 88},   {INT96, 96},   {INT104, 104}, {INT112, 112}, {INT120, 120},
  {INT128, 128}, {INT136, 136}, {INT144, 144}, {INT152, 152}, {INT160, 160},
  {INT168, 168}, {INT176, 176}, {INT184, 184}, {INT192, 192}, {INT200, 200},
  {INT208, 208}, {INT216, 216}, {INT224, 224}, {INT232, 232}, {INT240, 240},
  {INT248, 248}, {INT256, 256},
};
const std::unordered_map<std::string, ElementaryTypeNameT> bytesn_to_type_map =
  {
    {"byte", BYTES1},     {"bytes1", BYTES1},   {"bytes2", BYTES2},
    {"bytes3", BYTES3},   {"bytes4", BYTES4},   {"bytes5", BYTES5},
    {"bytes6", BYTES6},   {"bytes7", BYTES7},   {"bytes8", BYTES8},
    {"bytes9", BYTES9},   {"bytes10", BYTES10}, {"bytes11", BYTES11},
    {"bytes12", BYTES12}, {"bytes13", BYTES13}, {"bytes14", BYTES14},
    {"bytes15", BYTES15}, {"bytes16", BYTES16}, {"bytes17", BYTES17},
    {"bytes18", BYTES18}, {"bytes19", BYTES19}, {"bytes20", BYTES20},
    {"bytes21", BYTES21}, {"bytes22", BYTES22}, {"bytes23", BYTES23},
    {"bytes24", BYTES24}, {"bytes25", BYTES25}, {"bytes26", BYTES26},
    {"bytes27", BYTES27}, {"bytes28", BYTES28}, {"bytes29", BYTES29},
    {"bytes30", BYTES30}, {"bytes31", BYTES31}, {"bytes32", BYTES32},
};
const std::map<ElementaryTypeNameT, unsigned int> bytesn_size_map = {
  {BYTES1, 1},   {BYTES2, 2},   {BYTES3, 3},   {BYTES4, 4},   {BYTES5, 5},
  {BYTES6, 6},   {BYTES7, 7},   {BYTES8, 8},   {BYTES9, 9},   {BYTES10, 10},
  {BYTES11, 11}, {BYTES12, 12}, {BYTES13, 13}, {BYTES14, 14}, {BYTES15, 15},
  {BYTES16, 16}, {BYTES17, 17}, {BYTES18, 18}, {BYTES19, 19}, {BYTES20, 20},
  {BYTES21, 21}, {BYTES22, 22}, {BYTES23, 23}, {BYTES24, 24}, {BYTES25, 25},
  {BYTES26, 26}, {BYTES27, 27}, {BYTES28, 28}, {BYTES29, 29}, {BYTES30, 30},
  {BYTES31, 31}, {BYTES32, 32},
};

// rule contract-body-element
ContractBodyElementT get_contract_body_element_t(const nlohmann::json &element)
{
  if (element["nodeType"] == "VariableDeclaration")
  {
    return VarDecl;
  }
  else if (
    element["nodeType"] == "FunctionDefinition" &&
    (element["kind"] == "function" || element["kind"] == "constructor"))
  {
    return FunctionDef;
  }
  else if (element["nodeType"] == "StructDefinition")
  {
    return StructDef;
  }
  else if (element["nodeType"] == "EnumDefinition")
  {
    return EnumDef;
  }
  else if (element["nodeType"] == "ErrorDefinition")
  {
    return ErrorDef;
  }
  else if (element["nodeType"] == "EventDefinition")
  {
    return EventDef;
  }
  else
  {
    log_error(
      "Got contract-body-element nodeType={}. Unsupported "
      "contract-body-element type",
      element["nodeType"].get<std::string>());
    abort();
  }
  return ContractBodyElementTError;
}

const char *contract_body_element_to_str(ContractBodyElementT type)
{
  switch (type)
  {
    ENUM_TO_STR(VarDecl)
    ENUM_TO_STR(FunctionDef)
    ENUM_TO_STR(StructDef)
    ENUM_TO_STR(EnumDef)
    ENUM_TO_STR(ErrorDef)
    ENUM_TO_STR(EventDef)
    ENUM_TO_STR(ContractBodyElementTError)
  default:
  {
    assert(!"Unknown contract-body-element type");
    return "UNKNOWN";
  }
  }
}

// rule type-name
TypeNameT get_type_name_t(const nlohmann::json &type_name)
{
  // Solidity AST node has duplicate descrptions: ["typeName"]["typeDescriptions"] and ["typeDescriptions"]
  //! Order matters

  if (type_name.contains("typeString"))
  {
    // for AST node that contains ["typeName"]["typeDescriptions"]
    const std::string typeString = type_name["typeString"].get<std::string>();
    const std::string typeIdentifier =
      type_name["typeIdentifier"].get<std::string>();

    // we must first handle tuple
    // otherwise we might parse tuple(literal_string, literal_string)
    // as ElementaryTypeName
    if (typeString.compare(0, 6, "tuple(") == 0)
    {
      return TupleTypeName;
    }
    if (typeIdentifier.find("t_mapping$") != std::string::npos)
    {
      return MappingTypeName;
    }
    else if (typeIdentifier.find("t_array$") != std::string::npos)
    {
      // Solidity's array type description is like:
      //  "typeIdentifier": "t_array$_t_uint8_$2_memory_ptr",
      //  "typeString": "uint8[2] memory"

      // The Arrays in Solidity can be classified into the following two types based on size –
      //   Fixed Size Array
      //   Dynamic Array
      // Furthermore, the solidity array can also be categorized based on where they are stored as –
      //   Storage Array
      //   Memory Array

      // Multi-Dimensional Arrays
      if (typeIdentifier.find("t_array$_t_array$") != std::string::npos)
      {
        log_error("Multi-Dimensional Arrays are not supported.");
        abort();
      }

      if (typeIdentifier.find("$dyn") != std::string::npos)
        return DynArrayTypeName;

      return ArrayTypeName;
    }
    else if (
      uint_string_to_type_map.count(typeString) ||
      int_string_to_type_map.count(typeString) || typeString == "bool" ||
      typeString == "string" || typeString.find("literal_string") == 0 ||
      typeString == "string storage ref" || typeString == "string memory" ||
      typeString == "address payable" || typeString == "address" ||
      typeString.compare(0, 5, "bytes") == 0)
    {
      // For state var declaration,
      return ElementaryTypeName;
    }
    else if (typeIdentifier.find("t_enum$") != std::string::npos)
    {
      return EnumTypeName;
    }
    else if (typeIdentifier.find("t_contract$") != std::string::npos)
    {
      return ContractTypeName;
    }
    else if (typeIdentifier.find("t_struct$") != std::string::npos)
    {
      return StructTypeName;
    }
    else if (typeString.find("type(") != std::string::npos)
    {
      // For type conversion
      return TypeConversionName;
    }
    else if (typeString.find("int_const") != std::string::npos)
    {
      // For Literal, their typeString is like "int_const 100".
      return ElementaryTypeName;
    }
    else if (
      typeString.find("function") != std::string::npos &&
      typeString.find("contract ") == std::string::npos)
    {
      // FunctionToPointer decay in CallExpr when making a function call
      return Pointer;
    }
    else if (typeIdentifier.find("ArrayToPtr") != std::string::npos)
    {
      // ArrayToPointer decay in DeclRefExpr when dereferencing an array, e.g. a[0]
      return PointerArrayToPtr;
    }
    // for Special Variables and Functions
    else if (typeIdentifier.compare(0, 7, "t_magic") == 0)
    {
      return BuiltinTypeName;
    }
    else
    {
      log_error(
        "Got type-name typeString={}. Unsupported type-name type",
        type_name["typeString"].get<std::string>());
      abort();
    }
  }
  else
  {
    // for AST node that does not contain ["typeDescriptions"] only
    // function returnParameters
    if (type_name["nodeType"] == "ParameterList")
    {
      return ParameterList;
    }
    else
    {
      log_error(
        "Got type-name nodeType={}. Unsupported type-name type",
        type_name["nodeType"].get<std::string>());
      abort();
    }
  }

  return TypeNameTError; // to make some old compiler happy
}

const char *type_name_to_str(TypeNameT type)
{
  switch (type)
  {
    ENUM_TO_STR(ElementaryTypeName)
    ENUM_TO_STR(ParameterList)
    ENUM_TO_STR(Pointer)
    ENUM_TO_STR(PointerArrayToPtr)
    ENUM_TO_STR(ArrayTypeName)
    ENUM_TO_STR(DynArrayTypeName)
    ENUM_TO_STR(ContractTypeName)
    ENUM_TO_STR(TypeConversionName)
    ENUM_TO_STR(EnumTypeName)
    ENUM_TO_STR(StructTypeName)
    ENUM_TO_STR(TupleTypeName)
    ENUM_TO_STR(MappingTypeName)
    ENUM_TO_STR(BuiltinTypeName)
    ENUM_TO_STR(TypeNameTError)
  default:
  {
    assert(!"Unknown type-name type");
    return "UNKNOWN";
  }
  }
}

// rule elementary-type-name
// return the type of expression
ElementaryTypeNameT get_elementary_type_name_t(const nlohmann::json &type_name)
{
  std::string typeString = type_name["typeString"].get<std::string>();
  // rule unsigned-integer-type

  if (uint_string_to_type_map.count(typeString))
  {
    return uint_string_to_type_map.at(typeString);
  }
  if (int_string_to_type_map.count(typeString))
  {
    return int_string_to_type_map.at(typeString);
  }
  if (typeString == "bool")
  {
    return BOOL;
  }
  if (typeString.find("int_const") != std::string::npos)
  {
    /**
     * For Literal, their typeString is like "int_const 100".
     * There is no additional type info (bitsize, signed/unsigned),
     * This means it will require additional type info from the parent
     * expr to create an internal type.
     */
    return INT_LITERAL;
  }
  if (typeString.find("literal_string") == 0)
  {
    return STRING_LITERAL;
  }
  if (
    typeString == "string" || typeString == "string storage ref" ||
    typeString == "string memory")
  {
    return STRING;
  }
  if (typeString == "address")
  {
    return ADDRESS;
  }
  if (typeString == "address payable")
  {
    return ADDRESS_PAYABLE;
  }
  if (bytesn_to_type_map.count(typeString))
  {
    // fixed-size arrays bytesN, where N is a number between 1 and 32
    return bytesn_to_type_map.at(typeString);
  }
  if (typeString.find("bytes") != std::string::npos)
  {
    // dynamic bytes array
    // e.g.
    //    bytes
    //    bytes storage ref
    //    bytes memory
    return BYTES;
  }
  log_error(
    "Got elementary-type-name typeString={}. Unsupported "
    "elementary-type-name type",
    type_name["typeString"].get<std::string>());
  abort();
}

const char *elementary_type_name_to_str(ElementaryTypeNameT type)
{
  switch (type)
  {
    ENUM_TO_STR(UINT8)
    ENUM_TO_STR(UINT16)
    ENUM_TO_STR(UINT24)
    ENUM_TO_STR(UINT32)
    ENUM_TO_STR(UINT40)
    ENUM_TO_STR(UINT48)
    ENUM_TO_STR(UINT56)
    ENUM_TO_STR(UINT64)
    ENUM_TO_STR(UINT72)
    ENUM_TO_STR(UINT80)
    ENUM_TO_STR(UINT88)
    ENUM_TO_STR(UINT96)
    ENUM_TO_STR(UINT104)
    ENUM_TO_STR(UINT112)
    ENUM_TO_STR(UINT120)
    ENUM_TO_STR(UINT128)
    ENUM_TO_STR(UINT136)
    ENUM_TO_STR(UINT144)
    ENUM_TO_STR(UINT152)
    ENUM_TO_STR(UINT160)
    ENUM_TO_STR(UINT168)
    ENUM_TO_STR(UINT176)
    ENUM_TO_STR(UINT184)
    ENUM_TO_STR(UINT192)
    ENUM_TO_STR(UINT200)
    ENUM_TO_STR(UINT208)
    ENUM_TO_STR(UINT216)
    ENUM_TO_STR(UINT224)
    ENUM_TO_STR(UINT232)
    ENUM_TO_STR(UINT240)
    ENUM_TO_STR(UINT248)
    ENUM_TO_STR(UINT256)
    ENUM_TO_STR(INT_LITERAL)
    ENUM_TO_STR(INT8)
    ENUM_TO_STR(INT16)
    ENUM_TO_STR(INT24)
    ENUM_TO_STR(INT32)
    ENUM_TO_STR(INT40)
    ENUM_TO_STR(INT48)
    ENUM_TO_STR(INT56)
    ENUM_TO_STR(INT64)
    ENUM_TO_STR(INT72)
    ENUM_TO_STR(INT80)
    ENUM_TO_STR(INT88)
    ENUM_TO_STR(INT96)
    ENUM_TO_STR(INT104)
    ENUM_TO_STR(INT112)
    ENUM_TO_STR(INT120)
    ENUM_TO_STR(INT128)
    ENUM_TO_STR(INT136)
    ENUM_TO_STR(INT144)
    ENUM_TO_STR(INT152)
    ENUM_TO_STR(INT160)
    ENUM_TO_STR(INT168)
    ENUM_TO_STR(INT176)
    ENUM_TO_STR(INT184)
    ENUM_TO_STR(INT192)
    ENUM_TO_STR(INT200)
    ENUM_TO_STR(INT208)
    ENUM_TO_STR(INT216)
    ENUM_TO_STR(INT224)
    ENUM_TO_STR(INT232)
    ENUM_TO_STR(INT240)
    ENUM_TO_STR(INT248)
    ENUM_TO_STR(INT256)
    ENUM_TO_STR(BOOL)
    ENUM_TO_STR(STRING_LITERAL)
    ENUM_TO_STR(STRING)
    ENUM_TO_STR(ADDRESS)
    ENUM_TO_STR(ADDRESS_PAYABLE)
    ENUM_TO_STR(BYTES)
    ENUM_TO_STR(BYTES1)
    ENUM_TO_STR(BYTES2)
    ENUM_TO_STR(BYTES3)
    ENUM_TO_STR(BYTES4)
    ENUM_TO_STR(BYTES5)
    ENUM_TO_STR(BYTES6)
    ENUM_TO_STR(BYTES7)
    ENUM_TO_STR(BYTES8)
    ENUM_TO_STR(BYTES9)
    ENUM_TO_STR(BYTES10)
    ENUM_TO_STR(BYTES11)
    ENUM_TO_STR(BYTES12)
    ENUM_TO_STR(BYTES13)
    ENUM_TO_STR(BYTES14)
    ENUM_TO_STR(BYTES15)
    ENUM_TO_STR(BYTES16)
    ENUM_TO_STR(BYTES17)
    ENUM_TO_STR(BYTES18)
    ENUM_TO_STR(BYTES19)
    ENUM_TO_STR(BYTES20)
    ENUM_TO_STR(BYTES21)
    ENUM_TO_STR(BYTES22)
    ENUM_TO_STR(BYTES23)
    ENUM_TO_STR(BYTES24)
    ENUM_TO_STR(BYTES25)
    ENUM_TO_STR(BYTES26)
    ENUM_TO_STR(BYTES27)
    ENUM_TO_STR(BYTES28)
    ENUM_TO_STR(BYTES29)
    ENUM_TO_STR(BYTES30)
    ENUM_TO_STR(BYTES31)
    ENUM_TO_STR(BYTES32)
    ENUM_TO_STR(ElementaryTypeNameTError)
  default:
  {
    assert(!"Unknown elementary-type-name type");
    return "UNKNOWN";
  }
  }
}

unsigned int uint_type_name_to_size(ElementaryTypeNameT type)
{
  return uint_size_map.at(type);
}

unsigned int int_type_name_to_size(ElementaryTypeNameT type)
{
  return int_size_map.at(type);
}

unsigned int bytesn_type_name_to_size(ElementaryTypeNameT type)
{
  return bytesn_size_map.at(type);
}

// rule parameter-list
ParameterListT get_parameter_list_t(const nlohmann::json &type_name)
{
  if (type_name["parameters"].size() == 0)
  {
    return EMPTY;
  }
  else if (type_name["parameters"].size() == 1)
  {
    return ONE_PARAM;
  }
  else if (type_name["parameters"].size() > 1)
  {
    return MORE_THAN_ONE_PARAM;
  }

  return ParameterListTError; // to make some old gcc compilers happy
}

const char *parameter_list_to_str(ParameterListT type)
{
  switch (type)
  {
    ENUM_TO_STR(EMPTY)
    ENUM_TO_STR(ONE_PARAM)
    ENUM_TO_STR(MORE_THAN_ONE_PARAM)
    ENUM_TO_STR(ParameterListTError)
  default:
  {
    assert(!"Unknown parameter-list type");
    return "UNKNOWN";
  }
  }
}

// rule block
BlockT get_block_t(const nlohmann::json &block)
{
  if (block["nodeType"] == "Block" && block.contains("statements"))
  {
    return Statement;
  }
  else if (block["nodeType"] == "ForStatement")
  {
    return BlockForStatement;
  }
  else if (block["nodeType"] == "IfStatement")
  {
    return BlockIfStatement;
  }
  else if (block["nodeType"] == "WhileStatement")
  {
    return BlockWhileStatement;
  }
  else if (block["nodeType"] == "ExpressionStatement")
  {
    return BlockExpressionStatement;
  }

  // fall-through
  log_error(
    "Got block nodeType={}. Unsupported block type",
    block["nodeType"].get<std::string>());
  return BlockTError;
}

const char *block_to_str(BlockT type)
{
  switch (type)
  {
    ENUM_TO_STR(Statement)
    ENUM_TO_STR(BlockForStatement)
    ENUM_TO_STR(BlockIfStatement)
    ENUM_TO_STR(BlockWhileStatement)
    ENUM_TO_STR(BlockExpressionStatement)
    ENUM_TO_STR(UncheckedBlock)
    ENUM_TO_STR(BlockTError)
  default:
  {
    assert(!"Unknown block type");
    return "UNKNOWN";
  }
  }
}

// rule statement
StatementT get_statement_t(const nlohmann::json &stmt)
{
  if (stmt["nodeType"] == "ExpressionStatement")
  {
    return ExpressionStatement;
  }
  else if (stmt["nodeType"] == "VariableDeclarationStatement")
  {
    return VariableDeclStatement;
  }
  else if (stmt["nodeType"] == "Return")
  {
    return ReturnStatement;
  }
  else if (stmt["nodeType"] == "ForStatement")
  {
    return ForStatement;
  }
  else if (stmt["nodeType"] == "Block")
  {
    return Block;
  }
  else if (stmt["nodeType"] == "IfStatement")
  {
    return IfStatement;
  }
  else if (stmt["nodeType"] == "WhileStatement")
  {
    return WhileStatement;
  }
  else if (stmt["nodeType"] == "Continue")
  {
    return ContinueStatement;
  }
  else if (stmt["nodeType"] == "Break")
  {
    return BreakStatement;
  }
  else if (stmt["nodeType"] == "RevertStatement")
  {
    return RevertStatement;
  }
  else if (stmt["nodeType"] == "EmitStatement")
  {
    return EmitStatement;
  }
  else
  {
    log_error(
      "Got statement nodeType={}. Unsupported statement type",
      stmt["nodeType"].get<std::string>());
    abort();
  }
  return StatementTError;
}

const char *statement_to_str(StatementT type)
{
  switch (type)
  {
    ENUM_TO_STR(Block)
    ENUM_TO_STR(ExpressionStatement)
    ENUM_TO_STR(VariableDeclStatement)
    ENUM_TO_STR(ReturnStatement)
    ENUM_TO_STR(ForStatement)
    ENUM_TO_STR(IfStatement)
    ENUM_TO_STR(WhileStatement)
    ENUM_TO_STR(StatementTError)
    ENUM_TO_STR(ContinueStatement)
    ENUM_TO_STR(BreakStatement)
    ENUM_TO_STR(RevertStatement)
    ENUM_TO_STR(EmitStatement)
  default:
  {
    assert(!"Unknown statement type");
    return "UNKNOWN";
  }
  }
}

// rule expression
ExpressionT get_expression_t(const nlohmann::json &expr)
{
  if (expr.is_null())
  {
    return NullExpr;
  }
  if (expr["nodeType"] == "Assignment" || expr["nodeType"] == "BinaryOperation")
  {
    return BinaryOperatorClass;
  }
  else if (expr["nodeType"] == "UnaryOperation")
  {
    return UnaryOperatorClass;
  }
  else if (expr["nodeType"] == "Conditional")
  {
    return ConditionalOperatorClass;
  }
  else if (
    expr["nodeType"] == "Identifier" && expr.contains("referencedDeclaration"))
  {
    return DeclRefExprClass;
  }
  else if (expr["nodeType"] == "Literal")
  {
    return Literal;
  }
  else if (expr["nodeType"] == "TupleExpression")
  {
    return Tuple;
  }
  else if (
    expr["nodeType"] == "Mapping" ||
    (expr["nodeType"] == "VariableDeclaration" &&
     expr["typeName"]["nodeType"] == "Mapping"))
  {
    return Mapping;
  }
  else if (expr["nodeType"] == "FunctionCall")
  {
    if (expr["expression"]["nodeType"] == "NewExpression")
      return NewExpression;
    if (
      expr["expression"]["nodeType"] == "ElementaryTypeNameExpression" &&
      expr["kind"] == "typeConversion")
      return ElementaryTypeNameExpression;
    return CallExprClass;
  }
  else if (expr["nodeType"] == "MemberAccess")
  {
    assert(expr.contains("expression"));
    SolidityGrammar::TypeNameT type_name =
      get_type_name_t(expr["expression"]["typeDescriptions"]);
    if (type_name == SolidityGrammar::TypeNameT::StructTypeName)
      return StructMemberCall;
    else if (type_name == SolidityGrammar::TypeNameT::EnumTypeName)
      return EnumMemberCall;
    else if (type_name == SolidityGrammar::TypeNameT::ContractTypeName)
      return ContractMemberCall;
    else
      //TODO Assume it's a builtin member
      // due to that the BuiltinTypeName cannot cover all the builtin member
      // e.g. string.concat ==> TypeConversionName
      return BuiltinMemberCall;
  }
  else if (expr["nodeType"] == "ImplicitCastExprClass")
  {
    return ImplicitCastExprClass;
  }
  else if (expr["nodeType"] == "IndexAccess")
  {
    return IndexAccess;
  }

  // fall-through
  log_error(
    "Got expression nodeType={}. Unsupported expression type",
    expr["nodeType"].get<std::string>());
  return ExpressionTError;
}

ExpressionT get_unary_expr_operator_t(const nlohmann::json &expr, bool uo_pre)
{
  if (expr["operator"] == "--")
  {
    if (uo_pre)
      return UO_PreDec;
    else
      return UO_PostDec;
  }
  else if (expr["operator"] == "++")
  {
    if (uo_pre)
      return UO_PreInc;
    else
      return UO_PostInc;
  }
  else if (expr["operator"] == "-")
  {
    return UO_Minus;
  }
  else if (expr["operator"] == "~")
  {
    return UO_Not;
  }
  else if (expr["operator"] == "!")
  {
    return UO_LNot;
  }
  else
  {
    log_error(
      "Got expression operator={}. Unsupported expression operator",
      expr["operator"].get<std::string>());

    abort();
  }
}

ExpressionT get_expr_operator_t(const nlohmann::json &expr)
{
  if (expr["operator"] == "=")
  {
    return BO_Assign;
  }
  else if (expr["operator"] == "+")
  {
    return BO_Add;
  }
  else if (expr["operator"] == "-")
  {
    return BO_Sub;
  }
  else if (expr["operator"] == "*")
  {
    return BO_Mul;
  }
  else if (expr["operator"] == "/")
  {
    return BO_Div;
  }
  else if (expr["operator"] == "%")
  {
    return BO_Rem;
  }
  else if (expr["operator"] == "<<")
  {
    return BO_Shl;
  }
  else if (expr["operator"] == ">>")
  {
    return BO_Shr;
  }
  else if (expr["operator"] == "&")
  {
    return BO_And;
  }
  else if (expr["operator"] == "^")
  {
    return BO_Xor;
  }
  else if (expr["operator"] == "|")
  {
    return BO_Or;
  }
  else if (expr["operator"] == ">")
  {
    return BO_GT;
  }
  else if (expr["operator"] == "<")
  {
    return BO_LT;
  }
  else if (expr["operator"] == ">=")
  {
    return BO_GE;
  }
  else if (expr["operator"] == "<=")
  {
    return BO_LE;
  }
  else if (expr["operator"] == "!=")
  {
    return BO_NE;
  }
  else if (expr["operator"] == "==")
  {
    return BO_EQ;
  }
  else if (expr["operator"] == "&&")
  {
    return BO_LAnd;
  }
  else if (expr["operator"] == "||")
  {
    return BO_LOr;
  }
  else if (expr["operator"] == "+=")
  {
    return BO_AddAssign;
  }
  else if (expr["operator"] == "-=")
  {
    return BO_SubAssign;
  }
  else if (expr["operator"] == "*=")
  {
    return BO_MulAssign;
  }
  else if (expr["operator"] == "/=")
  {
    return BO_DivAssign;
  }
  else if (expr["operator"] == "%=")
  {
    return BO_RemAssign;
  }
  else if (expr["operator"] == "<<=")
  {
    return BO_ShlAssign;
  }
  else if (expr["operator"] == ">>=")
  {
    return BO_ShrAssign;
  }
  else if (expr["operator"] == "&=")
  {
    return BO_AndAssign;
  }
  else if (expr["operator"] == "^=")
  {
    return BO_XorAssign;
  }
  else if (expr["operator"] == "|=")
  {
    return BO_OrAssign;
  }
  else if (expr["operator"] == "**")
  {
    return BO_Pow;
  }
  else
  {
    log_error(
      "Got expression operator={}. Unsupported expression operator",
      expr["operator"].get<std::string>());
    abort();
  }

  return ExpressionTError; // make some old compilers happy
}

const char *expression_to_str(ExpressionT type)
{
  switch (type)
  {
    ENUM_TO_STR(BinaryOperatorClass)
    ENUM_TO_STR(BO_Assign)
    ENUM_TO_STR(BO_Add)
    ENUM_TO_STR(BO_Sub)
    ENUM_TO_STR(BO_Mul)
    ENUM_TO_STR(BO_Div)
    ENUM_TO_STR(BO_Rem)

    ENUM_TO_STR(BO_Shl)
    ENUM_TO_STR(BO_Shr)
    ENUM_TO_STR(BO_And)
    ENUM_TO_STR(BO_Xor)
    ENUM_TO_STR(BO_Or)

    ENUM_TO_STR(BO_GT)
    ENUM_TO_STR(BO_LT)
    ENUM_TO_STR(BO_GE)
    ENUM_TO_STR(BO_LE)
    ENUM_TO_STR(BO_NE)
    ENUM_TO_STR(BO_EQ)
    ENUM_TO_STR(BO_LAnd)
    ENUM_TO_STR(BO_LOr)

    ENUM_TO_STR(BO_AddAssign)
    ENUM_TO_STR(BO_SubAssign)
    ENUM_TO_STR(BO_MulAssign)
    ENUM_TO_STR(BO_DivAssign)
    ENUM_TO_STR(BO_RemAssign)
    ENUM_TO_STR(BO_ShlAssign)
    ENUM_TO_STR(BO_ShrAssign)
    ENUM_TO_STR(BO_AndAssign)
    ENUM_TO_STR(BO_XorAssign)
    ENUM_TO_STR(BO_OrAssign)
    ENUM_TO_STR(BO_Pow)

    ENUM_TO_STR(UnaryOperatorClass)
    ENUM_TO_STR(UO_PreDec)
    ENUM_TO_STR(UO_PreInc)
    ENUM_TO_STR(UO_PostDec)
    ENUM_TO_STR(UO_PostInc)
    ENUM_TO_STR(UO_Minus)
    ENUM_TO_STR(UO_Not)
    ENUM_TO_STR(UO_LNot)

    ENUM_TO_STR(ConditionalOperatorClass)

    ENUM_TO_STR(DeclRefExprClass)
    ENUM_TO_STR(Literal)
    ENUM_TO_STR(Tuple)
    ENUM_TO_STR(Mapping)
    ENUM_TO_STR(CallExprClass)
    ENUM_TO_STR(ImplicitCastExprClass)
    ENUM_TO_STR(IndexAccess)
    ENUM_TO_STR(NewExpression)
    ENUM_TO_STR(ContractMemberCall)
    ENUM_TO_STR(StructMemberCall)
    ENUM_TO_STR(EnumMemberCall)
    ENUM_TO_STR(BuiltinMemberCall)
    ENUM_TO_STR(ElementaryTypeNameExpression)
    ENUM_TO_STR(NullExpr)
    ENUM_TO_STR(ExpressionTError)
  default:
  {
    assert(!"Unknown expression type");
    return "UNKNOWN";
  }
  }
}

// rule variable-declaration-statement
VarDeclStmtT get_var_decl_stmt_t(const nlohmann::json &stmt)
{
  if (
    stmt["nodeType"] == "VariableDeclaration" && stmt["stateVariable"] == false)
  {
    return VariableDecl;
  }
  else
  {
    log_error(
      "Got expression nodeType={}. Unsupported "
      "variable-declaration-statement operator",
      stmt["nodeType"].get<std::string>());
    abort();
  }
  return VarDeclStmtTError; // make some old compilers happy
}

const char *var_decl_statement_to_str(VarDeclStmtT type)
{
  switch (type)
  {
    ENUM_TO_STR(VariableDecl)
    ENUM_TO_STR(VariableDeclTuple)
    ENUM_TO_STR(VarDeclStmtTError)
  default:
  {
    assert(!"Unknown variable-declaration-statement type");
    return "UNKNOWN";
  }
  }
}

// auxiliary type to convert function call
FunctionDeclRefT get_func_decl_ref_t(const nlohmann::json &decl)
{
  assert(
    decl["nodeType"] == "FunctionDefinition" ||
    decl["nodeType"] == "EventDefinition");
  if (
    decl["parameters"]["parameters"].size() == 0 ||
    decl["kind"] == "constructor")
  {
    return FunctionNoProto;
  }
  else
  {
    return FunctionProto;
  }
  return FunctionDeclRefTError; // to make some old compilers happy
}

const char *func_decl_ref_to_str(FunctionDeclRefT type)
{
  switch (type)
  {
    ENUM_TO_STR(FunctionProto)
    ENUM_TO_STR(FunctionNoProto)
    ENUM_TO_STR(FunctionDeclRefTError)
  default:
  {
    assert(!"Unknown auxiliary type to convert function call");
    return "UNKNOWN";
  }
  }
}

// auxiliary type for implicit casting
ImplicitCastTypeT get_implicit_cast_type_t(std::string cast)
{
  if (cast == "LValueToRValue")
  {
    return LValueToRValue;
  }
  else if (cast == "FunctionToPointerDecay")
  {
    return FunctionToPointerDecay;
  }
  else if (cast == "ArrayToPointerDecay")
  {
    return ArrayToPointerDecay;
  }
  else
  {
    log_error("Got implicit cast type={}. Unsupported case type", cast.c_str());
    abort();
  }

  return ImplicitCastTypeTError; // to make some old compilers happy
}

const char *implicit_cast_type_to_str(ImplicitCastTypeT type)
{
  switch (type)
  {
    ENUM_TO_STR(LValueToRValue)
    ENUM_TO_STR(FunctionToPointerDecay)
    ENUM_TO_STR(ArrayToPointerDecay)
    ENUM_TO_STR(ImplicitCastTypeTError)
  default:
  {
    assert(!"Unknown auxiliary type for implicit casting");
    return "UNKNOWN";
  }
  }
}

VisibilityT get_access_t(const nlohmann::json &ast_node)
{
  if (!ast_node.contains("visibility"))
    return UnknownT;
  std::string access = ast_node["visibility"].get<std::string>();
  if (access == "public")
  {
    return PublicT;
  }
  else if (access == "private")
  {
    return PrivateT;
  }
  else if (access == "external")
  {
    return ExternalT;
  }
  else if (access == "internal")
  {
    return InternalT;
  }
  else
  {
    log_error("Unknown Visibility");
    abort();
  }
  return UnknownT;
}

}; // namespace SolidityGrammar
