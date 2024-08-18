#include <cassert>
#include <complex>
#include <functional>
#include <goto-symex/execution_state.h>
#include <goto-symex/goto_symex.h>
#include <goto-symex/reachability_tree.h>
#include <goto-symex/printf_formatter.h>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <util/arith_tools.h>
#include <util/base_type.h>
#include <util/c_types.h>
#include <util/cprover_prefix.h>
#include <util/expr_util.h>
#include <util/i2string.h>
#include <irep2/irep2.h>
#include <util/message.h>
#include <util/message/format.h>
#include <util/migrate.h>
#include <util/prefix.h>
#include <util/std_types.h>
#include <vector>
#include <algorithm>
#include <util/array2string.h>

expr2tc goto_symext::symex_malloc(const expr2tc &lhs, const sideeffect2t &code)
{
  return symex_mem(true, lhs, code);
}

expr2tc goto_symext::symex_alloca(const expr2tc &lhs, const sideeffect2t &code)
{
  return symex_mem(false, lhs, code);
}

void goto_symext::symex_realloc(const expr2tc &lhs, const sideeffect2t &code)
{
  expr2tc src_ptr = code.operand;
  expr2tc realloc_size = code.size;

  internal_deref_items.clear();
  expr2tc deref = dereference2tc(get_empty_type(), src_ptr);
  dereference(deref, dereferencet::INTERNAL);
  // src_ptr is now invalidated.

  // Free the given pointer. This just uses the pointer object from the pointer
  // variable that's the argument to realloc. It also leads to pointer validity
  // checking, and checks that the offset is zero.
  expr2tc fr = code_free2tc(code.operand);
  symex_free(fr);

  // We now have a list of things to work on. Recurse into them, build a result,
  // and then switch between those results afterwards.
  // Result list is the address of the reallocated piece of data, and the guard.
  std::list<std::pair<expr2tc, expr2tc>> result_list;
  for (auto &item : internal_deref_items)
  {
    expr2tc guard = item.guard;
    cur_state->rename_address(item.object);
    cur_state->guard.guard_expr(guard);
    target->renumber(guard, item.object, realloc_size, cur_state->source);
    type2tc new_ptr = pointer_type2tc(item.object->type);
    expr2tc addrof = address_of2tc(new_ptr, item.object);
    result_list.emplace_back(addrof, item.guard);

    // Bump the realloc-numbering of the object. This ensures that, after
    // renaming, the address_of we just generated compares differently to
    // previous address_of's before the realloc.
    unsigned int cur_num = 0;
    if (
      cur_state->realloc_map.find(item.object) != cur_state->realloc_map.end())
    {
      cur_num = cur_state->realloc_map[item.object];
    }

    cur_num++;
    std::map<expr2tc, unsigned>::value_type v(item.object, cur_num);
    cur_state->realloc_map.insert(v);
  }

  // Rebuild a gigantic if-then-else chain from the result list.
  expr2tc result;
  if (result_list.size() == 0)
  {
    // Nothing happened; there was nothing, or only null, to point at.
    // In this case, just return right now and leave the pointer free. The
    // symex_free that occurred above should trigger a dereference failure.
    return;
  }

  for (auto const &it : result_list)
  {
    if (is_nil_expr(result))
      result = it.first;
    else
      result = if2tc(result->type, it.second, it.first, result);
  }

  // Install pointer modelling data into the relevant arrays.
  expr2tc ptr_obj = pointer_object2tc(pointer_type2(), result);
  track_new_pointer(ptr_obj, type2tc(), realloc_size);

  symex_assign(code_assign2tc(lhs, result), true);
}

expr2tc goto_symext::symex_mem(
  const bool is_malloc,
  const expr2tc &lhs,
  const sideeffect2t &code)
{
  if (is_nil_expr(lhs))
    return expr2tc(); // ignore

  // size
  type2tc type = code.alloctype;
  expr2tc size = code.size;
  bool size_is_one = false;

  if (is_nil_type(type))
    type = char_type2();

  if (is_nil_expr(size))
    size_is_one = true;
  else
  {
    cur_state->rename(size);
    BigInt i;
    if (is_constant_int2t(size))
    {
      uint64_t v = to_constant_int2t(size).value.to_uint64();
      if (v == 1)
        size_is_one = true;
      else if (v == 0 && options.get_bool_option("malloc-zero-is-null"))
        return symbol2tc(pointer_type2tc(type), "NULL");
    }
  }

  unsigned int &dynamic_counter = get_dynamic_counter();
  dynamic_counter++;

  // value
  symbolt symbol;

  symbol.name = "dynamic_" + i2string(dynamic_counter) +
                (size_is_one ? "_value" : "_array");

  symbol.id = std::string("symex_dynamic::") + (!is_malloc ? "alloca::" : "") +
              id2string(symbol.name);
  symbol.lvalue = true;

  typet renamedtype = ns.follow(migrate_type_back(type));
  if (size_is_one)
    symbol.type = renamedtype;
  else
  {
    symbol.type = typet(typet::t_array);
    symbol.type.subtype() = renamedtype;
    symbol.type.size(migrate_expr_back(size));
  }

  symbol.type.dynamic(true);

  symbol.type.set(
    "alignment", constant_exprt(config.ansi_c.max_alignment(), size_type()));

  symbol.mode = "C";

  new_context.add(symbol);

  type2tc new_type = migrate_type(symbol.type);

  type2tc rhs_type;
  expr2tc rhs_ptr_obj;

  if (size_is_one)
  {
    rhs_type = migrate_type(symbol.type);
    rhs_ptr_obj = symbol2tc(new_type, symbol.id);
  }
  else
  {
    type2tc subtype = migrate_type(symbol.type.subtype());
    expr2tc sym = symbol2tc(new_type, symbol.id);
    expr2tc idx_val = gen_long(size->type, 0L);
    expr2tc idx = index2tc(subtype, sym, idx_val);
    rhs_type = migrate_type(symbol.type.subtype());
    rhs_ptr_obj = idx;
  }

  expr2tc rhs_addrof = address_of2tc(rhs_type, rhs_ptr_obj);

  expr2tc rhs = rhs_addrof;
  expr2tc ptr_rhs = rhs;
  guardt alloc_guard = cur_state->guard;

  if (options.get_bool_option("malloc-zero-is-null"))
  {
    expr2tc null_sym = symbol2tc(rhs->type, "NULL");
    expr2tc choice = greaterthan2tc(size, gen_long(size->type, 0));
    alloc_guard.add(choice);
    rhs = if2tc(rhs->type, choice, rhs, null_sym);
  }

  if (!options.get_bool_option("force-malloc-success") && is_malloc)
  {
    expr2tc null_sym = symbol2tc(rhs->type, "NULL");
    expr2tc choice = sideeffect2tc(
      get_bool_type(),
      expr2tc(),
      expr2tc(),
      std::vector<expr2tc>(),
      type2tc(),
      sideeffect2t::nondet);
    replace_nondet(choice);

    rhs = if2tc(rhs->type, choice, rhs, null_sym);
    alloc_guard.add(choice);

    ptr_rhs = rhs;
  }

  if (rhs->type != lhs->type)
    rhs = typecast2tc(lhs->type, rhs);

  cur_state->rename(rhs);
  expr2tc rhs_copy(rhs);

  symex_assign(code_assign2tc(lhs, rhs), true);

  expr2tc ptr_obj = pointer_object2tc(pointer_type2(), ptr_rhs);
  track_new_pointer(ptr_obj, new_type);

  dynamic_memory.emplace_back(
    rhs_copy, alloc_guard, !is_malloc, symbol.name.as_string());

  return to_address_of2t(rhs_addrof).ptr_obj;
}

