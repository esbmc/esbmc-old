#ifndef SOLIDITY_FRONTEND_PATTERN_CHECK_H_
#define SOLIDITY_FRONTEND_PATTERN_CHECK_H_

#include <memory>
#include <iomanip>
#include <util/context.h>
#include <util/namespace.h>
#include <util/std_types.h>
#include <fmt/core.h>
#include <nlohmann/json.hpp>
#include <solidity-frontend/solidity_grammar.h>

class pattern_checker
{
  // There are two types of vulnerabilities:
  //  - pattern-based vulnerability (e.g. Authoriation via TxOrigin)
  //  - reasoning-based vulnerability (e.g. array out-of-bound access)
  // This class implements the detection of pattern-based vulnerbaility.
  // The reasoning-based vulnerability is handled by ESBMC verification pipeline.
public:
  pattern_checker(
    const nlohmann::json &_ast_nodes,
    const std::string &_target_func);
  virtual ~pattern_checker() = default;

  void do_pattern_check();
  void start_pattern_based_check(const nlohmann::json &func);

  // Authorization through Tx origin
  void check_authorization_through_tx_origin(const nlohmann::json &func);
  void check_require_call(const nlohmann::json &expr);
  void check_require_argument(const nlohmann::json &call_args);
  void check_tx_origin(const nlohmann::json &left_expr);

protected:
  const nlohmann::json &ast_nodes;
  const std::string target_func; // function to be verified
};

#endif /* SOLIDITY_FRONTEND_PATTERN_CHECK_H_ */
