#include <solidity-frontend/solidity_grammar.h>

namespace SolidityGrammar
{
  // rule contract-body-element
  ContractBodyElementT get_contract_body_element_t(const nlohmann::json &element)
  {
    if (element["nodeType"] == "VariableDeclaration" &&
        element["stateVariable"] == true)
    {
      return StateVarDecl;
    }
    else if (element["nodeType"] == "FunctionDefinition" &&
             element["kind"] == "function")
    {
      return FunctionDef;
    }
    else
    {
      printf("Got contract-body-element nodeType=%s\n", element["nodeType"].get<std::string>().c_str());
      assert(!"Unsupported contract-body-element type");
    }
    return ContractBodyElementTError;
  }

  const char* contract_body_element_to_str(ContractBodyElementT type)
  {
    switch(type)
    {
      ENUM_TO_STR(StateVarDecl)
      ENUM_TO_STR(FunctionDef)
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
    if (type_name.contains("typeString"))
    {
      // for AST node that contains ["typeName"]["typeDescriptions"]
      if (type_name["typeString"] == "uint8" ||
          type_name["typeString"] == "bool")
      {
        // For state var declaration,
        return ElementaryTypeName;
      }
      else if (type_name["typeString"].get<std::string>().find("int_const") != std::string::npos)
      {
        // For Literal, their typeString is like "int_const 100".
        return ElementaryTypeName;
      }
      else
      {
        printf("Got type-name typeString=%s\n", type_name["typeString"].get<std::string>().c_str());
        assert(!"Unsupported type-name type");
      }
    }
    else
    {
      // for AST node that contains ["typeDescriptions"] only
      if (type_name["nodeType"] == "ParameterList")
      {
        return ParameterList;
      }
      else
      {
        printf("Got type-name nodeType=%s\n", type_name["nodeType"].get<std::string>().c_str());
        assert(!"Unsupported type-name type");
      }
    }

    return TypeNameTError; // to make some old compiler happy
  }

  const char* type_name_to_str(TypeNameT type)
  {
    switch(type)
    {
      ENUM_TO_STR(ElementaryTypeName)
      ENUM_TO_STR(TypeNameTError)
      default:
      {
        assert(!"Unknown type-name type");
        return "UNKNOWN";
      }
    }
  }

  // rule elementary-type-name
  ElementaryTypeNameT get_elementary_type_name_t(const nlohmann::json &type_name)
  {
    // rule unsigned-integer-type
    if (type_name["typeString"] == "uint8")
    {
      return UINT8;
    }
    else if (type_name["typeString"] == "bool")
    {
      return BOOL;
    }
    else if (type_name["typeString"].get<std::string>().find("int_const") != std::string::npos)
    {
      // For Literal, their typeString is like "int_const 100".
      // TODO: Fix me! For simplicity, we assume everything is unsigned int.
      return UINT8;
    }
    else
    {
      printf("Got elementary-type-name typeString=%s\n", type_name["typeString"].get<std::string>().c_str());
      assert(!"Unsupported elementary-type-name type");
    }
    return ElementaryTypeNameTError;
  }

  const char* elementary_type_name_to_str(ElementaryTypeNameT type)
  {
    switch(type)
    {
      ENUM_TO_STR(UINT8)
      ENUM_TO_STR(BOOL)
      ENUM_TO_STR(ElementaryTypeNameTError)
      default:
      {
        assert(!"Unknown elementary-type-name type");
        return "UNKNOWN";
      }
    }
  }

  // rule parameter-list
  ParameterListT get_parameter_list_t(const nlohmann::json &type_name)
  {
    if (type_name["parameters"].size() == 0)
    {
      return EMPTY;
    }
    else
    {
      return NONEMPTY;
    }

    return ParameterListTError; // to make some old gcc compilers happy
  }

  const char* parameter_list_to_str(ParameterListT type)
  {
    switch(type)
    {
      ENUM_TO_STR(EMPTY)
      ENUM_TO_STR(NONEMPTY)
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
    else
    {
      printf("Got block nodeType=%s\n", block["nodeType"].get<std::string>().c_str());
      assert(!"Unsupported block type");
    }
    return BlockTError;
  }

  const char* block_to_str(BlockT type)
  {
    switch(type)
    {
      ENUM_TO_STR(Statement)
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
    else
    {
      printf("Got statement nodeType=%s\n", stmt["nodeType"].get<std::string>().c_str());
      assert(!"Unsupported statement type");
    }
    return StatementTError;
  }

  const char* statement_to_str(StatementT type)
  {
    switch(type)
    {
      ENUM_TO_STR(Block)
      ENUM_TO_STR(ExpressionStatement)
      ENUM_TO_STR(VariableDeclStatement)
      ENUM_TO_STR(ReturnStatement)
      ENUM_TO_STR(StatementTError)
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
    if (expr["nodeType"] == "Assignment" ||
        expr["nodeType"] == "BinaryOperation")
    {
      return BinaryOperatorClass;
    }
    else if (expr["nodeType"] == "Identifier" &&
             expr.contains("referencedDeclaration"))
    {
      return DeclRefExprClass;
    }
    else if (expr["nodeType"] == "Literal")
    {
      return Literal;
    }
    else if (expr["nodeType"] == "FunctionCall")
    {
      return CallExprClass;
    }
    else
    {
      printf("Got expression nodeType=%s\n", expr["nodeType"].get<std::string>().c_str());
      assert(!"Unsupported expression type");
    }
    return ExpressionTError;
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
    else if (expr["operator"] == ">")
    {
      return BO_GT;
    }
    else if (expr["operator"] == "<")
    {
      return BO_LT;
    }
    else
    {
      printf("Got expression operator=%s\n", expr["operator"].get<std::string>().c_str());
      assert(!"Unsupported expression operator");
    }

    return ExpressionTError; // make some old compilers happy
  }

  const char* expression_to_str(ExpressionT type)
  {
    switch(type)
    {
      ENUM_TO_STR(BinaryOperatorClass)
      ENUM_TO_STR(BO_Assign)
      ENUM_TO_STR(BO_Add)
      ENUM_TO_STR(BO_Sub)
      ENUM_TO_STR(BO_GT)
      ENUM_TO_STR(BO_LT)
      ENUM_TO_STR(DeclRefExprClass)
      ENUM_TO_STR(Literal)
      ENUM_TO_STR(CallExprClass)
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
    if (stmt["nodeType"] == "VariableDeclaration" &&
        stmt["stateVariable"] == false)
    {
      return VariableDecl;
    }
    else
    {
      printf("Got expression nodeType=%s\n", stmt["nodeType"].get<std::string>().c_str());
      assert(!"Unsupported variable-declaration-statement operator");
    }
    return VarDeclStmtTError; // make some old compilers happy
  }

  const char* var_decl_statement_to_str(VarDeclStmtT type)
  {
    switch(type)
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
    assert(decl["nodeType"] == "FunctionDefinition");
    if (decl["parameters"]["parameters"].size() == 0)
    {
      return FunctionNoProto;
    }
    else
    {
      return FunctionProto;
    }
    return FunctionDeclRefTError; // to make some old compilers happy
  }

  const char* func_decl_ref_to_str(FunctionDeclRefT type)
  {
    switch(type)
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
};