void goto_symext::track_new_pointer(
  const expr2tc &ptr_obj,
  const type2tc &new_type,
  const expr2tc &size)
{
  // Also update all the accounting data.

  // Mark that object as being dynamic, in the __ESBMC_is_dynamic array
  type2tc sym_type = array_type2tc(get_bool_type(), expr2tc(), true);
  expr2tc sym = symbol2tc(sym_type, dyn_info_arr_name);

  expr2tc idx = index2tc(get_bool_type(), sym, ptr_obj);
  expr2tc truth = gen_true_expr();
  symex_assign(code_assign2tc(idx, truth), true);

  expr2tc valid_sym = symbol2tc(sym_type, valid_ptr_arr_name);
  expr2tc valid_index_expr = index2tc(get_bool_type(), valid_sym, ptr_obj);
  truth = gen_true_expr();
  symex_assign(code_assign2tc(valid_index_expr, truth), true);

  type2tc sz_sym_type = array_type2tc(size_type2(), expr2tc(), true);
  expr2tc sz_sym = symbol2tc(sz_sym_type, alloc_size_arr_name);
  expr2tc sz_index_expr = index2tc(size_type2(), sz_sym, ptr_obj);

  expr2tc object_size_exp =
    is_nil_expr(size) ? type_byte_size_expr(new_type) : size;

  symex_assign(code_assign2tc(sz_index_expr, object_size_exp), true);
}

void goto_symext::symex_free(const expr2tc &expr)
{
  const auto &code = static_cast<const code_expression_data &>(*expr);

  // Trigger 'free'-mode dereference of this pointer. Should generate various
  // dereference failure callbacks.
  expr2tc tmp = code.operand;
  dereference(tmp, dereferencet::FREE);

  // Don't rely on the output of dereference in free mode; instead fetch all
  // the internal dereference state for pointed at objects, and creates claims
  // that if pointed at, their offset is zero.
  internal_deref_items.clear();
  tmp = code.operand;

  // Create temporary, dummy, dereference
  tmp = dereference2tc(get_uint8_type(), tmp);
  dereference(tmp, dereferencet::INTERNAL);

  // Only add assertions to check pointer offset if pointer check is enabled
  if (!options.get_bool_option("no-pointer-check"))
  {
    // Get all dynamic objects allocated using alloca
    std::vector<allocated_obj> allocad;
    for (auto const &item : dynamic_memory)
      if (item.auto_deallocd)
        allocad.push_back(item);

    for (auto const &item : internal_deref_items)
    {
      guardt g = cur_state->guard;
      g.add(item.guard);

      // Check if the offset of the object being freed is zero
      expr2tc offset = item.offset;
      expr2tc eq = equality2tc(offset, gen_ulong(0));
      g.guard_expr(eq);
      claim(eq, "Operand of free must have zero pointer offset");

      // Check if we are not freeing an dynamic object allocated using alloca
      for (auto const &a : allocad)
      {
        expr2tc alloc_obj = get_base_object(a.obj);
        while (is_if2t(alloc_obj))
        {
          const if2t &the_if = to_if2t(alloc_obj);
          assert(is_symbol2t(the_if.false_value));
          assert(to_symbol2t(the_if.false_value).thename == "NULL");
          alloc_obj = get_base_object(the_if.true_value);
        }
        assert(is_symbol2t(alloc_obj));
        const irep_idt &id_alloc_obj = to_symbol2t(alloc_obj).thename;
        const irep_idt &id_item_obj = to_symbol2t(item.object).thename;
        // Check if the object allocated with alloca is the same
        // as given in the free function
        if (id_alloc_obj == id_item_obj)
        {
          expr2tc noteq = notequal2tc(alloc_obj, item.object);
          g.guard_expr(noteq);
          claim(noteq, "dereference failure: invalid pointer freed");
        }
      }
    }
  }

  // Clear the alloc bit.
  type2tc sym_type = array_type2tc(get_bool_type(), expr2tc(), true);
  expr2tc ptr_obj = pointer_object2tc(pointer_type2(), code.operand);
  dereference(ptr_obj, dereferencet::READ);

  expr2tc valid_sym = symbol2tc(sym_type, valid_ptr_arr_name);
  expr2tc valid_index_expr = index2tc(get_bool_type(), valid_sym, ptr_obj);
  expr2tc falsity = gen_false_expr();
  symex_assign(code_assign2tc(valid_index_expr, falsity), true);
}

