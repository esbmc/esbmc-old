/// \file
/// Abstract Interpretation

#ifndef CPROVER_ANALYSES_AI_H
#define CPROVER_ANALYSES_AI_H

#include <iosfwd>
#include <map>
#include <memory>
#include <goto-programs/abstract-interpretation/ai_domain.h>
#include <goto-programs/goto_functions.h>
#include <util/xml.h>
#include <util/expr.h>

/**
 * This is the basic interface of the abstract interpreter with default
 * implementations of the core functionality.
 *
 * Users of abstract interpreters should use the interface given by this class.
 * It breaks into three categories:
 *
 * 1. Running an analysis, via operator() overloads
 * 2. Accessing the results of an analysis, by looking up the state map
 * 3. Outputting the results of the analysis;
*/
class ai_baset
{
public:
  typedef ai_domain_baset statet;

  ai_baset() = default;

  /**
   * @brief Run analysis over Program
   *
   * @param goto_program prograam under analysis
   * @param ns current namespace
   */
  void operator()(const goto_programt &goto_program, const namespacet &ns)
  {
    goto_functionst goto_functions;
    initialize(goto_program);
    entry_state(goto_program);
    fixedpoint(goto_program, goto_functions, ns);
    finalize();
  }

  /**
   * @brief Run analysis over Module (Goto Functions)
   *
   * @param goto_functions functions under analysis
   * @param ns current namespace
   */
  void operator()(const goto_functionst &goto_functions, const namespacet &ns)
  {
    initialize(goto_functions);
    entry_state(goto_functions);
    fixedpoint(goto_functions, ns);
    finalize();
  }

  // TODO: add history (for widening!)

  virtual std::unique_ptr<statet>
  abstract_state_before(goto_programt::const_targett l) const = 0;

  // Returns the abstract state after the given instruction
  virtual std::unique_ptr<statet>
  abstract_state_after(goto_programt::const_targett l) const
  {
    assert(!l->is_end_function()); // No state after the last instruction
    return abstract_state_before(std::next(l));
  }

  /// Resets the domain
  virtual void clear()
  {
  }

  virtual void
  output(const goto_functionst &goto_functions, std::ostream &out) const;

protected:
  // overload to add a factory
  virtual void initialize(const goto_programt &);
  virtual void initialize(const goto_functiont &);
  virtual void initialize(const goto_functionst &);

  // override to add a cleanup step after fixedpoint has run
  virtual void finalize();

  void entry_state(const goto_programt &);
  void entry_state(const goto_functionst &);

  /* The fixedpoint is computed through a Work set algorithm which
   * consists in adding nodes that have changed with the current merge
  */
  // the work-queue is sorted by location number
  typedef std::unordered_map<unsigned, goto_programt::const_targett>
    working_sett;

  goto_programt::const_targett get_next(working_sett &working_set);

  void
  put_in_working_set(working_sett &working_set, goto_programt::const_targett l)
  {
    working_set.insert(
      std::pair<unsigned, goto_programt::const_targett>(l->location_number, l));
  }

  // true = found something new
  bool fixedpoint(
    const goto_programt &goto_program,
    const goto_functionst &goto_functions,
    const namespacet &ns);

  virtual void
  fixedpoint(const goto_functionst &goto_functions, const namespacet &ns) = 0;

  void sequential_fixedpoint(
    const goto_functionst &goto_functions,
    const namespacet &ns);

  // Visit performs one step of abstract interpretation from location l
  // Depending on the instruction type it may compute a number of "edges"
  // or applications of the abstract transformer
  // true = found something new
  bool visit(
    goto_programt::const_targett l,
    working_sett &working_set,
    const goto_programt &goto_program,
    const goto_functionst &goto_functions,
    const namespacet &ns);

  // function calls
  bool do_function_call_rec(
    goto_programt::const_targett l_call,
    goto_programt::const_targett l_return,
    const expr2tc &function,
    const goto_functionst &goto_functions,
    const namespacet &ns);

  bool do_function_call(
    goto_programt::const_targett l_call,
    goto_programt::const_targett l_return,
    const goto_functionst &goto_functions,
    const goto_functionst::function_mapt::const_iterator f_it,
    const namespacet &ns);

  // abstract methods

  virtual bool merge(
    const statet &src,
    goto_programt::const_targett from,
    goto_programt::const_targett to) = 0;
  // for concurrent fixedpoint
  virtual bool merge_shared(
    const statet &src,
    goto_programt::const_targett from,
    goto_programt::const_targett to,
    const namespacet &ns) = 0;
  virtual statet &get_state(goto_programt::const_targett l) = 0;
  virtual const statet &find_state(goto_programt::const_targett l) const = 0;
  virtual std::unique_ptr<statet> make_temporary_state(const statet &s) = 0;
};

// domainT is expected to be derived from ai_domain_baseT
template <typename domainT>
class ait : public ai_baset
{
public:
  // constructor
  ait() : ai_baset()
  {
  }

  domainT &operator[](goto_programt::const_targett l)
  {
    typename state_mapt::iterator it = state_map.find(l);
    if (it == state_map.end())
      throw "failed to find state";

    return it->second;
  }

  bool target_is_mapped(goto_programt::const_targett l) const
  {
    return state_map.count(l) != 0;
  }

  const domainT &operator[](goto_programt::const_targett l) const
  {
    typename state_mapt::const_iterator it = state_map.find(l);
    if (it == state_map.end())
      throw "failed to find state";

    return it->second;
  }

  std::unique_ptr<statet>
  abstract_state_before(goto_programt::const_targett t) const override
  {
    typename state_mapt::const_iterator it = state_map.find(t);
    if (it == state_map.end())
    {
      std::unique_ptr<statet> d = std::make_unique<domainT>();
      assert(d->is_bottom());
      return d;
    }

    return std::make_unique<domainT>(it->second);
  }

  void clear() override
  {
    state_map.clear();
    ai_baset::clear();
  }

  typedef std::unordered_map<
    goto_programt::const_targett,
    domainT,
    const_target_hash,
    pointee_address_equalt>
    state_mapt;
  state_mapt state_map;

protected:
  // this one creates states, if need be
  virtual statet &get_state(goto_programt::const_targett l) override
  {
    return state_map[l]; // calls default constructor
  }

  // this one just finds states
  const statet &find_state(goto_programt::const_targett l) const override
  {
    typename state_mapt::const_iterator it = state_map.find(l);
    if (it == state_map.end())
      throw "failed to find state";

    return it->second;
  }

  bool merge(
    const statet &src,
    goto_programt::const_targett from,
    goto_programt::const_targett to) override
  {
    statet &dest = get_state(to);
    return static_cast<domainT &>(dest).merge(
      static_cast<const domainT &>(src), from, to);
  }

  std::unique_ptr<statet> make_temporary_state(const statet &s) override
  {
    return std::make_unique<domainT>(static_cast<const domainT &>(s));
  }

  void fixedpoint(const goto_functionst &goto_functions, const namespacet &ns)
    override
  {
    sequential_fixedpoint(goto_functions, ns);
  }

private:
  // to enforce that domainT is derived from ai_domain_baset
  void dummy(const domainT &s)
  {
    const statet &x = s;
    (void)x;
  }

  // not implemented in sequential analyses
  bool merge_shared(
    const statet &,
    goto_programt::const_targett,
    goto_programt::const_targett,
    const namespacet &) override
  {
    throw "not implemented";
  }
};

#endif // CPROVER_ANALYSES_AI_H
