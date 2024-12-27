#include <goto-programs/rw_set.h>
#include <pointer-analysis/goto_program_dereference.h>
#include <util/arith_tools.h>
#include <util/expr_util.h>
#include <util/namespace.h>
#include <util/std_expr.h>

void rw_sett::compute(const exprt &expr)
{
  const goto_programt::instructiont &instruction = *target;

  if (expr.is_code())
  {
    codet code = to_code(expr);
    const irep_idt &statement = code.get_statement();

    if (statement == "assign")
    {
      assert(code.operands().size() == 2);
      assign(code.op0(), code.op1());
    }
    else if (statement == "printf")
    {
      exprt expr = code;
      Forall_operands (it, expr)
        read_rec(*it);
    }
    else if (statement == "return")
    {
      assert(code.operands().size() == 1);
      read_rec(code.op0());
    }
    else if (statement == "function_call")
    {
      assert(code.operands().size());
      read_write_rec(code.op0(), false, true, "", guardt(), exprt());
      // check args of function call
      if (
        !has_prefix(instruction.location.function(), "ESBMC_execute_kernel") &&
        !has_prefix(instruction.location.function(), "ESBMC_verify_kernel"))
        Forall_operands (it, code.op2())
          if (!(*it).type().is_pointer())
            read_rec(*it);
    }
  }
  else if (
    instruction.is_goto() || instruction.is_assert() || instruction.is_assume())
  {
    read_rec(expr);
  }
}

void rw_sett::assign(const exprt &lhs, const exprt &rhs)
{
  read_rec(rhs);
  read_write_rec(lhs, false, true, "", guardt(), exprt());
}

void rw_sett::read_write_rec(
  const exprt &expr,
  bool r,
  bool w,
  const std::string &suffix,
  const guardt &guard,
  const exprt &original_expr,
  bool dereferenced)
{
  if (expr.is_symbol() && !expr.has_operands())
  {
    const symbol_exprt &symbol_expr = to_symbol_expr(expr);

    const symbolt *symbol = ns.lookup(symbol_expr.get_identifier());
    if (symbol)
    {
      if (!symbol->static_lifetime && !dereferenced)
      {
        return; // ignore for now
      }

      if (
        symbol->name == "__ESBMC_alloc" ||
        symbol->name == "__ESBMC_alloc_size" || symbol->name == "stdin" ||
        symbol->name == "stdout" || symbol->name == "stderr" ||
        symbol->name == "sys_nerr" || symbol->name == "operator=::ref" ||
        symbol->name == "this" || symbol->name == "__ESBMC_atexits")
      {
        return; // ignore for now
      }

      // Improvements for CUDA features
      if (symbol->name == "indexOfThread" || symbol->name == "indexOfBlock")
      {
        return; // ignore for now
      }
    }

    irep_idt object = id2string(symbol_expr.get_identifier()) + suffix;

    entryt &entry = entries[object];
    entry.object = object;
    entry.r = entry.r || r;
    entry.w = entry.w || w;
    entry.deref = expr.type().is_pointer() && dereferenced;
    entry.guard = migrate_expr_back(guard.as_expr());
    entry.original_expr = original_expr;
  }
  else if (expr.is_member())
  {
    assert(expr.operands().size() == 1);
    const std::string &component_name = expr.component_name().as_string();
    read_write_rec(
      expr.op0(), r, w, "." + component_name + suffix, guard, original_expr);
  }
  else if (expr.is_index())
  {
    assert(expr.operands().size() == 2);
    read_write_rec(expr.op0(), r, w, suffix, guard, expr, dereferenced);
  }
  else if (expr.is_dereference())
  {
    assert(expr.operands().size() == 1);
    read_rec(expr.op0(), guard, original_expr);

    expr2tc tmp_expr;
    migrate_expr(expr, tmp_expr);
    dereference(target, tmp_expr, ns, value_sets);
    exprt tmp = migrate_expr_back(tmp_expr);

    // If dereferencing fails, then we revert the variable
    // and we will attempt dereferencing in symex
    if (
      has_prefix(id2string(tmp.identifier()), "symex::invalid_object") ||
      id2string(tmp.identifier()) == "")
      tmp = expr.op0();

    if (tmp.id() == "+")
    {
      index_exprt tmp_index(tmp.op0(), tmp.op1(), tmp.type());
      tmp.swap(tmp_index);
    }

    read_write_rec(tmp, r, w, suffix, guard, original_expr, true);
  }
  else if (expr.is_address_of() || expr.id() == "implicit_address_of")
  {
    assert(expr.operands().size() == 1);
  }
  else if (expr.id() == "if")
  {
    assert(expr.operands().size() == 3);
    read_rec(expr.op0(), guard, original_expr);

    guardt true_guard(guard);
    expr2tc tmp_expr;
    migrate_expr(expr.op0(), tmp_expr);
    true_guard.add(tmp_expr);
    read_write_rec(expr.op1(), r, w, suffix, true_guard, original_expr);

    guardt false_guard(guard);
    migrate_expr(gen_not(expr.op0()), tmp_expr);
    false_guard.add(tmp_expr);
    read_write_rec(expr.op2(), r, w, suffix, false_guard, original_expr);
  }
  else
  {
    forall_operands (it, expr)
      read_write_rec(*it, r, w, suffix, guard, original_expr);
  }
}
