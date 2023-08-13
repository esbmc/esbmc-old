#include <cassert>
#include <goto-symex/execution_state.h>
#include <goto-symex/goto_symex.h>
#include <goto-symex/goto_symex_state.h>
#include <goto-symex/reachability_tree.h>
#include <goto-symex/symex_target_equation.h>

#include <pointer-analysis/value_set_analysis.h>

#include <util/c_types.h>
#include <util/config.h>
#include <util/expr_util.h>
#include <irep2/irep2.h>
#include <util/migrate.h>
#include <util/prefix.h>
#include <util/pretty.h>
#include <util/simplify_expr.h>
#include <util/std_expr.h>
#include <vector>

bool goto_symext::check_incremental(const expr2tc &expr, const std::string &msg)
{
  auto rte = std::dynamic_pointer_cast<runtime_encoded_equationt>(target);
  expr2tc question = equality2tc(gen_true_expr(), expr);
  try
  {
    // check whether the assertion holds
    tvt res = rte->ask_solver_question(question);
    // we don't add this assertion to the resulting logical formula
    if(res.is_true())
      // incremental verification succeeded
      return true;
    // this assertion evaluates to false via incremental SMT solving
    if(res.is_false())
    {
      // check assertion to produce a counterexample
      assertion(gen_false_expr(), msg);
      // eliminate subsequent execution paths
      assume(gen_false_expr());
      // incremental verification succeeded
      return true;
    }
    log_status("Incremental verification returned unknown");
    // incremental verification returned unknown
    return false;
  }
  catch(runtime_encoded_equationt::dual_unsat_exception &e)
  {
    log_error(
      "This solver was unable to check this expression. Please try it with "
      "another solver");
  }
  return false;
}

void goto_symext::claim(const expr2tc &claim_expr, const std::string &msg)
{
  // Convert asserts in assumes, if it's not the last loop iteration
  // also, don't convert assertions added by the bidirectional search
  if(inductive_step && first_loop && !cur_state->source.pc->inductive_assertion)
  {
    BigInt unwind = cur_state->loop_iterations[first_loop];
    if(unwind < (max_unwind - 1))
    {
      assume(claim_expr);
      return;
    }
  }

  // Can happen when evaluating certain special intrinsics. Gulp.
  if(cur_state->guard.is_false())
    return;

  total_claims++;

  expr2tc new_expr = claim_expr;
  cur_state->rename(new_expr);

  // first try simplifier on it
  do_simplify(new_expr);

  if(is_true(new_expr))
    return;

  if(options.get_bool_option("smt-symex-assert"))
  {
    if(check_incremental(new_expr, msg))
      // incremental verification has succeeded
      return;
  }

  // add assertion to the target equation
  assertion(new_expr, msg);
}

void goto_symext::assertion(
  const expr2tc &the_assertion,
  const std::string &msg)
{
  expr2tc expr = the_assertion;
  cur_state->guard.guard_expr(expr);
  cur_state->global_guard.guard_expr(expr);
  remaining_claims++;
  target->assertion(
    cur_state->guard.as_expr(),
    expr,
    msg,
    cur_state->gen_stack_trace(),
    cur_state->source,
    first_loop);
}

void goto_symext::assume(const expr2tc &the_assumption)
{
  expr2tc assumption = the_assumption;
  cur_state->rename(assumption);
  do_simplify(assumption);

  if(is_true(assumption))
    return;

  cur_state->guard.guard_expr(assumption);

  // Irritatingly, assumption destroys its expr argument
  expr2tc tmp_guard = cur_state->guard.as_expr();
  target->assumption(tmp_guard, assumption, cur_state->source, first_loop);

  // If we're assuming false, make the guard for the following statement false
  if(is_false(the_assumption))
    cur_state->guard.make_false();
}

std::shared_ptr<goto_symext::symex_resultt> goto_symext::get_symex_result()
{
  return std::shared_ptr<goto_symext::symex_resultt>(
    new goto_symext::symex_resultt(target, total_claims, remaining_claims));
}

