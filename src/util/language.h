#ifndef CPROVER_LANGUAGE_H
#define CPROVER_LANGUAGE_H

#include <cstdio>
#include <set>
#include <util/context.h>
#include <util/namespace.h>

enum class presentationt
{
  HUMAN,
  WITNESS,
};

class languaget
{
public:
  // parse file
  virtual bool parse(const std::string &path) = 0;

  // final adjustments, e.g., initialization and call to main()
  virtual bool final(contextt &)
  {
    return false;
  }

  // type check a module in the currently parsed file
  virtual bool typecheck(contextt &context, const std::string &module) = 0;

  // language id
  /* This is used by language_filest::final() to call languaget::final() only
   * once for each concrete languaget in case of multiple source files. */
  virtual std::string id() const
  {
    return "";
  }

  // show parse tree

  virtual void show_parse(std::ostream &out) = 0;

  // conversion of expressions
  bool from_expr(
    const exprt &expr,
    std::string &code,
    const namespacet &ns,
    presentationt target = presentationt::HUMAN)
  {
    return from_expr(expr, code, ns, default_flags(target));
  }

  bool from_type(
    const typet &type,
    std::string &code,
    const namespacet &ns,
    presentationt target = presentationt::HUMAN)
  {
    return from_type(type, code, ns, default_flags(target));
  }

  virtual unsigned default_flags(presentationt /* target */) const
  {
    return 0;
  }

  virtual languaget *new_language() const = 0;

  virtual ~languaget() = default;

protected:
  virtual bool from_expr(
    const exprt &expr,
    std::string &code,
    const namespacet &ns,
    unsigned flags) = 0;

  virtual bool from_type(
    const typet &type,
    std::string &code,
    const namespacet &ns,
    unsigned flags) = 0;
};

#endif