void goto_symext::symex_printf(const expr2tc &lhs, expr2tc &rhs)
{
  assert(is_code_printf2t(rhs));

  expr2tc renamed_rhs = rhs;
  cur_state->rename(renamed_rhs);

  code_printf2t &new_rhs = to_code_printf2t(renamed_rhs);

  if (new_rhs.bs_name.empty())
  {
    log_error("No base_name for code_printf2t");
    return;
  }

  const std::string &base_name = new_rhs.bs_name;

  // get the format string base on the bs_name
  irep_idt fmt;
  size_t idx;
  if (base_name == "printf")
  {
    // 1. printf: 1st argument
    assert(new_rhs.operands.size() >= 1 && "Wrong printf signature");
    const expr2tc &base_expr = get_base_object(new_rhs.operands[0]);
    if (is_constant_string2t(base_expr))
    {
      fmt = to_constant_string2t(base_expr).value;
      idx = 1;
    }
    else
    {
      // e.g.
      // int x = 1;
      // printf(x); // output ""
      fmt = "";
      idx = 0;
    }
  }
  else if (
    base_name == "fprintf" || base_name == "dprintf" ||
    base_name == "sprintf" || base_name == "vfprintf")
  {
    // 2.fprintf, sprintf, dprintf: 2nd argument
    assert(
      new_rhs.operands.size() >= 2 &&
      "Wrong fprintf/sprintf/dprintf/vfprintf signature");
    const expr2tc &base_expr = get_base_object(new_rhs.operands[1]);
    if (is_constant_string2t(base_expr))
    {
      fmt = to_constant_string2t(base_expr).value;
      idx = 2;
    }
    else
    {
      fmt = "";
      idx = 1;
    }
  }
  else if (base_name == "snprintf")
  {
    // 3. snprintf: 3rd argument
    assert(new_rhs.operands.size() >= 3 && "Wrong snprintf signature");
    const expr2tc &base_expr = get_base_object(new_rhs.operands[2]);
    if (is_constant_string2t(base_expr))
    {
      fmt = to_constant_string2t(base_expr).value;
      idx = 3;
    }
    else
    {
      fmt = "";
      idx = 2;
    }
  }
  else
    abort();

  // Now we pop the format
  for (size_t i = 0; i < idx; i++)
    new_rhs.operands.erase(new_rhs.operands.begin());

  std::list<expr2tc> args;
  new_rhs.foreach_operand([this, &args](const expr2tc &e) {
    expr2tc tmp = e;
    do_simplify(tmp);
    args.push_back(tmp);
  });

  if (!is_nil_expr(lhs))
  {
    // get the return value from code_printf2tc
    // 1. covert code_printf2tc back to sideeffect2tc
    exprt rhs_expr = migrate_expr_back(rhs);
    exprt printf_code("sideeffect", migrate_type_back(lhs->type));

    printf_code.statement("printf2");
    printf_code.operands() = rhs_expr.operands();
    printf_code.location() = rhs_expr.location();

    migrate_expr(printf_code, rhs);

    // 2 check if it is a char array. if so, convert it to a string
    // this is due to printf_formatter does not handle the char array.
    for (auto &arg : args)
    {
      const expr2tc &base_expr = get_base_object(arg);
      if (!is_constant_string2t(base_expr) && is_array_type(base_expr))
      {
        // the current expression "arg" does not hold the value info (might be a bug)
        // thus we need to look it up from the symbol table
        assert(is_symbol2t(base_expr));
        const symbolt &s = *ns.lookup(to_symbol2t(base_expr).thename);
        exprt dest;
        if (array2string(s, dest))
          continue;
        migrate_expr(dest, arg);
      }
    }

    // 3 get the number of characters output (return value)
    printf_formattert printf_formatter;
    printf_formatter(fmt.as_string(), args);
    size_t outlen = printf_formatter.as_string().length();

    // 4. do assign
    symex_assign(
      code_assign2tc(lhs, constant_int2tc(int_type2(), BigInt(outlen))));
  }

  target->output(
    cur_state->guard.as_expr(), cur_state->source, fmt.as_string(), args);
}

void goto_symext::symex_input(const code_function_call2t &func_call)
{
  assert(is_symbol2t(func_call.function));

  unsigned number_of_format_args, fmt_idx;
  const irep_idt func_name = to_symbol2t(func_call.function).thename;

  if (func_name == "c:@F@scanf")
  {
    assert(func_call.operands.size() >= 2 && "Wrong scanf signature");
    fmt_idx = 0;
    number_of_format_args = func_call.operands.size() - 1;
  }
  else if (func_name == "c:@F@fscanf" || func_name == "c:@F@sscanf")
  {
    assert(func_call.operands.size() >= 3 && "Wrong fscanf/sscanf signature");
    fmt_idx = 1;
    number_of_format_args = func_call.operands.size() - 2;
  }
  else
    abort();

  cur_state->source.pc--;

  if (func_call.ret)
    symex_assign(code_assign2tc(
      func_call.ret,
      constant_int2tc(int_type2(), BigInt(number_of_format_args))));

  // TODO: fill / cut off the inputs stream based on the length limits.

  for (long unsigned int i = fmt_idx + 1; i <= number_of_format_args + fmt_idx;
       i++)
  {
    expr2tc operand = func_call.operands[i];
    internal_deref_items.clear();
    expr2tc deref = dereference2tc(get_empty_type(), operand);
    dereference(deref, dereferencet::INTERNAL);

    for (const auto &item : internal_deref_items)
    {
      assert(is_symbol2t(item.object) && "This only works for variables");

      auto type = item.object->type;
      expr2tc val = sideeffect2tc(
        type,
        expr2tc(),
        expr2tc(),
        std::vector<expr2tc>(),
        type2tc(),
        sideeffect2t::nondet);

      symex_assign(code_assign2tc(item.object, val), false, cur_state->guard);
    }
  }

  cur_state->source.pc++;
}

void goto_symext::symex_cpp_new(const expr2tc &lhs, const sideeffect2t &code)
{
  expr2tc size = code.size;

  bool do_array = (code.kind == sideeffect2t::cpp_new_arr);

  unsigned int &dynamic_counter = get_dynamic_counter();
  dynamic_counter++;

  const std::string count_string(i2string(dynamic_counter));

  // value
  symbolt symbol;
  symbol.name = do_array ? "dynamic_" + count_string + "_array"
                         : "dynamic_" + count_string + "_value";
  symbol.id = "symex_dynamic::" + id2string(symbol.name);
  symbol.lvalue = true;
  symbol.mode = "C++";

  const pointer_type2t &ptr_ref = to_pointer_type(code.type);
  type2tc renamedtype2 =
    migrate_type(ns.follow(migrate_type_back(ptr_ref.subtype)));

  type2tc newtype = do_array
                      ? type2tc(array_type2tc(renamedtype2, code.size, false))
                      : renamedtype2;

  symbol.type = migrate_type_back(newtype);

  symbol.type.dynamic(true);

  new_context.add(symbol);

  // make symbol expression
  expr2tc rhs_ptr_obj;
  if (do_array)
  {
    expr2tc sym = symbol2tc(newtype, symbol.id);
    expr2tc idx = index2tc(renamedtype2, sym, gen_ulong(0));
    rhs_ptr_obj = idx;
  }
  else
    rhs_ptr_obj = symbol2tc(newtype, symbol.id);

  expr2tc rhs = address_of2tc(renamedtype2, rhs_ptr_obj);

  cur_state->rename(rhs);
  expr2tc rhs_copy(rhs);
  expr2tc ptr_rhs(rhs);

  symex_assign(code_assign2tc(lhs, rhs), true);

  expr2tc ptr_obj = pointer_object2tc(pointer_type2(), ptr_rhs);
  track_new_pointer(ptr_obj, newtype, size);

  dynamic_memory.emplace_back(
    rhs_copy, cur_state->guard, false, symbol.name.as_string());
}

void goto_symext::symex_cpp_delete(const expr2tc &expr)
{
  const auto &code = static_cast<const code_expression_data &>(*expr);

  expr2tc tmp = code.operand;

  internal_deref_items.clear();
  expr2tc deref = dereference2tc(get_empty_type(), tmp);
  dereference(deref, dereferencet::INTERNAL);

  // we need to check the memory deallocation operator:
  // new and delete, new[] and delete[]
  bool is_arr = is_array_type(internal_deref_items.front().object->type);
  bool is_del_arr = is_code_cpp_del_array2t(expr);

  if (is_arr != is_del_arr)
  {
    const std::string &msg =
      "Mismatched memory deallocation operators: " + get_expr_id(expr);
    claim(gen_false_expr(), msg);
  }

  // implement delete as a call to free
  symex_free(expr);
}

