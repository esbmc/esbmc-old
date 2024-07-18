
#include <ac_config.h>

#include <cassert>
#include <goto-programs/goto_convert_class.h>
#include <regex>
#include <util/arith_tools.h>
#include <util/c_types.h>
#include <util/cprover_prefix.h>
#include <util/expr_util.h>
#include <util/i2string.h>
#include <util/location.h>
#include <util/message.h>
#include <util/message/format.h>
#include <util/prefix.h>
#include <util/simplify_expr.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/string_constant.h>
#include <util/type_byte_size.h>

static const std::string &get_string_constant(const exprt &expr)
{
  if (expr.id() == "typecast" && expr.operands().size() == 1)
    return get_string_constant(expr.op0());

  if (
    !expr.is_address_of() || expr.operands().size() != 1 ||
    !expr.op0().is_index() || expr.op0().operands().size() != 2)
  {
    log_error("expected string constant, but got:\n{}", expr);
    abort();
  }

  const exprt &string = expr.op0().op0();
  irep_idt v = string.value();
  if (string.id() == "string-constant")
    try
    {
      v = to_string_constant(string).mb_value();
    }
    catch (const string_constantt::mb_conversion_error &e)
    {
      log_warning("{}", e.what());
    }

  return v.as_string();
}

static void get_alloc_type_rec(const exprt &src, typet &type, exprt &size)
{
  static bool is_mul = false;

  const irept &sizeof_type = src.c_sizeof_type();
  //nec: ex33.c
  if (!sizeof_type.is_nil() && !is_mul)
  {
    type = (typet &)sizeof_type;
  }
  else if (src.id() == "*")
  {
    is_mul = true;
    forall_operands (it, src)
      get_alloc_type_rec(*it, type, size);
  }
  else
  {
    size.copy_to_operands(src);
  }
}

static void get_alloc_type(const exprt &src, typet &type, exprt &size)
{
  type.make_nil();
  size.make_nil();

  get_alloc_type_rec(src, type, size);

  if (type.is_nil())
    type = char_type();

  if (size.has_operands())
  {
    if (size.operands().size() == 1)
    {
      exprt tmp;
      tmp.swap(size.op0());
      size.swap(tmp);
    }
    else
    {
      size.id("*");
      size.type() = size.op0().type();
    }
  }
}

void goto_convertt::do_printf(
  const exprt &lhs,
  const exprt &function,
  const exprt::operandst &arguments,
  goto_programt &dest,
  const std::string &bs_name)
{
  exprt printf_code(
    "sideeffect", static_cast<const typet &>(function.type().return_type()));

  printf_code.statement("printf");

  printf_code.operands() = arguments;
  printf_code.location() = function.location();
  printf_code.base_name(bs_name);

  if (lhs.is_not_nil())
  {
    code_assignt assignment(lhs, printf_code);
    assignment.location() = function.location();
    copy(assignment, ASSIGN, dest);
  }
  else
  {
    printf_code.id("code");
    printf_code.type() = typet("code");
    copy(to_code(printf_code), OTHER, dest);
  }
}

void goto_convertt::do_atomic_begin(
  const exprt &lhs,
  const exprt &function,
  const exprt::operandst &arguments,
  goto_programt &dest)
{
  if (lhs.is_not_nil())
  {
    log_error("atomic_begin does not expect an LHS");
    abort();
  }

  if (arguments.size() != 0)
  {
    log_error("atomic_begin takes zero argument");
    abort();
  }

  // We should allow a context switch to happen before synchronization points.
  // In particular, here we force a context switch to happen before an atomic block
  // via the intrinsic function __ESBMC_yield();
  code_function_callt call;
  call.function() = symbol_expr(*context.find_symbol("c:@F@__ESBMC_yield"));
  do_function_call(call.lhs(), call.function(), call.arguments(), dest);

  goto_programt::targett t = dest.add_instruction(ATOMIC_BEGIN);
  t->location = function.location();
}

void goto_convertt::do_atomic_end(
  const exprt &lhs,
  const exprt &function,
  const exprt::operandst &arguments,
  goto_programt &dest)
{
  if (lhs.is_not_nil())
  {
    log_error("atomic_end does not expect an LHS");
    abort();
  }

  if (!arguments.empty())
  {
    log_error("atomic_end takes no arguments");
    abort();
  }

  goto_programt::targett t = dest.add_instruction(ATOMIC_END);
  t->location = function.location();
}