void goto_symext::symex_step(reachability_treet &art)
{
  assert(!cur_state->call_stack.empty());

  const goto_programt::instructiont &instruction = *cur_state->source.pc;

  // depth exceeded?
  {
    if(depth_limit != 0 && cur_state->num_instructions > depth_limit)
      cur_state->guard.add(gen_false_expr());
    cur_state->num_instructions++;
  }

  // Remember the first loop we're entering
  if(inductive_step && instruction.loop_number && !first_loop)
    first_loop = instruction.loop_number;

  // actually do instruction
  switch(instruction.type)
  {
  case SKIP:
  case LOCATION:
    // really ignore
    cur_state->source.pc++;
    break;

  case END_FUNCTION:
    symex_end_of_function();

    // Potentially skip to run another function ptr target; if not,
    // continue
    if(!run_next_function_ptr_target(false))
      cur_state->source.pc++;
    break;

  case GOTO:
  {
    expr2tc tmp(instruction.guard);
    replace_nondet(tmp);

    dereference(tmp, dereferencet::READ);
    replace_dynamic_allocation(tmp);

    symex_goto(tmp);
  }
  break;

  case ASSUME:
    symex_assume();
    cur_state->source.pc++;
    break;

  case ASSERT:
    symex_assert();
    cur_state->source.pc++;
    break;

  case RETURN:
    if(!cur_state->guard.is_false())
    {
      expr2tc thecode = instruction.code, assign;
      if(make_return_assignment(assign, thecode))
      {
        goto_symext::symex_assign(assign);
      }

      symex_return(thecode);
    }

    cur_state->source.pc++;
    break;

  case ASSIGN:
    if(!cur_state->guard.is_false())
    {
      code_assign2t deref_code = to_code_assign2t(instruction.code); // copy

      // XXX jmorse -- this is not fully symbolic.
      if(auto it = thrown_obj_map.find(cur_state->source.pc);
         it != thrown_obj_map.end())
      {
        const expr2tc &thrown_obj = it->second;
        assert(is_symbol2t(thrown_obj));

        if(
          is_pointer_type(deref_code.target->type) &&
          !is_pointer_type(thrown_obj->type))
        {
          expr2tc new_thrown_obj = address_of2tc(thrown_obj->type, thrown_obj);
          deref_code.source = new_thrown_obj;
        }
        else
          deref_code.source = thrown_obj;

        thrown_obj_map.erase(cur_state->source.pc);
      }

      symex_assign(code_assign2tc(std::move(deref_code)));
    }

    cur_state->source.pc++;
    break;

  case FUNCTION_CALL:
  {
    expr2tc deref_code = instruction.code;
    replace_nondet(deref_code);

    code_function_call2t &call = to_code_function_call2t(deref_code);

    if(!is_nil_expr(call.ret))
    {
      dereference(call.ret, dereferencet::WRITE);
    }

    replace_dynamic_allocation(deref_code);

    for(auto &operand : call.operands)
      if(!is_nil_expr(operand))
        dereference(operand, dereferencet::READ);

    // Always run intrinsics, whether guard is false or not. This is due to the
    // unfortunate circumstance where a thread starts with false guard due to
    // decision taken in another thread in this trace. In that case the
    // terminate intrinsic _has_ to run, or we explode.
    if(is_symbol2t(call.function))
    {
      const irep_idt &id = to_symbol2t(call.function).thename;
      if(has_prefix(id.as_string(), "c:@F@__ESBMC"))
      {
        cur_state->source.pc++;
        run_intrinsic(call, art, id.as_string());
        return;
      }
    }

    // Don't run a function call if the guard is false.
    if(!cur_state->guard.is_false())
    {
      symex_function_call(deref_code);
    }
    else
    {
      cur_state->source.pc++;
    }
  }
  break;

  case DECL:
    if(!cur_state->guard.is_false())
      symex_decl(instruction.code);
    cur_state->source.pc++;
    break;

  case DEAD:
    if(!cur_state->guard.is_false())
      symex_dead(instruction.code);
    cur_state->source.pc++;
    break;

  case OTHER:
    if(!cur_state->guard.is_false())
      symex_other(instruction.code);
    cur_state->source.pc++;
    break;

  case CATCH:
    symex_catch();
    break;

  case THROW:
    if(!cur_state->guard.is_false())
    {
      if(symex_throw())
        cur_state->source.pc++;
    }
    else
    {
      cur_state->source.pc++;
    }
    break;

  case THROW_DECL:
    symex_throw_decl();
    cur_state->source.pc++;
    break;

  case THROW_DECL_END:
    // When we reach THROW_DECL_END, we must clear any throw_decl
    if(stack_catch.size())
    {
      // Get to the correct try (always the last one)
      goto_symex_statet::exceptiont *except = &stack_catch.top();

      except->has_throw_decl = false;
      except->throw_list_set.clear();
    }

    cur_state->source.pc++;
    break;

  default:
    log_error(
      "GOTO instruction type {} not handled in goto_symext::symex_step",
      instruction.type);
    abort();
  }
}