void goto_symext::intrinsic_yield(reachability_treet &art)
{
  // Don't context switch if the guard is false.
  if (!cur_state->guard.is_false())
    art.get_cur_state().force_cswitch();
}

void goto_symext::intrinsic_switch_to(
  const code_function_call2t &call,
  reachability_treet &art)
{
  // Switch to other thread.
  const expr2tc &num = call.operands[0];
  if (!is_constant_int2t(num))
  {
    log_error("Can't switch to non-constant thread id no\n{}", *num);
    abort();
  }

  const constant_int2t &thread_num = to_constant_int2t(num);

  unsigned int tid = thread_num.value.to_uint64();
  if (tid != art.get_cur_state().get_active_state_number())
    art.get_cur_state().switch_to_thread(tid);
}

void goto_symext::intrinsic_switch_from(reachability_treet &art)
{
  // Mark switching back to this thread as already having been explored
  art.get_cur_state()
    .DFS_traversed[art.get_cur_state().get_active_state_number()] = true;

  // And force a context switch.
  art.get_cur_state().force_cswitch();
}

void goto_symext::intrinsic_get_thread_id(
  const code_function_call2t &call,
  reachability_treet &art)
{
  statet &state = art.get_cur_state().get_active_state();

  unsigned int thread_id = art.get_cur_state().get_active_state_number();
  expr2tc tid = constant_int2tc(call.ret->type, BigInt(thread_id));

  state.value_set.assign(call.ret, tid);

  symex_assign(code_assign2tc(call.ret, tid), true);
}

void goto_symext::intrinsic_set_thread_data(
  const code_function_call2t &call,
  reachability_treet &art)
{
  statet &state = art.get_cur_state().get_active_state();
  expr2tc threadid = call.operands[0];
  expr2tc startdata = call.operands[1];

  state.rename(threadid);
  state.rename(startdata);

  while (is_typecast2t(threadid))
    threadid = to_typecast2t(threadid).from;

  if (!is_constant_int2t(threadid))
  {
    log_error("__ESBMC_set_start_data received nonconstant thread id");
    abort();
  }
  unsigned int tid = to_constant_int2t(threadid).value.to_uint64();
  art.get_cur_state().set_thread_start_data(tid, startdata);
}

void goto_symext::intrinsic_get_thread_data(
  const code_function_call2t &call,
  reachability_treet &art)
{
  statet &state = art.get_cur_state().get_active_state();
  expr2tc threadid = call.operands[0];

  state.level2.rename(threadid);

  while (is_typecast2t(threadid))
    threadid = to_typecast2t(threadid).from;

  if (!is_constant_int2t(threadid))
  {
    log_error("__ESBMC_get_start_data received nonconstant thread id");
    abort();
  }

  unsigned int tid = to_constant_int2t(threadid).value.to_uint64();
  const expr2tc &startdata = art.get_cur_state().get_thread_start_data(tid);

  assert(base_type_eq(call.ret->type, startdata->type, ns));

  state.value_set.assign(call.ret, startdata);
  symex_assign(code_assign2tc(call.ret, startdata), true);
}

void goto_symext::intrinsic_spawn_thread(
  const code_function_call2t &call,
  reachability_treet &art)
{
  if (
    (k_induction || inductive_step) &&
    !options.get_bool_option("disable-inductive-step"))
  {
    log_warning(
      "k-induction does not support concurrency yet. Disabling inductive step");

    // Disable inductive step on multi threaded code
    options.set_option("disable-inductive-step", true);
  }

  // As an argument, we expect the address of a symbol.
  expr2tc addr = call.operands[0];
  simplify(addr); /* simplification is not needed for clang-11, but clang-13
                   * inserts a no-op typecast here. */
  assert(is_address_of2t(addr));
  const address_of2t &addrof = to_address_of2t(addr);
  assert(is_symbol2t(addrof.ptr_obj));
  const irep_idt &symname = to_symbol2t(addrof.ptr_obj).thename;

  goto_functionst::function_mapt::const_iterator it =
    art.goto_functions.function_map.find(symname);
  if (it == art.goto_functions.function_map.end())
  {
    log_error("Spawning thread \"{}\": symbol not found", symname);
    abort();
  }

  if (!it->second.body_available)
  {
    log_error("Spawning thread \"{}\": no body", symname);
    abort();
  }

  const goto_programt &prog = it->second.body;

  // Invalidates current state reference!
  unsigned int thread_id = art.get_cur_state().add_thread(&prog);

  statet &state = art.get_cur_state().get_active_state();

  expr2tc thread_id_exp = constant_int2tc(call.ret->type, BigInt(thread_id));

  state.value_set.assign(call.ret, thread_id_exp);

  symex_assign(code_assign2tc(call.ret, thread_id_exp), true);

  // Force a context switch point. If the caller is in an atomic block, it'll be
  // blocked, but a context switch will be forced when we exit the atomic block.
  // Otherwise, this will cause the required context switch.
  art.get_cur_state().force_cswitch();
}

void goto_symext::intrinsic_terminate_thread(reachability_treet &art)
{
  art.get_cur_state().end_thread();
  // No need to force a context switch; an ended thread will cause the run to
  // end and the switcher to be invoked.
}

void goto_symext::intrinsic_get_thread_state(
  const code_function_call2t &call,
  reachability_treet &art)
{
  statet &state = art.get_cur_state().get_active_state();
  expr2tc threadid = call.operands[0];
  state.level2.rename(threadid);

  while (is_typecast2t(threadid))
    threadid = to_typecast2t(threadid).from;

  if (!is_constant_int2t(threadid))
  {
    log_error("__ESBMC_get_thread_state received nonconstant thread id");
    abort();
  }

  unsigned int tid = to_constant_int2t(threadid).value.to_uint64();
  // Possibly we should handle this error; but meh.
  assert(art.get_cur_state().threads_state.size() >= tid);

  // Thread state is simply whether the thread is ended or not.
  unsigned int flags =
    (art.get_cur_state().threads_state[tid].thread_ended) ? 1 : 0;

  // Reuse threadid
  expr2tc flag_expr =
    constant_int2tc(get_uint_type(config.ansi_c.int_width), flags);
  symex_assign(code_assign2tc(call.ret, flag_expr), true);
}

void goto_symext::intrinsic_really_atomic_begin(reachability_treet &art)
{
  art.get_cur_state().increment_active_atomic_number();
}

void goto_symext::intrinsic_really_atomic_end(reachability_treet &art)
{
  art.get_cur_state().decrement_active_atomic_number();
}

void goto_symext::intrinsic_switch_to_monitor(reachability_treet &art)
{
  execution_statet &ex_state = art.get_cur_state();

  // Don't do this if we're in the initialization function.
  if (cur_state->source.pc->function == "__ESBMC_main")
    return;

  ex_state.switch_to_monitor();
}

void goto_symext::intrinsic_switch_from_monitor(reachability_treet &art)
{
  execution_statet &ex_state = art.get_cur_state();
  ex_state.switch_away_from_monitor();
}