void goto_convertt::do_mem(
  bool is_malloc,
  const exprt &lhs,
  const exprt &function,
  const exprt::operandst &arguments,
  goto_programt &dest)
{
  std::string func = is_malloc ? "malloc" : "alloca";

  if (lhs.is_nil())
    return; // does nothing

  locationt location = function.location();

  // get alloc type and size
  typet alloc_type;
  exprt alloc_size;

  get_alloc_type(arguments[0], alloc_type, alloc_size);

  if (alloc_size.is_nil())
    alloc_size = from_integer(1, size_type());

  if (alloc_type.is_nil())
    alloc_type = char_type();

  if (alloc_type.id() == "symbol")
    alloc_type = ns.follow(alloc_type);

  if (alloc_size.type() != size_type())
  {
    alloc_size.make_typecast(size_type());
    simplify(alloc_size);
  }

  // produce new object

  exprt new_expr("sideeffect", lhs.type());
  new_expr.statement(func);
  new_expr.copy_to_operands(arguments[0]);
  new_expr.cmt_size(alloc_size);
  new_expr.cmt_type(alloc_type);
  new_expr.location() = location;

  goto_programt::targett t_n = dest.add_instruction(ASSIGN);

  exprt new_assign = code_assignt(lhs, new_expr);
  expr2tc new_assign_expr;
  migrate_expr(new_assign, new_assign_expr);
  t_n->code = new_assign_expr;
  t_n->location = location;
}

void goto_convertt::do_alloca(
  const exprt &lhs,
  const exprt &function,
  const exprt::operandst &arguments,
  goto_programt &dest)
{
  do_mem(false, lhs, function, arguments, dest);
}

void goto_convertt::do_malloc(
  const exprt &lhs,
  const exprt &function,
  const exprt::operandst &arguments,
  goto_programt &dest)
{
  do_mem(true, lhs, function, arguments, dest);
}

void goto_convertt::do_realloc(
  const exprt &lhs,
  const exprt &function,
  const exprt::operandst &arguments,
  goto_programt &dest)
{
  // produce new object

  exprt new_expr("sideeffect", lhs.type());
  new_expr.statement("realloc");
  new_expr.copy_to_operands(arguments[0]);
  new_expr.cmt_size(arguments[1]);
  new_expr.location() = function.location();

  goto_programt::targett t_n = dest.add_instruction(ASSIGN);

  exprt new_assign = code_assignt(lhs, new_expr);
  expr2tc new_assign_expr;
  migrate_expr(new_assign, new_assign_expr);
  t_n->code = new_assign_expr;
  t_n->location = function.location();
}

void goto_convertt::do_cpp_new(
  const exprt &lhs,
  const exprt &rhs,
  goto_programt &dest)
{
  if (lhs.is_nil())
  {
    // TODO
    assert(0);
  }

  // grab initializer
  goto_programt tmp_initializer;
  cpp_new_initializer(lhs, rhs, tmp_initializer);

  exprt alloc_size;

  if (rhs.statement() == "cpp_new[]")
  {
    alloc_size = static_cast<const exprt &>(rhs.size_irep());
    if (alloc_size.type() != size_type())
      alloc_size.make_typecast(size_type());

    remove_sideeffects(alloc_size, dest);

    // jmorse: multiply alloc size by size of subtype.
    type2tc subtype = migrate_type(rhs.type());
    expr2tc alloc_units;
    migrate_expr(alloc_size, alloc_units);

    BigInt sz = type_byte_size(subtype);
    expr2tc sz_expr = constant_int2tc(size_type2(), sz);
    expr2tc byte_size = mul2tc(size_type2(), alloc_units, sz_expr);
    alloc_size = migrate_expr_back(byte_size);
  }
  else
    alloc_size = from_integer(1, size_type());

  if (alloc_size.is_nil())
    alloc_size = from_integer(1, size_type());

  if (alloc_size.type() != size_type())
  {
    alloc_size.make_typecast(size_type());
    simplify(alloc_size);
  }

  exprt new_expr("sideeffect", rhs.type());
  new_expr.statement(rhs.statement());
  new_expr.cmt_size(alloc_size);
  new_expr.location() = rhs.find_location();

  // produce new object
  goto_programt::targett t_n = dest.add_instruction(ASSIGN);
  exprt new_assign = code_assignt(lhs, new_expr);
  migrate_expr(new_assign, t_n->code);
  t_n->location = rhs.find_location();

  // run initializer
  dest.destructive_append(tmp_initializer);
}

