#pragma once

#include <string>

enum class StatementType
{
  VARIABLE_ASSIGN,
  COMPOUND_ASSIGN,
  FUNC_DEFINITION,
  IF_STATEMENT,
  WHILE_STATEMENT,
  EXPR,
  RETURN,
  ASSERT,
  CLASS_DEFINITION,
  PASS,
  IMPORT,
  BREAK,
  CONTINUE,
  UNKNOWN,
};

enum class ExpressionType
{
  BINARY_OPERATION,
  UNARY_OPERATION,
  FUNC_CALL,
  IF_EXPR,
  LOGICAL_OPERATION,
  LITERAL,
  SUBSCRIPT,
  VARIABLE_REF,
  UNKNOWN,
};

struct function_id
{
  std::string function_name;
  std::string symbol_id;
  std::string class_name;
};

bool is_builtin_type(const std::string &name);

// TODO: Add a compilation flag or move it to a specific implementation
bool is_consensus_type(const std::string &name);

bool is_consensus_func(const std::string &name);

// Check if a function is defined in the "models" folder
bool is_model_func(const std::string &name);

std::string get_type_from_consensus_func(const std::string &name);
