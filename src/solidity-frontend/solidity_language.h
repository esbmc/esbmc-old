#ifndef SOLIDITY_FRONTEND_SOLIDITY_AST_LANGUAGE_H_
#define SOLIDITY_FRONTEND_SOLIDITY_AST_LANGUAGE_H_

#include <clang-c-frontend/clang_c_language.h>
#include <util/language.h>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>
#include <boost/filesystem.hpp>

class solidity_languaget : public clang_c_languaget
{
public:
  solidity_languaget();

  bool parse(const std::string &path) override;

  bool final(contextt &context) override;

  bool typecheck(contextt &context, const std::string &module) override;

  std::string id() const override
  {
    return "solidity_ast";
  }

  void show_parse(std::ostream &out) override;

  // temp file used by clang-c-frontend
  std::string temp_path;

  // Functions to handle temp C file used by clang-c-frontend
  std::string get_temp_file();
  std::string temp_c_file();

  languaget *new_language() const override
  {
    return new solidity_languaget;
  }

  bool convert_intrinsics(contextt &context);

  // function name for verification that requires this information before GOTO conversion phase.
  std::string func_name;

  // smart contract source
  std::string smart_contract;

  // store AST json in nlohmann::json data structure
  nlohmann::json src_ast_json_array = nlohmann::json::array();
  nlohmann::json intrinsic_json;

  languaget *clang_c_module;
};

languaget *new_solidity_language();

#endif /* SOLIDITY_FRONTEND_SOLIDITY_AST_LANGUAGE_H_ */