void goto_convertt::cpp_new_initializer(
  const exprt &lhs,
  const exprt &rhs,
  goto_programt &dest)
{
  // grab initializer
  code_expressiont initializer;

  if (rhs.initializer().is_nil())
  {
    // Initialize with default value
    side_effect_exprt assignment("assign");
    assignment.type() = rhs.type().subtype();

    // the new object
    exprt new_object("new_object");
    new_object.set("#lvalue", true);
    new_object.type() = rhs.type().subtype();

    // Default value is zero
    exprt default_value = gen_zero(rhs.type().subtype());

    assignment.move_to_operands(new_object, default_value);
    initializer.expression() = assignment;
  }
  else
  {
    initializer = (code_expressiont &)rhs.initializer();

    if (!initializer.op0().get_bool("constructor"))
    {
      // for auto *p = Foo(3) and int *p = 3
      // constructor case:  init is Foo(&(*new_object), 3)
      // other case: init is 3,
      // we turn "3" into "*new_object = 3"
      side_effect_exprt assignment("assign");
      assignment.type() = rhs.type().subtype();

      // the new object
      exprt new_object("new_object");
      new_object.set("#lvalue", true);
      new_object.type() = rhs.type().subtype();

      assignment.move_to_operands(new_object, initializer.op0());
      initializer.expression() = assignment;
    }

    // XXX jmorse, const-qual misery
    const_cast<exprt &>(rhs).remove("initializer");
  }

  if (initializer.is_not_nil())
  {
    if (rhs.statement() == "cpp_new[]")
    {
      // build loop
    }
    else if (rhs.statement() == "cpp_new")
    {
      exprt deref_new("dereference", rhs.type().subtype());
      deref_new.copy_to_operands(lhs);
      replace_new_object(deref_new, initializer);
      convert(to_code(initializer), dest);
    }
    else
      assert(0);
  }
}

void goto_convertt::do_exit(
  const exprt &,
  const exprt &function,
  const exprt::operandst &,
  goto_programt &dest)
{
  // same as assume(false)

  goto_programt::targett t_a = dest.add_instruction(ASSUME);
  t_a->guard = gen_false_expr();
  t_a->location = function.location();
}

void goto_convertt::do_free(
  const exprt &,
  const exprt &function,
  const exprt::operandst &arguments,
  goto_programt &dest)
{
  // preserve the call
  codet free_statement("free");
  free_statement.location() = function.location();
  free_statement.copy_to_operands(arguments[0]);

  goto_programt::targett t_f = dest.add_instruction(OTHER);
  migrate_expr(free_statement, t_f->code);
  t_f->location = function.location();
}

bool is_lvalue(const exprt &expr)
{
  if (expr.is_index())
    return is_lvalue(to_index_expr(expr).op0());
  if (expr.is_member())
    return is_lvalue(to_member_expr(expr).op0());
  else if (expr.is_dereference())
    return true;
  else if (expr.is_symbol())
    return true;
  else
    return false;
}

exprt make_va_list(const exprt &expr)
{
  // we first strip any typecast
  if (expr.is_typecast())
    return make_va_list(to_typecast_expr(expr).op());

  // if it's an address of an lvalue, we take that
  if (
    expr.is_address_of() && expr.operands().size() == 1 &&
    is_lvalue(expr.op0()))
    return expr.op0();

  return expr;
}