void goto_symext::intrinsic_register_monitor(
  const code_function_call2t &call,
  reachability_treet &art)
{
  execution_statet &ex_state = art.get_cur_state();

  if (ex_state.tid_is_set)
    assert(
      0 && "Monitor thread ID was already set (__ESBMC_register_monitor)\n");

  statet &state = art.get_cur_state().get_active_state();
  expr2tc threadid = call.operands[0];
  state.level2.rename(threadid);

  while (is_typecast2t(threadid))
    threadid = to_typecast2t(threadid).from;

  if (!is_constant_int2t(threadid))
  {
    log_error("__ESBMC_register_monitor received nonconstant thread id");
    abort();
  }

  unsigned int tid = to_constant_int2t(threadid).value.to_uint64();
  assert(art.get_cur_state().threads_state.size() >= tid);
  ex_state.monitor_tid = tid;
  ex_state.tid_is_set = true;
}

void goto_symext::intrinsic_kill_monitor(reachability_treet &art)
{
  execution_statet &ex_state = art.get_cur_state();
  ex_state.kill_monitor_thread();
}

void goto_symext::symex_va_arg(const expr2tc &lhs, const sideeffect2t &code)
{
  // Get symbol
  expr2tc symbol = code.operand;
  assert(is_symbol2t(symbol));

  // to allow constant propagation
  cur_state->rename(symbol);
  do_simplify(symbol);

  expr2tc next_symbol = symbol;
  if (is_typecast2t(next_symbol))
    next_symbol = to_typecast2t(symbol).from;

  if (is_address_of2t(next_symbol))
    next_symbol = to_address_of2t(next_symbol).ptr_obj;

  assert(is_symbol2t(next_symbol));
  irep_idt id = to_symbol2t(next_symbol).thename;
  std::string base =
    id2string(cur_state->top().function_identifier) + "::va_arg";

  id = base + std::to_string(cur_state->top().va_index++);

  expr2tc va_rhs;

  const symbolt *s = new_context.find_symbol(id);
  if (s != nullptr)
  {
    type2tc symbol_type = migrate_type(s->type);

    va_rhs = symbol2tc(symbol_type, s->id);
    cur_state->top().level1.get_ident_name(va_rhs);

    va_rhs = address_of2tc(symbol_type, va_rhs);
    va_rhs = typecast2tc(lhs->type, va_rhs);
  }
  else
  {
    va_rhs = gen_zero(lhs->type);
  }

  symex_assign(code_assign2tc(lhs, va_rhs), true);
}

// Computes the equivalent object value when considering a memset operation on it
expr2tc gen_byte_expression_byte_update(
  const type2tc &type,
  const expr2tc &src,
  const expr2tc &value,
  const size_t num_of_bytes,
  const size_t offset)
{
  // Sadly, our simplifier can not typecast from value operations
  // safely. We can however :)
  auto new_src = src;
  auto new_type = type;

  auto found_constant = false;
  auto optimized = src->simplify();
  if (optimized)
  {
    found_constant = is_typecast2t(optimized) &&
                     is_constant_int2t(to_typecast2t(optimized).from);
    if (found_constant)
    {
      new_src = to_typecast2t(optimized).from;
      new_type = get_int64_type();
    }
  }

  expr2tc result = new_src;
  auto value_downcast = typecast2tc(get_uint8_type(), value);

  expr2tc off = constant_int2tc(get_int32_type(), BigInt(offset));
  for (size_t counter = 0; counter < num_of_bytes; counter++)
  {
    expr2tc increment = constant_int2tc(get_int32_type(), BigInt(counter));
    result = byte_update2tc(
      new_type,
      result,
      add2tc(off->type, off, increment),
      value_downcast,
      false);
  }

  if (found_constant)
    result = typecast2tc(type, result);

  simplify(result);

  return result;
}

// Computes the equivalent object value when considering a memset operation on it
static inline expr2tc gen_byte_expression(
  const type2tc &type,
  const expr2tc &src,
  const expr2tc &value,
  const size_t num_of_bytes,
  const size_t offset)
{
  /**
   * The idea of this expression is to compute the object value
   * in the case where every byte `value` was set up until num_of_bytes
   *
   * Note: this function assumes that all memory checks have been done!
   *
   * In summary, there are two main computations here:
   *
   * A. Generate the byte representation, this is mostly through
   *    the `result` expression. The expression is initialized with zero
   *    and then, until the num_of_bytes is reached it will do a full byte
   *    left-shift followed by an bitor operation with the byte value:
   *
   *    For example, for a integer(4 bytes) with memset using 3 bytes and value 0xF1
   *
   *    step 1: 0x00000000 -- left-shift 8 -- 0x00000000 -- bitor -- 0x000000F1
   *    step 2: 0x000000F1 -- left-shift 8 -- 0x0000F100 -- bitor -- 0x0000F1F1
   *    step 3: 0x0000F1F1 -- left-shift 8 -- 0x00F1F100 -- bitor -- 0x00F1F1F1
   *
   *    Since we only want 3 bytes, the initialized object value would be 0x00F1F1F1
   *
   * B. Generate a mask of the bits that were not set, this is done because skipped bits
   *    need to be returned back. The computation of this is simple, we initialize every
   *    bit that was changed by the byte-representation computation with a 1, which is then
   *    negated to be applied with an bitand in the original value:
   *
   *    Back to the example in A, we had the byte-representation of  0x00F1F1F1. If the
   *    original value was 0xA2A2A2A2, then we would have the following mask:
   *
   *    step 1: 0x00000000 -- set-bits -- 0x000000FF
   *    step 2: 0x000000FF -- set-bits -- 0x0000FFFF
   *    step 3: 0x0000FFFF -- set-bits -- 0x00FFFFFF
   *
   *   So, 0x00FFFFFF is the mask for all bits changed. We can negate it to: 0xFF000000
   *
   *   Then, we can apply it to the original source value with bitand
   *
   *   0xA2A2A2A2 AND 0xFF000000 --> 0xA2000000
   *
   * Finally, we get the result from A and B and unify them through a bitor
   *
   *  0xA2000000 OR 0x00F1F1F1 --> 0xA2F1F1F1
   *
   * Note about offsets: To handle them, we apply left shifts to the remaining offset after
   * the computation of the object-value and initial mask representation
   *
   */

  if (is_pointer_type(type))
    return gen_byte_expression_byte_update(
      type, src, value, num_of_bytes, offset);

  expr2tc result = gen_zero(type);
  auto value_downcast = typecast2tc(get_uint8_type(), value);
  auto value_upcast = typecast2tc(
    type,
    value_downcast); // so smt_conv won't complain about the width of the type

  expr2tc mask = gen_zero(type);

  const auto eight = constant_int2tc(type, BigInt(8));
  const auto one = constant_int2tc(type, BigInt(1));
  for (unsigned i = 0; i < num_of_bytes; i++)
  {
    result = shl2tc(type, result, eight);
    result = bitor2tc(type, result, value_upcast);

    for (int m = 0; m < 8; m++)
    {
      mask = shl2tc(type, mask, one);
      mask = bitor2tc(type, mask, one);
    }
  }

  // Do the rest of the offset!
  for (unsigned i = 0; i < offset; i++)
  {
    result = shl2tc(type, result, eight);
    mask = shl2tc(type, mask, eight);
  }

  mask = bitnot2tc(type, mask);
  mask = bitand2tc(type, src, mask);
  result = bitor2tc(type, result, mask);

  simplify(result);
  return result;
}