void goto_symext::symex_assume()
{
  if(cur_state->guard.is_false())
    return;

  expr2tc cond = cur_state->source.pc->guard;

  replace_nondet(cond);
  dereference(cond, dereferencet::READ);
  replace_dynamic_allocation(cond);

  assume(cond);
}

void goto_symext::symex_assert()
{
  if(cur_state->guard.is_false())
    return;

  // Don't convert if it's an user provided assertion and we're running in
  // no assertion mode or forward condition
  if(cur_state->source.pc->location.user_provided() && no_assertions)
    return;

  std::string msg = cur_state->source.pc->location.comment().as_string();
  if(msg == "")
    msg = "assertion";

  const goto_programt::instructiont &instruction = *cur_state->source.pc;

  expr2tc tmp = instruction.guard;
  replace_nondet(tmp);

  dereference(tmp, dereferencet::READ);
  replace_dynamic_allocation(tmp);

  claim(tmp, msg);
}

void goto_symext::run_intrinsic(
  const code_function_call2t &func_call,
  reachability_treet &art,
  const std::string &symname)
{
  if(symname == "c:@F@__ESBMC_yield")
  {
    intrinsic_yield(art);
  }
  else if(symname == "c:@F@__ESBMC_switch_to")
  {
    intrinsic_switch_to(func_call, art);
  }
  else if(symname == "c:@F@__ESBMC_switch_away_from")
  {
    intrinsic_switch_from(art);
  }
  else if(has_prefix(symname, "c:@F@__ESBMC_get_thread_id"))
  {
    intrinsic_get_thread_id(func_call, art);
  }
  else if(symname == "c:@F@__ESBMC_set_thread_internal_data")
  {
    intrinsic_set_thread_data(func_call, art);
  }
  else if(symname == "c:@F@__ESBMC_get_thread_internal_data")
  {
    intrinsic_get_thread_data(func_call, art);
  }
  else if(symname == "c:@F@__ESBMC_spawn_thread")
  {
    intrinsic_spawn_thread(func_call, art);
  }
  else if(symname == "c:@F@__ESBMC_terminate_thread")
  {
    intrinsic_terminate_thread(art);
  }
  else if(symname == "c:@F@__ESBMC_get_thread_state")
  {
    intrinsic_get_thread_state(func_call, art);
  }
  else if(symname == "c:@F@__ESBMC_really_atomic_begin")
  {
    intrinsic_really_atomic_begin(art);
  }
  else if(symname == "c:@F@__ESBMC_really_atomic_end")
  {
    intrinsic_really_atomic_end(art);
  }
  else if(symname == "c:@F@__ESBMC_switch_to_monitor")
  {
    intrinsic_switch_to_monitor(art);
  }
  else if(symname == "c:@F@__ESBMC_switch_from_monitor")
  {
    intrinsic_switch_from_monitor(art);
  }
  else if(symname == "c:@F@__ESBMC_register_monitor")
  {
    intrinsic_register_monitor(func_call, art);
  }
  else if(symname == "c:@F@__ESBMC_kill_monitor")
  {
    intrinsic_kill_monitor(art);
  }
  else if(symname == "c:@F@__ESBMC_memset")
  {
    intrinsic_memset(art, func_call);
  }
  else if(symname == "c:@F@__ESBMC_get_object_size")
  {
    intrinsic_get_object_size(func_call, art);
  }
  else if(has_prefix(symname, "c:@F@__ESBMC_overflow"))
  {
    bool is_mult = has_prefix(symname, "c:@F@__ESBMC_overflow_smul") ||
                   has_prefix(symname, "c:@F@__ESBMC_overflow_umul");
    bool is_add = has_prefix(symname, "c:@F@__ESBMC_overflow_sadd") ||
                  has_prefix(symname, "c:@F@__ESBMC_overflow_uadd");
    bool is_sub = has_prefix(symname, "c:@F@__ESBMC_overflow_ssub") ||
                  has_prefix(symname, "c:@F@__ESBMC_overflow_usub");

    assert(func_call.operands.size() == 3);

    const auto &func_type = to_code_type(func_call.function->type);
    assert(func_type.arguments[0] == func_type.arguments[1]);
    assert(is_pointer_type(func_type.arguments[2]));

    expr2tc op;
    if(is_mult)
      op = mul2tc(
        func_type.arguments[0], func_call.operands[0], func_call.operands[1]);
    else if(is_add)
      op = add2tc(
        func_type.arguments[0], func_call.operands[0], func_call.operands[1]);
    else if(is_sub)
      op = sub2tc(
        func_type.arguments[0], func_call.operands[0], func_call.operands[1]);
    else
    {
      assert(0 && "Unknown overflow intrinsics");
    }

    // Assign result of the two arguments to the dereferenced third argument
    symex_assign(code_assign2tc(
      dereference2tc(
        to_pointer_type(func_call.operands[2]->type).subtype,
        func_call.operands[2]),
      op));

    // Perform overflow check and assign it to the return object
    symex_assign(code_assign2tc(func_call.ret, overflow2tc(op)));
  }
  else if(has_prefix(symname, "c:@F@__ESBMC_convertvector"))
  {
    assert(
      func_call.operands.size() == 1 &&
      "Wrong __ESBMC_convertvector signature");
    auto &ex_state = art.get_cur_state();
    if(ex_state.cur_state->guard.is_false())
      return;

    auto t = func_call.ret->type;
    assert(t->type_id == type2t::type_ids::vector_id);
    auto subtype = to_vector_type(t).subtype;

    // v should be a vector
    expr2tc v = func_call.operands[0];
    ex_state.get_active_state().level2.rename(v);
    assert(v->expr_id == expr2t::expr_ids::constant_vector_id);

    // Create new vector
    std::vector<expr2tc> members;
    for(const auto &x : to_constant_vector2t(v).datatype_members)
    {
      // Create a typecast call
      auto typecast = typecast2tc(subtype, x);
      members.push_back(typecast);
    }
    expr2tc result =
      constant_vector2tc(func_call.ret->type, std::move(members));
    expr2tc ret_ref = func_call.ret;
    dereference(ret_ref, dereferencet::READ);
    symex_assign(code_assign2tc(ret_ref, result), false, cur_state->guard);
  }
  else if(has_prefix(symname, "c:@F@__ESBMC_shufflevector"))
  {
    assert(
      func_call.operands.size() >= 2 &&
      "Wrong __ESBMC_shufflevector signature");
    auto &ex_state = art.get_cur_state();
    if(ex_state.cur_state->guard.is_false())
      return;

    expr2tc v1 = func_call.operands[0];
    expr2tc v2 = func_call.operands[1];
    ex_state.get_active_state().level2.rename(v1);
    ex_state.get_active_state().level2.rename(v2);

    // V1 and V2 should have the same type
    assert(
      v1->type == v2->type &&
      v1->expr_id == expr2t::expr_ids::constant_vector_id);
    auto v1_size = (long int)to_constant_vector2t(v1).datatype_members.size();

    std::vector<expr2tc> members;
    for(long unsigned int i = 2; i < func_call.operands.size(); i++)
    {
      expr2tc e = func_call.operands[i];
      ex_state.get_active_state().level2.rename(e);

      auto index = to_constant_int2t(e).value.to_int64();
      if(index == -1)
      {
        // TODO: nondet_value
        members.push_back(to_constant_vector2t(v1).datatype_members[0]);
      }
      auto vec =
        index < v1_size ? to_constant_vector2t(v1) : to_constant_vector2t(v2);
      index = index % v1_size;
      members.push_back(vec.datatype_members[index]);
    }
    expr2tc result =
      constant_vector2tc(func_call.ret->type, std::move(members));
    expr2tc ret_ref = func_call.ret;
    dereference(ret_ref, dereferencet::READ);
    symex_assign(code_assign2tc(ret_ref, result), false, cur_state->guard);
  }
  else if(has_prefix(symname, "c:@F@__ESBMC_atomic_load"))
  {
    assert(
      func_call.operands.size() == 3 && "Wrong __ESBMC_atomic_load signature");
    auto &ex_state = art.get_cur_state();
    if(ex_state.cur_state->guard.is_false())
      return;

    expr2tc ptr = func_call.operands[0];
    expr2tc ret = func_call.operands[1];

    symex_assign(code_assign2tc(
      dereference2tc(to_pointer_type(ret->type).subtype, ret),
      dereference2tc(to_pointer_type(ptr->type).subtype, ptr)));
  }
  else if(has_prefix(symname, "c:@F@__ESBMC_atomic_store"))
  {
    assert(
      func_call.operands.size() == 3 && "Wrong __ESBMC_atomic_store signature");
    auto &ex_state = art.get_cur_state();
    if(ex_state.cur_state->guard.is_false())
      return;

    expr2tc ptr = func_call.operands[0];
    expr2tc ret = func_call.operands[1];

    symex_assign(code_assign2tc(
      dereference2tc(to_pointer_type(ptr->type).subtype, ptr),
      dereference2tc(to_pointer_type(ret->type).subtype, ret)));
  }
  else if(has_prefix(symname, "c:@F@__ESBMC_is_little_endian"))
  {
    expr2tc is_little_endian =
      (config.ansi_c.endianess == configt::ansi_ct::IS_LITTLE_ENDIAN)
        ? gen_true_expr()
        : gen_false_expr();
    symex_assign(code_assign2tc(func_call.ret, is_little_endian));
  }
  else if(symname == "c:@F@__ESBMC_no_abnormal_memory_leak")
  {
    expr2tc no_abnormal_memleak =
      config.options.get_bool_option("no-abnormal-memory-leak")
        ? gen_true_expr()
        : gen_false_expr();
    symex_assign(code_assign2tc(func_call.ret, no_abnormal_memleak));
  }
  else if(symname == "c:@F@__ESBMC_builtin_constant_p")
  {
    assert(
      func_call.operands.size() == 1 &&
      "Wrong __ESBMC_builtin_constant_p signature");
    auto &ex_state = art.get_cur_state();
    if(ex_state.cur_state->guard.is_false())
      return;

    expr2tc op1 = func_call.operands[0];
    cur_state->rename(op1);
    symex_assign(code_assign2tc(
      func_call.ret,
      is_constant_int2t(op1) ? gen_one(int_type2()) : gen_zero(int_type2())));
  }
  else if(has_prefix(symname, "c:@F@__ESBMC_sync_fetch_and_add"))
  {
    // Already modelled in builtin_libs
    return;
  }
  else if(
    has_prefix(symname, "c:@F@__ESBMC_scanf") ||
    has_prefix(symname, "c:@F@__ESBMC_fscanf"))
  {
    auto &ex_state = art.get_cur_state();
    if(ex_state.cur_state->guard.is_false())
      return;

    symex_input(func_call);
    return;
  }
  else if(has_prefix(symname, "c:@F@__ESBMC_init_object"))
  {
    assert(
      func_call.operands.size() == 1 && "Wrong __ESBMC_init_object signature");
    auto &ex_state = art.get_cur_state();
    if(ex_state.cur_state->guard.is_false())
      return;

    // Get the argument
    expr2tc arg0 = func_call.operands[0];
    internal_deref_items.clear();
    expr2tc deref = dereference2tc(get_empty_type(), arg0);
    dereference(deref, dereferencet::INTERNAL);

    for(const auto &item : internal_deref_items)
    {
      assert(
        is_symbol2t(item.object) &&
        "__ESBMC_init_object only works for variables");

      // Get the length of the type. This will propagate an exception for dynamic/infinite
      // sized arrays (as expected)
      try
      {
        type_byte_size(item.object->type).to_int64();
      }
      catch(array_type2t::dyn_sized_array_excp *e)
      {
        log_error("__ESBMC_init_object does not support VLAs");
        abort();
      }
      catch(array_type2t::inf_sized_array_excp *e)
      {
        log_error(
          "__ESBMC_init_object does not support infinite-length arrays");
        abort();
      }
      expr2tc val = sideeffect2tc(
        item.object->type,
        expr2tc(),
        expr2tc(),
        std::vector<expr2tc>(),
        type2tc(),
        sideeffect2t::nondet);

      symex_assign(code_assign2tc(item.object, val), false, cur_state->guard);
    }

    return;
  }
  else if(has_prefix(symname, "c:@F@__ESBMC_memory_leak_checks"))
  {
    add_memory_leak_checks();
  }
  else
  {
    log_error(
      "Function call to non-intrinsic prefixed with __ESBMC (fatal)\n"
      "The name in question: {}\n"
      "(NB: the C spec reserves the __ prefix for the compiler and "
      "environment)",
      symname);
    abort();
  }
}