void goto_convertt::do_function_call_symbol(
  const exprt &lhs,
  const exprt &function,
  const exprt::operandst &arguments,
  goto_programt &dest)
{
  if (function.invalid_object())
    return; // ignore

  // lookup symbol
  const irep_idt &identifier = function.identifier();

  const symbolt *symbol = ns.lookup(identifier);
  if (!symbol)
  {
    log_error("Function `{}' not found", id2string(identifier));
    abort();
  }

  if (!symbol->type.is_code())
  {
    log_error(
      "Function `{}' type mismatch: expected code", id2string(identifier));
  }

  // If the symbol is not nil, i.e., the user defined the expected behaviour of
  // the builtin function, we should honour the user function and call it
  if (symbol->value.is_not_nil())
  {
    // insert function call
    code_function_callt function_call;
    function_call.lhs() = lhs;
    function_call.function() = function;
    function_call.arguments() = arguments;
    function_call.location() = function.location();

    copy(function_call, FUNCTION_CALL, dest);
    return;
  }

  std::string base_name = symbol->name.as_string();

  bool is_assume =
    (base_name == "__ESBMC_assume") || (base_name == "__VERIFIER_assume");
  bool is_assert = (base_name == "assert");

  if (is_assume || is_assert)
  {
    if (arguments.size() != 1)
    {
      log_error("`{}' expected to have one argument", id2string(base_name));
      abort();
    }

    if (options.get_bool_option("no-assertions") && !is_assume)
      return;

    goto_programt::targett t =
      dest.add_instruction(is_assume ? ASSUME : ASSERT);
    migrate_expr(arguments.front(), t->guard);

    // The user may have re-declared the assert or assume functions to take an
    // integer argument, rather than a boolean. This leads to problems at the
    // other end of the model checking process, because we assume that
    // ASSUME/ASSERT insns are boolean exprs.  So, if the given argument to
    // this function isn't a bool, typecast it.  We can't rely on the C/C++
    // type system to ensure that.
    if (!is_bool_type(t->guard->type))
      t->guard = typecast2tc(get_bool_type(), t->guard);

    t->location = function.location();
    t->location.user_provided(true);

    if (is_assert)
      t->location.property("assertion");

    if (lhs.is_not_nil())
    {
      log_error("{} expected not to have LHS", id2string(base_name));
      abort();
    }
  }
  else if (base_name == "__ESBMC_assert")
  {
    // 1 argument --> Default assertion
    // 2 arguments --> Normal assertion + MSG
    if (arguments.size() > 2)
    {
      log_error("`{}' expected to have two arguments", id2string(base_name));
      abort();
    }

    if (options.get_bool_option("no-assertions"))
      return;

    goto_programt::targett t = dest.add_instruction(ASSERT);
    migrate_expr(arguments[0], t->guard);

    const std::string &description = arguments.size() == 1
                                       ? "ESBMC assertion"
                                       : get_string_constant(arguments[1]);
    t->location = function.location();
    t->location.user_provided(true);
    t->location.property("assertion");
    t->location.comment(description);

    if (lhs.is_not_nil())
    {
      log_error("{} expected not to have LHS", id2string(base_name));
      abort();
    }
  }
  else if (
    base_name == "__VERIFIER_error" || base_name == "reach_error" ||
    base_name == "__builtin_unreachable")
  {
    if (!arguments.empty())
    {
      log_error("`{}' expected to have no arguments", id2string(base_name));
      abort();
    }

#if ESBMC_SVCOMP
    /* <https://gitlab.com/sosy-lab/benchmarking/sv-benchmarks/-/issues/1296> */
    if (base_name == "__builtin_unreachable")
      return;
#endif

    goto_programt::targett t = dest.add_instruction(ASSERT);
    t->guard = gen_false_expr();
    t->location = function.location();
    t->location.user_provided(true);
    t->location.property("assertion");

    if (lhs.is_not_nil())
    {
      log_error("`{}' expected not to have LHS", id2string(base_name));
      abort();
    }

    // __VERIFIER_error has abort() semantics, even if no assertions
    // are being checked
    goto_programt::targett a = dest.add_instruction(ASSUME);
    a->guard = gen_false_expr();
    a->location = function.location();
    a->location.user_provided(true);
  }
  else if (
    (base_name == "__ESBMC_atomic_begin") ||
    (base_name == "__VERIFIER_atomic_begin"))
  {
    do_atomic_begin(lhs, function, arguments, dest);
  }
  else if (
    (base_name == "__ESBMC_atomic_end") ||
    (base_name == "__VERIFIER_atomic_end"))
  {
    do_atomic_end(lhs, function, arguments, dest);
  }
  else if (
    has_prefix(id2string(base_name), "nondet_") ||
    has_prefix(id2string(base_name), "__VERIFIER_nondet_"))
  {
    // make it a side effect if there is an LHS
    if (lhs.is_nil())
      return;

    exprt rhs = side_effect_expr_nondett(lhs.type());
    rhs.location() = function.location();

    code_assignt assignment(lhs, rhs);
    assignment.location() = function.location();
    copy(assignment, ASSIGN, dest);
  }
  else if (base_name == "exit")
  {
    do_exit(lhs, function, arguments, dest);
  }
  else if (base_name == "malloc")
  {
    do_malloc(lhs, function, arguments, dest);
  }
  else if (base_name == "realloc")
  {
    do_realloc(lhs, function, arguments, dest);
  }
  else if (base_name == "alloca" || base_name == "__builtin_alloca")
  {
    do_alloca(lhs, function, arguments, dest);
  }
  else if (base_name == "free")
  {
    do_free(lhs, function, arguments, dest);
  }
  else if (
    base_name == "printf" || base_name == "fprintf" || base_name == "dprintf" ||
    base_name == "sprintf" || base_name == "snprintf" ||
    base_name == "vfprintf")
  {
    do_printf(lhs, function, arguments, dest, base_name);
  }
  else if (base_name == "__assert_rtn" || base_name == "__assert_fail")
  {
    // __assert_fail is Linux
    // These take four arguments:
    // "expression", "file.c", line, __func__

    if (arguments.size() != 4)
    {
      log_error("`{}' expected to have four arguments", id2string(base_name));
      abort();
    }

    const irep_idt description =
      "assertion " + id2string(get_string_constant(arguments[0]));

    if (options.get_bool_option("no-assertions"))
      return;

    goto_programt::targett t = dest.add_instruction(ASSERT);
    t->guard = gen_false_expr();
    t->location = function.location();
    t->location.user_provided(true);
    t->location.property("assertion");
    t->location.comment(description);
    // we ignore any LHS
  }
  else if (config.ansi_c.target.is_freebsd() && base_name == "__assert")
  {
    /* This is FreeBSD, taking 4 arguments: __func__, __FILE__, __LINE__, #e */

    if (arguments.size() != 4)
    {
      log_error("`{}' expected to have four arguments", id2string(base_name));
      abort();
    }

    const irep_idt description =
      "assertion " + id2string(get_string_constant(arguments[3]));

    if (options.get_bool_option("no-assertions"))
      return;

    goto_programt::targett t = dest.add_instruction(ASSERT);
    t->guard = gen_false_expr();
    t->location = function.location();
    t->location.user_provided(true);
    t->location.property("assertion");
    t->location.comment(description);
    // we ignore any LHS
  }
  else if (base_name == "_wassert")
  {
    // this is Windows

    if (arguments.size() != 3)
    {
      log_error("`{}' expected to have three arguments", id2string(base_name));
      abort();
    }

    const std::string description =
      "assertion " + get_string_constant(arguments[0]);

    if (options.get_bool_option("no-assertions"))
      return;

    goto_programt::targett t = dest.add_instruction(ASSERT);
    t->guard = gen_false_expr();
    t->location = function.location();
    t->location.user_provided(true);
    t->location.property("assertion");
    t->location.comment(description);
    // we ignore any LHS
  }
  else if (base_name == "operator new")
  {
    assert(arguments.size() == 1);

    // Change it into a cpp_new expression
    side_effect_exprt new_function("cpp_new");
    new_function.add("#location") = function.cmt_location();
    new_function.add("sizeof") = arguments.front();

    // Set return type, a allocated pointer
    // XXX jmorse, const-qual misery
    new_function.type() = pointer_typet(
      static_cast<const typet &>(arguments.front().c_sizeof_type()));
    new_function.type().add("#location") = function.cmt_location();

    do_cpp_new(lhs, new_function, dest);
  }
  else if (base_name == "__ESBMC_va_arg")
  {
    // This does two things.
    // 1) Move list pointer to next argument.
    //    Done by gcc_builtin_va_arg_next.
    // 2) Return value of argument.
    //    This is just dereferencing.

    if (arguments.size() != 1)
    {
      log_error("`{}' expected to have one argument", id2string(base_name));
      abort();
    }

    exprt list_arg = make_va_list(arguments[0]);

    {
      side_effect_exprt rhs("va_arg", list_arg.type());
      rhs.copy_to_operands(list_arg);
      rhs.set("va_arg_type", to_code_type(function.type()).return_type());
      goto_programt::targett t1 = dest.add_instruction(ASSIGN);
      exprt assign_expr = code_assignt(list_arg, rhs);
      migrate_expr(assign_expr, t1->code);
      t1->location = function.location();
    }

    if (lhs.is_not_nil())
    {
      typet t = pointer_typet();
      t.subtype() = lhs.type();
      dereference_exprt rhs(lhs.type());
      rhs.op0() = typecast_exprt(list_arg, t);
      rhs.location() = function.location();
      goto_programt::targett t2 = dest.add_instruction(ASSIGN);
      exprt assign_expr = code_assignt(lhs, rhs);
      migrate_expr(assign_expr, t2->code);
      t2->location = function.location();
    }
  }
  else if (base_name == "__ESBMC_va_copy")
  {
    if (arguments.size() != 2)
    {
      log_error("`{}' expected to have two arguments", id2string(base_name));
      abort();
    }

    exprt dest_expr = make_va_list(arguments[0]);
    exprt src_expr = typecast_exprt(arguments[1], dest_expr.type());

    if (!is_lvalue(dest_expr))
    {
      log_error("va_copy argument expected to be lvalue");
      abort();
    }

    goto_programt::targett t = dest.add_instruction(ASSIGN);
    exprt assign_expr = code_assignt(dest_expr, src_expr);
    migrate_expr(assign_expr, t->code);
    t->location = function.location();
  }
  else if (base_name == "__ESBMC_va_start")
  {
    // Set the list argument to be the address of the
    // parameter argument.
    if (arguments.size() != 2)
    {
      log_error("`{}' expected to have two arguments", id2string(base_name));
      abort();
    }

    exprt dest_expr = make_va_list(arguments[0]);
    exprt src_expr =
      typecast_exprt(address_of_exprt(arguments[1]), dest_expr.type());

    if (!is_lvalue(dest_expr))
    {
      log_error("va_start argument expected to be lvalue");
      abort();
    }

    goto_programt::targett t = dest.add_instruction(ASSIGN);
    exprt assign_expr = code_assignt(dest_expr, src_expr);
    migrate_expr(assign_expr, t->code);
    t->location = function.location();
  }
  else if (base_name == "__ESBMC_va_end")
  {
    // Invalidates the argument. We do so by setting it to NULL.
    if (arguments.size() != 1)
    {
      log_error("`{}' expected to have one argument", id2string(base_name));
      abort();
    }

    exprt dest_expr = make_va_list(arguments[0]);

    if (!is_lvalue(dest_expr))
    {
      log_error("va_end argument expected to be lvalue");
      abort();
    }

    // our __builtin_va_list is a pointer
    if (ns.follow(dest_expr.type()).is_pointer())
    {
      goto_programt::targett t = dest.add_instruction(ASSIGN);
      exprt assign_expr = code_assignt(dest_expr, gen_zero(dest_expr.type()));
      migrate_expr(assign_expr, t->code);
      t->location = function.location();
    }
  }
  // Nontemporal means "do not cache please" (https://lwn.net/Articles/255364/)
  else if (base_name == "__builtin_nontemporal_load")
  {
    // T __builtin_nontemporal_load(T *addr);
    if (arguments.size() != 1)
    {
      log_error("`{}' expected to have one argument", id2string(base_name));
      abort();
    }

    goto_programt::targett t_n = dest.add_instruction(ASSIGN);

    exprt deref("dereference", lhs.type());
    deref.copy_to_operands(arguments[0]);

    exprt new_assign = code_assignt(lhs, deref);
    expr2tc new_assign_expr;
    migrate_expr(new_assign, new_assign_expr);
    t_n->code = new_assign_expr;
    t_n->location = function.location();
  }
  else
  {
    // insert function call
    code_function_callt function_call;
    function_call.lhs() = lhs;
    function_call.function() = function;
    function_call.arguments() = arguments;
    function_call.location() = function.location();

    copy(function_call, FUNCTION_CALL, dest);
  }
}
