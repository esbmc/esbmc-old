#include <goto-programs/add_race_assertions.h>
#include <goto-programs/remove_no_op.h>
#include <goto-programs/rw_set.h>
#include <pointer-analysis/value_sets.h>
#include <util/expr_util.h>
#include <util/guard.h>
#include <util/std_expr.h>

class w_guardst
{
public:
  w_guardst(contextt &_context) : context(_context)
  {
  }

  std::list<irep_idt> w_guards;

  const symbolt &get_guard_symbol(
    const irep_idt &object,
    const exprt &original_expr,
    bool deref)
  {
    const irep_idt identifier =
      deref ? "__ESBMC_deref_" + id2string(object) : "tmp_" + id2string(object);

    const symbolt *s = context.find_symbol(identifier);
    if (s != nullptr)
      return *s;

    if (!deref)
      w_guards.push_back(identifier);

    type2tc index = array_type2tc(get_bool_type(), expr2tc(), true);

    symbolt new_symbol;
    new_symbol.id = identifier;
    new_symbol.name = identifier;
    new_symbol.type =
      original_expr.is_index() ? migrate_type_back(index) : typet("bool");
    new_symbol.static_lifetime = true;
    new_symbol.value.make_false();

    symbolt *symbol_ptr;
    context.move(new_symbol, symbol_ptr);
    return *symbol_ptr;
  }

  const exprt get_guard_symbol_expr(
    const irep_idt &object,
    const exprt &original_expr,
    bool deref)
  {
    exprt expr = symbol_expr(get_guard_symbol(object, original_expr, deref));

    if (original_expr.is_index() && expr.type().is_array())
    {
      index_exprt full_expr = to_index_expr(original_expr);

      index_exprt index;
      index.array() = expr;
      index.index() = full_expr.index();
      index.type() = typet("bool");

      expr.swap(index);
    }

    return expr;
  }

  const exprt get_w_guard_expr(const rw_sett::entryt &entry)
  {
    return get_guard_symbol_expr(
      entry.object, entry.original_expr, entry.deref);
  }

  const exprt get_assertion(const rw_sett::entryt &entry)
  {
    return gen_not(
      get_guard_symbol_expr(entry.object, entry.original_expr, entry.deref));
  }

  void add_initialization(goto_programt &goto_program) const;

protected:
  contextt &context;
};

void w_guardst::add_initialization(goto_programt &goto_program) const
{
  goto_programt::targett t = goto_program.instructions.begin();
  const namespacet ns(context);

  for (const auto &w_guard : w_guards)
  {
    const symbolt &s = *ns.lookup(w_guard);
    exprt symbol = symbol_expr(s);
    expr2tc new_sym;
    migrate_expr(symbol, new_sym);

    expr2tc falsity = s.type.is_array() ? gen_zero(migrate_type(s.type), true)
                                        : gen_false_expr();
    t = goto_program.insert(t);
    t->type = ASSIGN;
    t->code = code_assign2tc(new_sym, falsity);

    t++;
  }
}

void add_race_assertions(
  value_setst &value_sets,
  contextt &context,
  goto_programt &goto_program,
  w_guardst &w_guards)
{
  namespacet ns(context);

  bool is_atomic = false;

  Forall_goto_program_instructions (i_it, goto_program)
  {
    goto_programt::instructiont &instruction = *i_it;

    if (instruction.is_atomic_begin())
      is_atomic = true;

    if (
      (instruction.is_assign() || instruction.is_other() ||
       instruction.is_return() || instruction.is_goto() ||
       instruction.is_assert() || instruction.is_function_call() ||
       instruction.is_assume()) &&
      !is_atomic)
    {
      exprt tmp_expr;
      if (instruction.is_goto() || instruction.is_assert())
        tmp_expr = migrate_expr_back(instruction.guard);
      else
        tmp_expr = migrate_expr_back(instruction.code);

      rw_sett rw_set(ns, value_sets, i_it, tmp_expr);

      if (rw_set.entries.empty())
        continue;

      goto_programt::instructiont original_instruction;
      original_instruction.swap(instruction);

      instruction.make_skip();
      i_it++;

      // now add assignments for what is written -- reset
      forall_rw_set_entries(e_it, rw_set)
      {
        goto_programt::targett t = goto_program.insert(i_it);

        t->type = ASSIGN;
        code_assignt theassign(
          w_guards.get_w_guard_expr(e_it->second), false_exprt());
        migrate_expr(theassign, t->code);

        t->location = original_instruction.location;
        i_it = ++t;
      }

      // Avoid adding too much thread interleaving by using atomic block
      // tmp_A = 0;
      // atomic {A = n; Assert tmp_A == 0; tmp_A = 1;}
      // See https://github.com/esbmc/esbmc/pull/1544
      goto_programt::targett t = goto_program.insert(i_it);
      *t = ATOMIC_BEGIN;
      i_it = ++t;

      // now add assertion for what is read and written
      forall_rw_set_entries(e_it, rw_set)
      {
        goto_programt::targett t = goto_program.insert(i_it);

        expr2tc assert;
        migrate_expr(w_guards.get_assertion(e_it->second), assert);
        t->make_assertion(assert);
        t->location = original_instruction.location;
        t->location.user_provided(false);
        t->location.comment(e_it->second.get_comment());
        i_it = ++t;
      }

      // now add assignments for what is written -- set
      forall_rw_set_entries(e_it, rw_set) if (e_it->second.w)
      {
        goto_programt::targett t = goto_program.insert(i_it);

        t->type = ASSIGN;
        code_assignt theassign(
          w_guards.get_w_guard_expr(e_it->second), e_it->second.get_guard());

        migrate_expr(theassign, t->code);

        t->location = original_instruction.location;
        i_it = ++t;
      }

      // insert original statement here
      // We need to keep all instructions before the return,
      // so when we process the return we need add the
      // original instruction at the end
      if (!original_instruction.is_return() && !original_instruction.is_goto())
      {
        goto_programt::targett t = goto_program.insert(i_it);

        *t = original_instruction;
        i_it = ++t;
      }

      {
        goto_programt::targett t = goto_program.insert(i_it);

        *t = ATOMIC_END;
        i_it = ++t;
      }

      if (original_instruction.is_return() || original_instruction.is_goto())
      {
        goto_programt::targett t = goto_program.insert(i_it);
        *t = original_instruction;
        i_it = ++t;
      }

      i_it--; // the for loop already counts us up
    }

    if (instruction.is_atomic_end())
      is_atomic = false;
  }

  remove_no_op(goto_program);
}

void add_race_assertions(
  value_setst &value_sets,
  contextt &context,
  goto_programt &goto_program)
{
  w_guardst w_guards(context);

  add_race_assertions(value_sets, context, goto_program, w_guards);

  w_guards.add_initialization(goto_program);
  goto_program.update();
}

void add_race_assertions(
  value_setst &value_sets,
  contextt &context,
  goto_functionst &goto_functions)
{
  w_guardst w_guards(context);

  Forall_goto_functions (f_it, goto_functions)
    if (f_it->first != goto_functions.main_id())
      add_race_assertions(value_sets, context, f_it->second.body, w_guards);

  // get "main"
  goto_functionst::function_mapt::iterator m_it =
    goto_functions.function_map.find(goto_functions.main_id());

  if (m_it != goto_functions.function_map.end())
  {
    goto_programt &main = m_it->second.body;
    w_guards.add_initialization(main);
  }

  goto_functions.update();
}