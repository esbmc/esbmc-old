#ifndef CPROVER_GOTO_PROGRAMS_RW_SET
#define CPROVER_GOTO_PROGRAMS_RW_SET

#include <pointer-analysis/value_sets.h>
#include <util/expr_util.h>
#include <util/guard.h>
#include <util/namespace.h>
#include <util/std_code.h>

class rw_sett
{
public:
  struct entryt
  {
    irep_idt object;
    bool r, w, deref;
    exprt guard;
    exprt original_expr;

    entryt() : r(false), w(false), guard(true_exprt())
    {
    }

    const exprt &get_guard() const
    {
      return guard;
    }

    std::string get_comment() const
    {
      std::string result;
      if (w)
        result = "W/W";
      else
        result = "R/W";

      result += " data race on " + id2string(object);

      return result;
    }
  };

  typedef std::unordered_map<irep_idt, entryt, irep_id_hash> entriest;
  entriest entries;

  void compute(const exprt &expr);

  rw_sett(
    const namespacet &_ns,
    value_setst &_value_sets,
    goto_programt::const_targett _target)
    : ns(_ns), value_sets(_value_sets), target(_target)
  {
  }

  rw_sett(
    const namespacet &_ns,
    value_setst &_value_sets,
    goto_programt::const_targett _target,
    const exprt &expr)
    : ns(_ns), value_sets(_value_sets), target(_target)
  {
    compute(expr);
  }

  void read_rec(const exprt &expr)
  {
    read_write_rec(expr, true, false, "", guardt(), exprt());
  }

  void
  read_rec(const exprt &expr, const guardt &guard, const exprt &original_expr)
  {
    read_write_rec(expr, true, false, "", guard, original_expr);
  }

protected:
  const namespacet &ns;
  value_setst &value_sets;
  const goto_programt::const_targett target;

  void assign(const exprt &lhs, const exprt &rhs);

  void read_write_rec(
    const exprt &expr,
    bool r,
    bool w,
    const std::string &suffix,
    const guardt &guard,
    const exprt &original_expr,
    bool dereferenced = false);
};

#define forall_rw_set_entries(it, rw_set)                                      \
  for (rw_sett::entriest::const_iterator it = (rw_set).entries.begin();        \
       it != (rw_set).entries.end();                                           \
       it++)

#endif
