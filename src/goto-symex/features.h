#pragma once

#include <unordered_set>
#include <util/algorithms.h>
#include <goto-symex/symex_target_equation.h>

enum class SSA_FEATURES
{
  NON_LINEAR,
  NON_INTEGER_NUMERAL,
  BITWISE_OPERATIONS,
  OVERFLOW_ASSERTIONS,
  ARRAY,
  STRUCTS
};

class ssa_features : public ssa_step_algorithm
{
public:
  ssa_features() : ssa_step_algorithm(false){};
  bool run(symex_target_equationt::SSA_stepst &) override;

  std::unordered_set<SSA_FEATURES> features;
  void print_result() const;

  BigInt ignored() const override
  {
    return 0;
  }

protected:
  void check(const expr2tc &e);

  bool is_entirely_constant(const expr2tc &e);
};