void goto_symext::add_memory_leak_checks()
{
  if(!memory_leak_check)
    return;

  std::function<expr2tc(expr2tc)> maybe_global_target;
  if(no_reachable_memleak)
  {
    /* We've been instructed to exclude any allocated dynamic object from the
     * memory-leak check that is still reachable via global symbols.
     *
     * So the idea is to go through all global symbols in the context and
     * check where they point to via the value-set of the respective symbol.
     * This forms the set of targets reachable from global symbols. However,
     * reachable is a transitive relation, so we'll build a fixpoint by then
     * adding those symbols that are reachable in one step from the already
     * known globally-reachable ones. It's basically a breadth-first search.
     *
     * The targets (actually, their addresses) are collected in
     * 'globals_point_to'.
     *
     * The 'globals' list contains the new "frontier" of symbols left to check
     * for their value-sets. Once it's empty, the fixpoint is reached.
     */
    std::unordered_set<expr2tc, irep2_hash> globals_point_to;
    bool has_unknown = false;
    value_set_analysist va(ns);

    std::list<value_sett::entryt> globals;
    va.get_globals(globals);
    for(int i=0; !has_unknown && !globals.empty(); i++) {
      /* Collect all objects reachable from 'globals' in 'points_to'. */
      value_sett::object_mapt points_to;
      for(const value_sett::entryt &e : globals)
      {
        /* Unfortunately, we just have the symbol id and a suffix that's only
         * meaningful to the value-set analysis, but no type. However, we
         * need a type. So reconstruct the current state's version of a
         * symbol-expr referring to this symbol. */
        symbol_exprt sym_expr(e.identifier);
        expr2tc sym_expr2;
        migrate_expr(sym_expr, sym_expr2);

        /* Now obtain the type. */
        const symbolt *sym = ns.lookup(to_symbol2t(sym_expr2).thename);

        /* By "global" only user-defined symbols are meant. Internally used ones
         * we can ignore. */
        if(e.identifier == "argv'" || has_prefix(sym->name, "__ESBMC_"))
          continue;
        fprintf(stderr, "memcleanup: itr %d, obtaining value-set for global "
                        "'%s' suffix '%s'\n",
                i, e.identifier.c_str(), e.suffix.c_str());
        sym_expr2->type = migrate_type(sym->type);

        /* Rename so that it reflects the current state. */
        cur_state->rename(sym_expr2);

        /* Collect its value-set into 'points_to'. Since that's a map, this
         * will only add targets that are not already in there. */
        cur_state->value_set
          .get_value_set_rec(sym_expr2, points_to, e.suffix, sym_expr2->type);
      }

      /* Now add the new found symbols to 'globals_point_to' and also record
       * them in 'globals'. If they were known already, we don't need to handle
       * them again. */
      globals.clear();
      for(auto it = points_to.begin(); it != points_to.end(); ++it)
      {
        expr2tc target = cur_state->value_set.to_expr(it);
        /* A value-set entry can be unknown, invalid or a descriptor of an
         * object. */
        if(is_unknown2t(target))
        {
          /* Treating 'unknown' as "could potentially point anywhere" generates
           * too many false positives. It will basically make the memory-leak
           * check useless since all dynamic objects could potentially still
           * be referenced. We ignore it for now and pretend that's OK because
           * dereference() with INTERNAL mode would also do that. */
          continue;
          has_unknown = true;
          globals.clear();
          break;
        }
        /* invalid targets are not objects, ignore those */
        if(is_invalid2t(target))
          continue;

        assert(is_object_descriptor2t(target));
        expr2tc root_object = to_object_descriptor2t(target).get_root_object();

        /* null-objects and constant strings are interesting for neither the
         * memory-leak check nor for finding more pointers to enlarge the set
         * of reachable objects */
        if(is_null_object2t(root_object) || is_constant_string2t(root_object))
          continue;

        /* Record and, if new, obtain all the "entries" interesting for the
         * value-set analysis. An entry is interesting basically if its type
         * contains a pointer type. Those are also exactly the ones interesting
         * for the building the set of reachable objects. */
        expr2tc adr = address_of2tc(root_object->type, root_object);
        auto [itr,ins] = globals_point_to.emplace(adr);
        if(!ins)
          continue;

        /* Check the contents of a valid root object of this target for more
         * pointers reaching out further */
        assert(is_symbol2t(root_object));
        va.get_entries_rec(
          to_symbol2t(root_object).get_symbol_name(),
          "",
          migrate_type_back(root_object->type),
          globals);
      }
    }

    fprintf(stderr, "memcleanup: unknown: %d, globals point to:\n", has_unknown);
    for (const expr2tc &e : globals_point_to)
      fprintf(stderr, "memcleanup:  %s\n",
              to_symbol2t(to_address_of2t(e).ptr_obj).get_symbol_name().c_str());

    if(has_unknown)
      maybe_global_target = [](expr2tc) { return gen_true_expr(); };
    else
      maybe_global_target = [tgts = std::move(globals_point_to)](expr2tc obj) {
        expr2tc is_any;
        for(const expr2tc &e : tgts)
        {
          expr2tc same = same_object2tc(obj, e);
          is_any = is_any ? or2tc(is_any, same) : same;
        }
        return is_any ? is_any : gen_false_expr();
      };
  }

  for(auto const &it : dynamic_memory)
  {
    // Don't check memory leak if the object is automatically deallocated
    if(it.auto_deallocd)
      continue;

    // Assert that the allocated object was freed.
    expr2tc deallocd = deallocated_obj2tc(it.obj);

    // For each dynamic object we generate a condition checking
    // whether it has been deallocated.
    expr2tc eq = equality2tc(deallocd, gen_true_expr());

    expr2tc when = it.alloc_guard.as_expr();

    if(no_reachable_memleak)
    {
      expr2tc obj = get_base_object(it.obj);
      expr2tc adr = address_of2tc(obj->type, obj);
      expr2tc targeted = maybe_global_target(adr);
      when = and2tc(when, not2tc(targeted));
    }

    // Additionally, we need to make sure that we check the above condition
    // only for dynamic objects that were created from successful
    // memory allocations. This is because we always create a dynamic object for
    // each dynamic allocation, and the allocation success status
    // is described by a separate "allocation_guard".
    // (see "symex_mem" method in "goto-symex/builtin_functions.cpp").
    expr2tc cond = implies2tc(when, eq);

    replace_dynamic_allocation(cond);
    cur_state->rename(cond);
    claim(
      cond,
      "dereference failure: forgotten memory: " + get_pretty_name(it.name));
  }
}
