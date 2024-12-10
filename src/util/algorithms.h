#ifndef ESBMC_ALGORITHM_H
#define ESBMC_ALGORITHM_H

#include <goto-programs/goto_functions.h>
#include <goto-programs/loopst.h>
#include <goto-programs/goto_loops.h>
#include <goto-symex/symex_target_equation.h>
#include <util/message.h>
/**
 * @brief Base interface to run an algorithm in esbmc
 */
template <typename T>
class algorithm
{
public:
  algorithm(bool sideeffect) : sideeffect(sideeffect)
  {
  }

  virtual ~algorithm() = default;

  /**
   * @brief Executes the algorithm over a T object
   *
   * @return success of the algorithm
   */
  virtual bool run(T &) = 0;

  /**
   * @brief Says wether the algorithm is a plain analysis
   * or if it also changes the structure
   *
   */
  bool has_sideeffect()
  {
    return sideeffect;
  }

protected:
  // The algorithm changes the CFG, container in some way?
  const bool sideeffect;
};

/**
 * @brief Base interface for goto-functions algorithms
 */
class goto_functions_algorithm : public algorithm<goto_functionst>
{
public:
  explicit goto_functions_algorithm(bool sideffect) : algorithm(sideffect)
  {
  }

  unsigned get_number_of_functions()
  {
    return number_of_functions;
  }
  unsigned get_number_of_loops()
  {
    return number_of_loops;
  }

  bool run(goto_functionst &) override;
  void setTarget(const std::string &_tgt)
  {
    target_function = _tgt;
  }

protected:
  virtual bool runOnFunction(std::pair<const dstring, goto_functiont> &F);
  virtual bool runOnLoop(loopst &loop, goto_programt &goto_program);
  virtual bool runOnProgram(goto_functionst &)
  {
    return false;
  }
  std::string target_function = "";

private:
  unsigned number_of_functions = 0;
  unsigned number_of_loops = 0;
};

/**
 * @brief Base interface for ssa-step algorithms
 */
class ssa_step_algorithm : public algorithm<symex_target_equationt::SSA_stepst>
{
public:
  explicit ssa_step_algorithm(bool sideffect) : algorithm(sideffect)
  {
  }

  /// How many steps were ignored after this algorithm
  virtual BigInt ignored() const = 0;

  void run_on_step(symex_target_equationt::SSA_stept &);

  virtual void run_on_assignment(symex_target_equationt::SSA_stept &)
  {
  }
  virtual void run_on_assume(symex_target_equationt::SSA_stept &)
  {
  }
  virtual void run_on_assert(symex_target_equationt::SSA_stept &)
  {
  }
  virtual void run_on_output(symex_target_equationt::SSA_stept &)
  {
  }
  virtual void run_on_skip(symex_target_equationt::SSA_stept &)
  {
  }
  virtual void run_on_renumber(symex_target_equationt::SSA_stept &)
  {
  }
};

/**
 * @brief Base interface for ssa-step algorithms
 */
class expr2tc_algorithm : public algorithm<expr2tc>
{
public:
  explicit expr2tc_algorithm(bool sideffect) : algorithm(sideffect)
  {
  }
};

#endif //ESBMC_ALGORITHM_H