static inline expr2tc gen_value_by_byte(
  const type2tc &type,
  const expr2tc &src,
  const expr2tc &value,
  const size_t num_of_bytes,
  const size_t offset)
{
  /**
   * @brief Construct a new object, initializing it with the memset equivalent
   *
   * There are a few corner cases here:
   *
   * 1 - Primitives: these are simple: just generate the byte_expression directly
   * 2 - Arrays: these are ok: just keep generating byte_expression for each member
   *        until a limit has arrived. Dynamic memory is dealt here.
   * 3 - Structs/Union: these are the hardest as we have to take the alignment into
   *        account when dealing with it. Hopefully the clang-frontend already give it
   *        to us.
   *
   */

  if (num_of_bytes == 0)
    return src;

  /* TODO: Bitwise operations are valid for floats, but we don't have an
   * implementation, yet. Give up. */
  if (is_floatbv_type(type) || is_fixedbv_type(type))
  {
    unsigned int type_size = type_byte_size(type).to_uint64();
    // HACK: this should fix the NN-benchmarks (see #1508)
    if (
      is_constant_int2t(value) && to_constant_int2t(value).value.is_zero() &&
      num_of_bytes == type_size && offset == 0)
      return gen_zero(type);
    return expr2tc();
  }

  if (is_scalar_type(type) && type->get_width() == 8 && offset == 0)
    return typecast2tc(type, value);

  if (is_array_type(type))
  {
    /*
     * Very straighforward, get the total number_of_bytes and keep subtracting until
     * the end
     */

    expr2tc result = gen_zero(type);
    constant_array2t &data = to_constant_array2t(result);

    uint64_t base_size =
      type_byte_size(to_array_type(type).subtype).to_uint64();
    uint64_t bytes_left = num_of_bytes;
    uint64_t offset_left = offset;

    for (unsigned i = 0; i < data.datatype_members.size(); i++)
    {
      BigInt position(i);
      expr2tc local_member = index2tc(
        to_array_type(type).subtype,
        src,
        constant_int2tc(get_uint32_type(), position));
      // Skip offsets
      if (offset_left >= base_size)
      {
        data.datatype_members[i] = local_member;
        offset_left -= base_size;
      }
      else
      {
        uint64_t bytes_to_write =
          bytes_left < base_size ? bytes_left : base_size;
        data.datatype_members[i] = gen_value_by_byte(
          to_array_type(type).subtype,
          local_member,
          value,
          bytes_to_write,
          offset_left);
        if (!data.datatype_members[i])
          return expr2tc();
        bytes_left =
          bytes_left <= base_size ? 0 : bytes_left - (base_size - offset_left);
        offset_left = 0;
      }
    }

    return result;
  }

  if (is_struct_type(type))
  {
    /** Similar to array, however get the size of
     * each component
     */
    expr2tc result = gen_zero(type);
    constant_struct2t &data = to_constant_struct2t(result);
    uint64_t bytes_left = num_of_bytes;
    uint64_t offset_left = offset;

    for (unsigned i = 0; i < data.datatype_members.size(); i++)
    {
      irep_idt name = to_struct_type(type).member_names[i];
      // TODO: We need a better way to detect bitfields
      if (has_prefix(name.as_string(), "bit_field_pad$"))
        return expr2tc();
      expr2tc local_member =
        member2tc(to_struct_type(type).members[i], src, name);

      // Since it is a symbol, lets start from the old value
      if (is_pointer_type(to_struct_type(type).members[i]))
        data.datatype_members[i] = local_member;

      type2tc current_member_type = data.datatype_members[i]->type;

      uint64_t current_member_size =
        type_byte_size(current_member_type).to_uint64();

      // Skip offsets
      if (offset_left >= current_member_size)
      {
        data.datatype_members[i] = local_member;
        offset_left -= current_member_size;
      }
      else
      {
        assert(offset_left < current_member_size);
        uint64_t bytes_to_write = std::min(bytes_left, current_member_size);
        data.datatype_members[i] = gen_value_by_byte(
          current_member_type,
          local_member,
          value,
          bytes_to_write,
          offset_left);

        if (!data.datatype_members[i])
          return expr2tc();

        bytes_left = bytes_left < current_member_size
                       ? 0
                       : bytes_left - (current_member_size - offset_left);
        offset_left = 0;
      }
    }
    return result;
  }

  if (is_union_type(type))
  {
    /**
     * Unions are not nice, let's go through every member
     * and get the biggest one! And then use it directly
     *
     * @warning there is a semantic difference on this when
     * compared to c:@F@__memset_impl. While this function
     * will yield the same result as `clang` would, ESBMC
     * will handle the dereference (in the __memset_impl)
     * using the first member, which can lead to overflows.
     * See GitHub Issue #639
     *
     */
    expr2tc result = gen_zero(type);
    constant_union2t &data = to_constant_union2t(result);

    uint64_t union_total_size = type_byte_size(type).to_uint64();
    // Let's find a member with the biggest size
    size_t n = to_union_type(type).members.size();
    size_t selected_member_index = n;

    for (size_t i = 0; i < n; i++)
    {
      if (
        type_byte_size(to_union_type(type).members[i]).to_uint64() ==
        union_total_size)
      {
        selected_member_index = i;
        break;
      }
    }

    assert(selected_member_index < n);

    const irep_idt &name =
      to_union_type(type).member_names[selected_member_index];
    const type2tc &member_type =
      to_union_type(type).members[selected_member_index];
    expr2tc member = member2tc(member_type, src, name);

    data.init_field = name;
    data.datatype_members[0] =
      gen_value_by_byte(member_type, member, value, num_of_bytes, offset);
    return data.datatype_members[0] ? result : expr2tc();
  }

  // Found a primitive! Just apply the function
  return gen_byte_expression(type, src, value, num_of_bytes, offset);
}

/**
 * @brief This function will try to initialize the object pointed by
 * the address in a smarter way, minimizing the number of assignments.
 * This is intend to optimize the behaviour of a memset operation:
 *
 * memset(void* ptr, int value, size_t num_of_bytes)
 *
 * - ptr can point to anything. We have to add checks!
 * - value is interpreted as a uchar.
 * - num_of_bytes must be known. If it is nondet, we will bump the call
 *
 * In plain C, the objective of a call such as:
 *
 * int a;
 * memset(&a, value, num)
 *
 * Would generate something as:
 *
 * int temp = 0;
 * for(int i = 0; i < num; i++) temp = byte | (temp << 8);
 * a = temp;
 *
 * This is just a simplification for understanding though. During the
 * instrumentation size checks will be added, and also, the original
 * bytes from `a` that were not overwritten must be mantained!
 * Arrays will need to be added up to an nth element.
 *
 * In ESBMC though, we have 2 main methods of dealing with memory objects:
 *
 * A. Heap objects, which are valid/invalid. They are the easiest to deal
 *    with, as the dereference will actually return a big array of char to us.
 *    For this case, we can just overwrite the members directly with the value
 *
 * B. Stack objects, which are typed. It will be hard, this will require operations
 *    which depends on the base type and also on padding.
 */
void goto_symext::intrinsic_memset(
  reachability_treet &art,
  const code_function_call2t &func_call)
{
  // 1. Check for the functions parameters and do the deref and processing!

  assert(func_call.operands.size() == 3 && "Wrong memset signature");
  const execution_statet &ex_state = art.get_cur_state();
  if (ex_state.cur_state->guard.is_false())
    return;

  /* Get the arguments
   * arg0: ptr to object
   * arg1: int for the new byte value
   * arg2: number of bytes to be set */
  expr2tc arg0 = func_call.operands[0];
  expr2tc arg1 = func_call.operands[1];
  expr2tc arg2 = func_call.operands[2];

  // Checks where arg0 points to
  internal_deref_items.clear();
  expr2tc deref = dereference2tc(get_empty_type(), arg0);
  dereference(deref, dereferencet::INTERNAL);

  /* Preconditions for the optimization:
   * A: It should point to someplace
   * B: byte itself should be renamed properly
   * C: Number of bytes cannot be symbolic
   * D: This is a simplification. So don't run with --no-simplify */
  cur_state->rename(arg1);
  cur_state->rename(arg2);
  if (
    !internal_deref_items.size() || !arg1 || !arg2 || is_symbol2t(arg2) ||
    options.get_bool_option("no-simplify"))
  {
    /* Not sure what to do here, let's rely
       * on the default implementation then */
    log_debug("memset", "Couldn't optimize memset due to precondition");
    bump_call(func_call, "c:@F@__memset_impl");
    return;
  }

  simplify(arg2);
  if (!is_constant_int2t(arg2))
  {
    log_debug("memset", "TODO: simplifier issues :/");
    bump_call(func_call, "c:@F@__memset_impl");
    return;
  }

  unsigned long number_of_bytes = to_constant_int2t(arg2).as_ulong();

  // Where are we pointing to?
  for (auto &item : internal_deref_items)
  {
    guardt guard = ex_state.cur_state->guard;
    expr2tc item_object = item.object;
    expr2tc item_offset = item.offset;
    guard.add(item.guard);

    cur_state->rename(item_object);
    cur_state->rename(item_offset);

    /* Pre-requisites locally:
       * item_object must be something!
       * item_offset must be something! */
    if (!item_object || !item_offset)
    {
      log_debug("memset", "Couldn't get item_object/item_offset");
      bump_call(func_call, "c:@F@__memset_impl");
      return;
    }

    simplify(item_offset);
    // We can't optimize symbolic offsets :/
    if (is_symbol2t(item_offset))
    {
      log_debug(
        "memset",
        "Item offset is symbolic: {}",
        to_symbol2t(item_offset).get_symbol_name());
      bump_call(func_call, "c:@F@__memset_impl");
      return;
    }

    /* TODO: Shouldn't the simplifier be able to solve pointer arithmethic
     *  when it multiplies and divides for the same value?
     */
    if (is_div2t(item_offset))
    {
      auto as_div = to_div2t(item_offset);
      if (is_mul2t(as_div.side_1) && is_constant_int2t(as_div.side_2))
      {
        auto as_mul = to_mul2t(as_div.side_1);
        if (
          is_constant_int2t(as_mul.side_2) &&
          (to_constant_int2t(as_mul.side_2).as_ulong() ==
           to_constant_int2t(as_div.side_2).as_ulong()))
        {
          // if side_1 of mult is a pointer_offset, then it is just zero
          if (is_pointer_offset2t(as_mul.side_1))
          {
            log_debug("memset", "TODO: some simplifications are missing");
            item_offset = constant_int2tc(get_uint64_type(), BigInt(0));
          }
        }
      }
    }

    if (!is_constant_int2t(item_offset))
    {
      /* If we reached here, item_offset is not symbolic
       * and we don't know what the actual value of it is...
       *
       * For now bump_call, later we should expand our simplifier
       */
      log_debug(
        "memset", "TODO: some simplifications are missing, bumping call");
      bump_call(func_call, "c:@F@__memset_impl");
      return;
    }

    uint64_t number_of_offset =
      to_constant_int2t(item_offset).value.to_uint64();

    /* This fails for VLAs or dynamically allocated arrays.
     * XXX: We could consider not failing and encoding the is_out_bounds
     *      condition below symbolically instead. */
    uint64_t type_size;
    try
    {
      type_size = type_byte_size(item_object->type).to_uint64();
    }
    catch (const array_type2t::dyn_sized_array_excp &)
    {
      bump_call(func_call, "c:@F@__memset_impl");
      return;
    }

    if (is_code_type(item_object->type))
    {
      std::string error_msg =
        fmt::format("dereference failure: trying to deref a ptr code");

      // SAME_OBJECT(ptr, item) => DEREF ERROR
      expr2tc check = implies2tc(item.guard, gen_false_expr());
      claim(check, error_msg);
      continue;
    }

    bool is_out_bounds = ((type_size - number_of_offset) < number_of_bytes) ||
                         (number_of_offset > type_size);
    if (
      is_out_bounds && !options.get_bool_option("no-pointer-check") &&
      !options.get_bool_option("no-bounds-check"))
    {
      std::string error_msg = fmt::format(
        "dereference failure: memset of memory segment of size {} with {} "
        "bytes",
        type_size - number_of_offset,
        number_of_bytes);

      // SAME_OBJECT(ptr, item) => DEREF ERROR
      expr2tc check = implies2tc(item.guard, gen_false_expr());
      claim(check, error_msg);
      continue;
    }

    expr2tc new_object = gen_value_by_byte(
      item_object->type, item_object, arg1, number_of_bytes, number_of_offset);

    // Were we able to optimize it? If not... bump call
    if (!new_object)
    {
      log_debug("memset", "gen_value_by_byte failed");
      bump_call(func_call, "c:@F@__memset_impl");
      return;
    }
    // 4. Assign the new object
    symex_assign(code_assign2tc(item.object, new_object), false, guard);
  }
  // Lastly, let's add a NULL ptr check
  if (!options.get_bool_option("no-pointer-check"))
  {
    expr2tc null_sym = symbol2tc(arg0->type, "NULL");
    expr2tc obj = same_object2tc(arg0, null_sym);
    expr2tc null_check = not2tc(same_object2tc(arg0, null_sym));
    ex_state.cur_state->guard.guard_expr(null_check);
    claim(null_check, " dereference failure: NULL pointer");
  }

  expr2tc ret_ref = func_call.ret;
  dereference(ret_ref, dereferencet::READ);
  symex_assign(code_assign2tc(ret_ref, arg0), false, cur_state->guard);
}

void goto_symext::intrinsic_get_object_size(
  const code_function_call2t &func_call,
  reachability_treet &)
{
  assert(func_call.operands.size() == 1 && "Wrong get_object_size signature");
  expr2tc ptr = func_call.operands[0];

  // Work out what the ptr points at.
  internal_deref_items.clear();
  expr2tc deref = dereference2tc(get_empty_type(), ptr);
  dereference(deref, dereferencet::INTERNAL);

  assert(is_array_type(internal_deref_items.front().object->type));
  expr2tc obj_size =
    to_array_type(internal_deref_items.front().object->type).array_size;

  expr2tc ret_ref = func_call.ret;
  dereference(ret_ref, dereferencet::READ);
  symex_assign(
    code_assign2tc(ret_ref, typecast2tc(ret_ref->type, obj_size)),
    false,
    cur_state->guard);
}

void goto_symext::intrinsic_races_check_dereference(expr2tc &expr)
{
  if (!options.get_bool_option("data-races-check"))
    return;

  exprt tmp_exprt = migrate_expr_back(expr);
  std::string iden;

  // There are two kinds of instructions generated in a data races check:
  // assignment and assertion (_ESBMC_deref_a = false; assert !_ESBMC_deref_a)

  if (!tmp_exprt.is_not())
    iden = tmp_exprt.is_index() ? id2string(tmp_exprt.op0().identifier())
                                : id2string(tmp_exprt.identifier());
  else
  {
    tmp_exprt = tmp_exprt.op0();
    iden = tmp_exprt.is_index() ? id2string(tmp_exprt.op0().identifier())
                                : id2string(tmp_exprt.identifier());
  }

  // Only instructions with special prefixes are processed
  // _ESBMC_deref_a = "_ESBMC_deref_" + "a"
  if (has_prefix(iden, "__ESBMC_deref"))
  {
    const irep_idt identifier = iden.substr(14);

    const symbolt *symbol = new_context.find_symbol(identifier);

    if (!symbol)
      return;

    exprt deref("dereference");
    deref.type() = symbol_expr(*symbol).type().subtype();
    deref.copy_to_operands(symbol_expr(*symbol));

    expr2tc tmp_deref;
    migrate_expr(deref, tmp_deref);
    dereference(tmp_deref, dereferencet::READ);
    deref = migrate_expr_back(tmp_deref);

    const irep_idt new_idt = deref.is_symbol()
                               ? "tmp_" + id2string(deref.identifier())
                               : "tmp_" + id2string(deref.op0().identifier());

    if (new_idt == "tmp_")
      return;

    const symbolt *s = new_context.find_symbol(new_idt);

    symbolt new_symbol;

    if (s)
      new_symbol = *s;
    else
    {
      type2tc index = array_type2tc(get_bool_type(), expr2tc(), true);

      new_symbol.id = new_idt;
      new_symbol.name = new_idt;
      new_symbol.type =
        tmp_exprt.is_index() ? migrate_type_back(index) : typet("bool");
      new_symbol.static_lifetime = true;
      new_symbol.value.make_false();

      new_context.add(new_symbol);
    }

    deref = symbol_expr(new_symbol);

    if (tmp_exprt.is_index())
    {
      index_exprt index(
        symbol_expr(new_symbol),
        to_index_expr(tmp_exprt).index(),
        typet("bool"));
      deref.swap(index);
    }

    if (expr->expr_id != expr2t::not_id)
      migrate_expr(deref, expr);
    else
      migrate_expr(gen_not(deref), expr);
  }
}

void goto_symext::bump_call(
  const code_function_call2t &func_call,
  const std::string &symname)
{
  // We're going to execute a function call, and that's going to mess with
  // the program counter. Set it back *onto* pointing at this intrinsic, so
  // symex_function_call calculates the right return address. Misery.
  cur_state->source.pc--;

  expr2tc newcall = func_call.clone();
  code_function_call2t &mutable_funccall = to_code_function_call2t(newcall);
  mutable_funccall.function = symbol2tc(get_empty_type(), symname);
  // Execute call
  symex_function_call(newcall);
  return;
}

// Copied from https://stackoverflow.com/questions/874134/find-out-if-string-ends-with-another-string-in-c
static inline bool
ends_with(std::string const &value, std::string const &ending)
{
  if (ending.size() > value.size())
    return false;
  return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

bool goto_symext::run_builtin(
  const code_function_call2t &func_call,
  const std::string &symname)
{
  if (
    has_prefix(symname, "c:@F@__builtin_sadd") ||
    has_prefix(symname, "c:@F@__builtin_uadd") ||
    has_prefix(symname, "c:@F@__builtin_ssub") ||
    has_prefix(symname, "c:@F@__builtin_usub") ||
    has_prefix(symname, "c:@F@__builtin_smul") ||
    has_prefix(symname, "c:@F@__builtin_umul"))
  {
    assert(ends_with(symname, "_overflow"));
    assert(func_call.operands.size() == 3);

    const auto &func_type = to_code_type(func_call.function->type);
    assert(func_type.arguments[0] == func_type.arguments[1]);
    assert(is_pointer_type(func_type.arguments[2]));

    bool is_mult = has_prefix(symname, "c:@F@__builtin_smul") ||
                   has_prefix(symname, "c:@F@__builtin_umul");
    bool is_add = has_prefix(symname, "c:@F@__builtin_sadd") ||
                  has_prefix(symname, "c:@F@__builtin_uadd");
    bool is_sub = has_prefix(symname, "c:@F@__builtin_ssub") ||
                  has_prefix(symname, "c:@F@__builtin_usub");

    expr2tc op;
    if (is_mult)
      op = mul2tc(
        func_type.arguments[0], func_call.operands[0], func_call.operands[1]);
    else if (is_add)
      op = add2tc(
        func_type.arguments[0], func_call.operands[0], func_call.operands[1]);
    else if (is_sub)
      op = sub2tc(
        func_type.arguments[0], func_call.operands[0], func_call.operands[1]);
    else
    {
      log_error("Unknown overflow intrinsics");
      abort();
    }

    // Assign result of the two arguments to the dereferenced third argument
    symex_assign(code_assign2tc(
      dereference2tc(
        to_pointer_type(func_call.operands[2]->type).subtype,
        func_call.operands[2]),
      op));

    // Perform overflow check and assign it to the return object
    symex_assign(code_assign2tc(func_call.ret, overflow2tc(op)));

    return true;
  }

  if (has_prefix(symname, "c:@F@__builtin_constant_p"))
  {
    expr2tc op1 = func_call.operands[0];
    cur_state->rename(op1);
    symex_assign(code_assign2tc(
      func_call.ret,
      is_constant_int2t(op1) ? gen_one(int_type2()) : gen_zero(int_type2())));
    return true;
  }

  return false;
}
