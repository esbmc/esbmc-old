#include <iostream> /* std::cout */
#include <z3_conv.h>

#define new_ast new_solver_ast<z3_smt_ast>

static void error_handler(Z3_context c, Z3_error_code e)
{
  log_error("Z3 error {} encountered", Z3_get_error_msg(c, e));
  abort();
}

smt_convt *create_new_z3_solver(
  const optionst &options,
  const namespacet &ns,
  tuple_iface **tuple_api,
  array_iface **array_api,
  fp_convt **fp_api)
{
  std::string z3_file = options.get_option("z3-debug-dump-file");
  if (options.get_bool_option("z3-debug"))
  {
    // Generate Z3 API file
    Z3_open_log(z3_file.empty() ? "z3.log" : z3_file.c_str());

    // Add Type checker
    z3::config().set("stats", "true");

    // Add typecheck
    z3::config().set("type_check", "true");
    z3::config().set("well_sorted_check", "true");

    // SMT2 Compliant
    z3::config().set("smtlib2_compliant", "true");
  }

  if (
    options.get_bool_option("--smt-formula-only") ||
    options.get_bool_option("--smt-formula-too"))
    z3::config().set("smtlib2_compliant", "true");

  z3_convt *conv = new z3_convt(ns, options);
  *tuple_api = static_cast<tuple_iface *>(conv);
  *array_api = static_cast<array_iface *>(conv);
  *fp_api = static_cast<fp_convt *>(conv);
  return conv;
}

z3_convt::z3_convt(const namespacet &_ns, const optionst &_options)
  : smt_convt(_ns, _options),
    array_iface(true, true),
    fp_convt(this),
    z3_ctx(),
    solver((z3::tactic(z3_ctx, "simplify") & z3::tactic(z3_ctx, "solve-eqs") &
            z3::tactic(z3_ctx, "simplify") & z3::tactic(z3_ctx, "smt"))
             .mk_solver())
{
  z3::params p(z3_ctx);
  p.set("relevancy", 0U);
  p.set("model", true);
  p.set("proof", false);
  solver.set(p);
  Z3_set_ast_print_mode(z3_ctx, Z3_PRINT_SMTLIB2_COMPLIANT);
  Z3_set_error_handler(z3_ctx, error_handler);
}

z3_convt::~z3_convt()
{
  // delete ASTs before destructing z3_ctx: this speeds up the latter, see #752
  delete_all_asts();
}

void z3_convt::push_ctx()
{
  smt_convt::push_ctx();
  solver.push();
}

void z3_convt::pop_ctx()
{
  solver.pop();
  smt_convt::pop_ctx();
}

smt_convt::resultt z3_convt::dec_solve()
{
  pre_solve();

  z3::check_result result = solver.check();

  if (result == z3::sat)
    return P_SATISFIABLE;

  if (result == z3::unsat)
    return smt_convt::P_UNSATISFIABLE;

  return smt_convt::P_ERROR;
}

void z3_convt::assert_ast(smt_astt a)
{
  z3::expr theval = to_solver_smt_ast<z3_smt_ast>(a)->a;
  solver.add(theval);
}

z3::expr
z3_convt::mk_tuple_update(const z3::expr &t, unsigned i, const z3::expr &newval)
{
  z3::sort ty = t.get_sort();
  if (!ty.is_datatype())
  {
    log_error("argument must be a tuple");
    abort();
  }

  std::size_t num_fields = Z3_get_tuple_sort_num_fields(z3_ctx, ty);
  if (i >= num_fields)
  {
    log_error("invalid tuple update, index is too big");
    abort();
  }

  z3::expr_vector args(z3_ctx);
  for (std::size_t j = 0; j < num_fields; j++)
  {
    if (i == j)
    {
      /* use new_val at position i */
      args.push_back(newval);
    }
    else
    {
      /* use field j of t */
      z3::func_decl proj_decl =
        z3::to_func_decl(z3_ctx, Z3_get_tuple_sort_field_decl(z3_ctx, ty, j));
      args.push_back(proj_decl(t));
    }
  }

  return z3::to_func_decl(z3_ctx, Z3_get_tuple_sort_mk_decl(z3_ctx, ty))(args);
}

z3::expr z3_convt::mk_tuple_select(const z3::expr &t, unsigned i)
{
  z3::sort ty = t.get_sort();
  if (!ty.is_datatype())
  {
    log_error("Z3 conversion: argument must be a tuple");
    abort();
  }

  size_t num_fields = Z3_get_tuple_sort_num_fields(z3_ctx, ty);
  if (i >= num_fields)
  {
    log_error("Z3 conversion: invalid tuple select, index is too large");
    abort();
  }

  z3::func_decl proj_decl =
    z3::to_func_decl(z3_ctx, Z3_get_tuple_sort_field_decl(z3_ctx, ty, i));
  return proj_decl(t);
}

// SMT-abstraction migration routines.
smt_astt z3_convt::mk_add(smt_astt a, smt_astt b)
{
  assert(a->sort->id == SMT_SORT_INT || a->sort->id == SMT_SORT_REAL);
  assert(b->sort->id == SMT_SORT_INT || b->sort->id == SMT_SORT_REAL);
  assert(a->sort->id == b->sort->id);
  return new_ast(
    (to_solver_smt_ast<z3_smt_ast>(a)->a + to_solver_smt_ast<z3_smt_ast>(b)->a),
    a->sort);
}

smt_astt z3_convt::mk_bvadd(smt_astt a, smt_astt b)
{
  assert(a->sort->id != SMT_SORT_INT && a->sort->id != SMT_SORT_REAL);
  assert(b->sort->id != SMT_SORT_INT && b->sort->id != SMT_SORT_REAL);
  assert(a->sort->get_data_width() == b->sort->get_data_width());
  return new_ast(
    (to_solver_smt_ast<z3_smt_ast>(a)->a + to_solver_smt_ast<z3_smt_ast>(b)->a),
    a->sort);
}

smt_astt z3_convt::mk_sub(smt_astt a, smt_astt b)
{
  assert(a->sort->id == SMT_SORT_INT || a->sort->id == SMT_SORT_REAL);
  assert(b->sort->id == SMT_SORT_INT || b->sort->id == SMT_SORT_REAL);
  assert(a->sort->id == b->sort->id);
  return new_ast(
    (to_solver_smt_ast<z3_smt_ast>(a)->a - to_solver_smt_ast<z3_smt_ast>(b)->a),
    a->sort);
}

smt_astt z3_convt::mk_bvsub(smt_astt a, smt_astt b)
{
  assert(a->sort->id != SMT_SORT_INT && a->sort->id != SMT_SORT_REAL);
  assert(b->sort->id != SMT_SORT_INT && b->sort->id != SMT_SORT_REAL);
  assert(a->sort->get_data_width() == b->sort->get_data_width());
  return new_ast(
    (to_solver_smt_ast<z3_smt_ast>(a)->a - to_solver_smt_ast<z3_smt_ast>(b)->a),
    a->sort);
}

smt_astt z3_convt::mk_mul(smt_astt a, smt_astt b)
{
  assert(a->sort->id == SMT_SORT_INT || a->sort->id == SMT_SORT_REAL);
  assert(b->sort->id == SMT_SORT_INT || b->sort->id == SMT_SORT_REAL);
  assert(a->sort->id == b->sort->id);
  return new_ast(
    (to_solver_smt_ast<z3_smt_ast>(a)->a * to_solver_smt_ast<z3_smt_ast>(b)->a),
    a->sort);
}

smt_astt z3_convt::mk_bvmul(smt_astt a, smt_astt b)
{
  assert(a->sort->id != SMT_SORT_INT && a->sort->id != SMT_SORT_REAL);
  assert(b->sort->id != SMT_SORT_INT && b->sort->id != SMT_SORT_REAL);
  assert(a->sort->get_data_width() == b->sort->get_data_width());
  return new_ast(
    (to_solver_smt_ast<z3_smt_ast>(a)->a * to_solver_smt_ast<z3_smt_ast>(b)->a),
    a->sort);
}

smt_astt z3_convt::mk_mod(smt_astt a, smt_astt b)
{
  assert(a->sort->id == SMT_SORT_INT || a->sort->id == SMT_SORT_REAL);
  assert(b->sort->id == SMT_SORT_INT || b->sort->id == SMT_SORT_REAL);
  assert(a->sort->id == b->sort->id);
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_mod(
        z3_ctx,
        to_solver_smt_ast<z3_smt_ast>(a)->a,
        to_solver_smt_ast<z3_smt_ast>(b)->a)),
    a->sort);
}

smt_astt z3_convt::mk_bvsmod(smt_astt a, smt_astt b)
{
  assert(a->sort->id != SMT_SORT_INT && a->sort->id != SMT_SORT_REAL);
  assert(b->sort->id != SMT_SORT_INT && b->sort->id != SMT_SORT_REAL);
  assert(a->sort->get_data_width() == b->sort->get_data_width());
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_bvsrem(
        z3_ctx,
        to_solver_smt_ast<z3_smt_ast>(a)->a,
        to_solver_smt_ast<z3_smt_ast>(b)->a)),
    a->sort);
}

smt_astt z3_convt::mk_bvumod(smt_astt a, smt_astt b)
{
  assert(a->sort->id != SMT_SORT_INT && a->sort->id != SMT_SORT_REAL);
  assert(b->sort->id != SMT_SORT_INT && b->sort->id != SMT_SORT_REAL);
  assert(a->sort->get_data_width() == b->sort->get_data_width());
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_bvurem(
        z3_ctx,
        to_solver_smt_ast<z3_smt_ast>(a)->a,
        to_solver_smt_ast<z3_smt_ast>(b)->a)),
    a->sort);
}

smt_astt z3_convt::mk_div(smt_astt a, smt_astt b)
{
  assert(a->sort->id == SMT_SORT_INT || a->sort->id == SMT_SORT_REAL);
  assert(b->sort->id == SMT_SORT_INT || b->sort->id == SMT_SORT_REAL);
  assert(a->sort->id == b->sort->id);
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_div(
        z3_ctx,
        to_solver_smt_ast<z3_smt_ast>(a)->a,
        to_solver_smt_ast<z3_smt_ast>(b)->a)),
    a->sort);
}

smt_astt z3_convt::mk_bvsdiv(smt_astt a, smt_astt b)
{
  assert(a->sort->id != SMT_SORT_INT && a->sort->id != SMT_SORT_REAL);
  assert(b->sort->id != SMT_SORT_INT && b->sort->id != SMT_SORT_REAL);
  assert(a->sort->get_data_width() == b->sort->get_data_width());
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_bvsdiv(
        z3_ctx,
        to_solver_smt_ast<z3_smt_ast>(a)->a,
        to_solver_smt_ast<z3_smt_ast>(b)->a)),
    a->sort);
}

smt_astt z3_convt::mk_bvudiv(smt_astt a, smt_astt b)
{
  assert(a->sort->id != SMT_SORT_INT && a->sort->id != SMT_SORT_REAL);
  assert(b->sort->id != SMT_SORT_INT && b->sort->id != SMT_SORT_REAL);
  assert(a->sort->get_data_width() == b->sort->get_data_width());
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_bvudiv(
        z3_ctx,
        to_solver_smt_ast<z3_smt_ast>(a)->a,
        to_solver_smt_ast<z3_smt_ast>(b)->a)),
    a->sort);
}

smt_astt z3_convt::mk_shl(smt_astt a, smt_astt b)
{
  assert(a->sort->id == SMT_SORT_INT || a->sort->id == SMT_SORT_REAL);
  assert(b->sort->id == SMT_SORT_INT || b->sort->id == SMT_SORT_REAL);
  assert(a->sort->id == b->sort->id);
  return new_ast(
    to_solver_smt_ast<z3_smt_ast>(a)->a *
      pw(z3_ctx.int_val(2), to_solver_smt_ast<z3_smt_ast>(b)->a),
    a->sort);
}

smt_astt z3_convt::mk_bvshl(smt_astt a, smt_astt b)
{
  assert(a->sort->id != SMT_SORT_INT && a->sort->id != SMT_SORT_REAL);
  assert(b->sort->id != SMT_SORT_INT && b->sort->id != SMT_SORT_REAL);
  assert(a->sort->get_data_width() == b->sort->get_data_width());
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_bvshl(
        z3_ctx,
        to_solver_smt_ast<z3_smt_ast>(a)->a,
        to_solver_smt_ast<z3_smt_ast>(b)->a)),
    a->sort);
}

smt_astt z3_convt::mk_bvashr(smt_astt a, smt_astt b)
{
  assert(a->sort->id != SMT_SORT_INT && a->sort->id != SMT_SORT_REAL);
  assert(b->sort->id != SMT_SORT_INT && b->sort->id != SMT_SORT_REAL);
  assert(a->sort->get_data_width() == b->sort->get_data_width());
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_bvashr(
        z3_ctx,
        to_solver_smt_ast<z3_smt_ast>(a)->a,
        to_solver_smt_ast<z3_smt_ast>(b)->a)),
    a->sort);
}

smt_astt z3_convt::mk_bvlshr(smt_astt a, smt_astt b)
{
  assert(a->sort->id != SMT_SORT_INT && a->sort->id != SMT_SORT_REAL);
  assert(b->sort->id != SMT_SORT_INT && b->sort->id != SMT_SORT_REAL);
  assert(a->sort->get_data_width() == b->sort->get_data_width());
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_bvlshr(
        z3_ctx,
        to_solver_smt_ast<z3_smt_ast>(a)->a,
        to_solver_smt_ast<z3_smt_ast>(b)->a)),
    a->sort);
}

smt_astt z3_convt::mk_neg(smt_astt a)
{
  assert(a->sort->id == SMT_SORT_INT || a->sort->id == SMT_SORT_REAL);
  return new_ast((-to_solver_smt_ast<z3_smt_ast>(a)->a), a->sort);
}

smt_astt z3_convt::mk_bvneg(smt_astt a)
{
  assert(a->sort->id != SMT_SORT_INT && a->sort->id != SMT_SORT_REAL);
  return new_ast((-to_solver_smt_ast<z3_smt_ast>(a)->a), a->sort);
}

smt_astt z3_convt::mk_bvnot(smt_astt a)
{
  assert(a->sort->id != SMT_SORT_INT && a->sort->id != SMT_SORT_REAL);
  return new_ast((~to_solver_smt_ast<z3_smt_ast>(a)->a), a->sort);
}

smt_astt z3_convt::mk_bvnxor(smt_astt a, smt_astt b)
{
  assert(a->sort->id != SMT_SORT_INT && a->sort->id != SMT_SORT_REAL);
  assert(b->sort->id != SMT_SORT_INT && b->sort->id != SMT_SORT_REAL);
  assert(a->sort->get_data_width() == b->sort->get_data_width());
  return new_ast(
    !(to_solver_smt_ast<z3_smt_ast>(a)->a ^
      to_solver_smt_ast<z3_smt_ast>(b)->a),
    a->sort);
}

smt_astt z3_convt::mk_bvnor(smt_astt a, smt_astt b)
{
  assert(a->sort->id != SMT_SORT_INT && a->sort->id != SMT_SORT_REAL);
  assert(b->sort->id != SMT_SORT_INT && b->sort->id != SMT_SORT_REAL);
  assert(a->sort->get_data_width() == b->sort->get_data_width());
  return new_ast(
    !(to_solver_smt_ast<z3_smt_ast>(a)->a |
      to_solver_smt_ast<z3_smt_ast>(b)->a),
    a->sort);
}

smt_astt z3_convt::mk_bvnand(smt_astt a, smt_astt b)
{
  assert(a->sort->id != SMT_SORT_INT && a->sort->id != SMT_SORT_REAL);
  assert(b->sort->id != SMT_SORT_INT && b->sort->id != SMT_SORT_REAL);
  assert(a->sort->get_data_width() == b->sort->get_data_width());
  return new_ast(
    !(to_solver_smt_ast<z3_smt_ast>(a)->a &
      to_solver_smt_ast<z3_smt_ast>(b)->a),
    a->sort);
}

smt_astt z3_convt::mk_bvxor(smt_astt a, smt_astt b)
{
  assert(a->sort->id != SMT_SORT_INT && a->sort->id != SMT_SORT_REAL);
  assert(b->sort->id != SMT_SORT_INT && b->sort->id != SMT_SORT_REAL);
  assert(a->sort->get_data_width() == b->sort->get_data_width());
  return new_ast(
    (to_solver_smt_ast<z3_smt_ast>(a)->a ^ to_solver_smt_ast<z3_smt_ast>(b)->a),
    a->sort);
}

smt_astt z3_convt::mk_bvor(smt_astt a, smt_astt b)
{
  assert(a->sort->id != SMT_SORT_INT && a->sort->id != SMT_SORT_REAL);
  assert(b->sort->id != SMT_SORT_INT && b->sort->id != SMT_SORT_REAL);
  assert(a->sort->get_data_width() == b->sort->get_data_width());
  return new_ast(
    (to_solver_smt_ast<z3_smt_ast>(a)->a | to_solver_smt_ast<z3_smt_ast>(b)->a),
    a->sort);
}

smt_astt z3_convt::mk_bvand(smt_astt a, smt_astt b)
{
  assert(a->sort->id != SMT_SORT_INT && a->sort->id != SMT_SORT_REAL);
  assert(b->sort->id != SMT_SORT_INT && b->sort->id != SMT_SORT_REAL);
  assert(a->sort->get_data_width() == b->sort->get_data_width());
  return new_ast(
    (to_solver_smt_ast<z3_smt_ast>(a)->a & to_solver_smt_ast<z3_smt_ast>(b)->a),
    a->sort);
}

smt_astt z3_convt::mk_implies(smt_astt a, smt_astt b)
{
  assert(a->sort->id == SMT_SORT_BOOL && b->sort->id == SMT_SORT_BOOL);
  return new_ast(
    implies(
      to_solver_smt_ast<z3_smt_ast>(a)->a, to_solver_smt_ast<z3_smt_ast>(b)->a),
    boolean_sort);
}

smt_astt z3_convt::mk_xor(smt_astt a, smt_astt b)
{
  assert(a->sort->id == SMT_SORT_BOOL && b->sort->id == SMT_SORT_BOOL);
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_xor(
        z3_ctx,
        to_solver_smt_ast<z3_smt_ast>(a)->a,
        to_solver_smt_ast<z3_smt_ast>(b)->a)),
    boolean_sort);
}

smt_astt z3_convt::mk_or(smt_astt a, smt_astt b)
{
  assert(a->sort->id == SMT_SORT_BOOL && b->sort->id == SMT_SORT_BOOL);
  return new_ast(
    (to_solver_smt_ast<z3_smt_ast>(a)->a ||
     to_solver_smt_ast<z3_smt_ast>(b)->a),
    boolean_sort);
}

smt_astt z3_convt::mk_and(smt_astt a, smt_astt b)
{
  assert(a->sort->id == SMT_SORT_BOOL && b->sort->id == SMT_SORT_BOOL);
  return new_ast(
    (to_solver_smt_ast<z3_smt_ast>(a)->a &&
     to_solver_smt_ast<z3_smt_ast>(b)->a),
    boolean_sort);
}

smt_astt z3_convt::mk_not(smt_astt a)
{
  assert(a->sort->id == SMT_SORT_BOOL);
  return new_ast(!to_solver_smt_ast<z3_smt_ast>(a)->a, boolean_sort);
}

smt_astt z3_convt::mk_lt(smt_astt a, smt_astt b)
{
  assert(a->sort->id == SMT_SORT_INT || a->sort->id == SMT_SORT_REAL);
  assert(b->sort->id == SMT_SORT_INT || b->sort->id == SMT_SORT_REAL);
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_lt(
        z3_ctx,
        to_solver_smt_ast<z3_smt_ast>(a)->a,
        to_solver_smt_ast<z3_smt_ast>(b)->a)),
    boolean_sort);
}

smt_astt z3_convt::mk_bvult(smt_astt a, smt_astt b)
{
  assert(a->sort->id != SMT_SORT_INT && a->sort->id != SMT_SORT_REAL);
  assert(b->sort->id != SMT_SORT_INT && b->sort->id != SMT_SORT_REAL);
  assert(a->sort->get_data_width() == b->sort->get_data_width());
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_bvult(
        z3_ctx,
        to_solver_smt_ast<z3_smt_ast>(a)->a,
        to_solver_smt_ast<z3_smt_ast>(b)->a)),
    boolean_sort);
}

smt_astt z3_convt::mk_bvslt(smt_astt a, smt_astt b)
{
  assert(a->sort->id != SMT_SORT_INT && a->sort->id != SMT_SORT_REAL);
  assert(b->sort->id != SMT_SORT_INT && b->sort->id != SMT_SORT_REAL);
  assert(a->sort->get_data_width() == b->sort->get_data_width());
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_bvslt(
        z3_ctx,
        to_solver_smt_ast<z3_smt_ast>(a)->a,
        to_solver_smt_ast<z3_smt_ast>(b)->a)),
    boolean_sort);
}

smt_astt z3_convt::mk_gt(smt_astt a, smt_astt b)
{
  assert(a->sort->id == SMT_SORT_INT || a->sort->id == SMT_SORT_REAL);
  assert(b->sort->id == SMT_SORT_INT || b->sort->id == SMT_SORT_REAL);
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_gt(
        z3_ctx,
        to_solver_smt_ast<z3_smt_ast>(a)->a,
        to_solver_smt_ast<z3_smt_ast>(b)->a)),
    boolean_sort);
}

smt_astt z3_convt::mk_bvugt(smt_astt a, smt_astt b)
{
  assert(a->sort->id != SMT_SORT_INT && a->sort->id != SMT_SORT_REAL);
  assert(b->sort->id != SMT_SORT_INT && b->sort->id != SMT_SORT_REAL);
  assert(a->sort->get_data_width() == b->sort->get_data_width());
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_bvugt(
        z3_ctx,
        to_solver_smt_ast<z3_smt_ast>(a)->a,
        to_solver_smt_ast<z3_smt_ast>(b)->a)),
    boolean_sort);
}

smt_astt z3_convt::mk_bvsgt(smt_astt a, smt_astt b)
{
  assert(a->sort->id != SMT_SORT_INT && a->sort->id != SMT_SORT_REAL);
  assert(b->sort->id != SMT_SORT_INT && b->sort->id != SMT_SORT_REAL);
  assert(a->sort->get_data_width() == b->sort->get_data_width());
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_bvsgt(
        z3_ctx,
        to_solver_smt_ast<z3_smt_ast>(a)->a,
        to_solver_smt_ast<z3_smt_ast>(b)->a)),
    boolean_sort);
}

smt_astt z3_convt::mk_le(smt_astt a, smt_astt b)
{
  assert(a->sort->id == SMT_SORT_INT || a->sort->id == SMT_SORT_REAL);
  assert(b->sort->id == SMT_SORT_INT || b->sort->id == SMT_SORT_REAL);
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_le(
        z3_ctx,
        to_solver_smt_ast<z3_smt_ast>(a)->a,
        to_solver_smt_ast<z3_smt_ast>(b)->a)),
    boolean_sort);
}

smt_astt z3_convt::mk_bvule(smt_astt a, smt_astt b)
{
  assert(a->sort->id != SMT_SORT_INT && a->sort->id != SMT_SORT_REAL);
  assert(b->sort->id != SMT_SORT_INT && b->sort->id != SMT_SORT_REAL);
  assert(a->sort->get_data_width() == b->sort->get_data_width());
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_bvule(
        z3_ctx,
        to_solver_smt_ast<z3_smt_ast>(a)->a,
        to_solver_smt_ast<z3_smt_ast>(b)->a)),
    boolean_sort);
}

smt_astt z3_convt::mk_bvsle(smt_astt a, smt_astt b)
{
  assert(a->sort->id != SMT_SORT_INT && a->sort->id != SMT_SORT_REAL);
  assert(b->sort->id != SMT_SORT_INT && b->sort->id != SMT_SORT_REAL);
  assert(a->sort->get_data_width() == b->sort->get_data_width());
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_bvsle(
        z3_ctx,
        to_solver_smt_ast<z3_smt_ast>(a)->a,
        to_solver_smt_ast<z3_smt_ast>(b)->a)),
    boolean_sort);
}

smt_astt z3_convt::mk_ge(smt_astt a, smt_astt b)
{
  assert(a->sort->id == SMT_SORT_INT || a->sort->id == SMT_SORT_REAL);
  assert(b->sort->id == SMT_SORT_INT || b->sort->id == SMT_SORT_REAL);
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_ge(
        z3_ctx,
        to_solver_smt_ast<z3_smt_ast>(a)->a,
        to_solver_smt_ast<z3_smt_ast>(b)->a)),
    boolean_sort);
}

smt_astt z3_convt::mk_bvuge(smt_astt a, smt_astt b)
{
  assert(a->sort->id != SMT_SORT_INT && a->sort->id != SMT_SORT_REAL);
  assert(b->sort->id != SMT_SORT_INT && b->sort->id != SMT_SORT_REAL);
  assert(a->sort->get_data_width() == b->sort->get_data_width());
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_bvuge(
        z3_ctx,
        to_solver_smt_ast<z3_smt_ast>(a)->a,
        to_solver_smt_ast<z3_smt_ast>(b)->a)),
    boolean_sort);
}

smt_astt z3_convt::mk_bvsge(smt_astt a, smt_astt b)
{
  assert(a->sort->id != SMT_SORT_INT && a->sort->id != SMT_SORT_REAL);
  assert(b->sort->id != SMT_SORT_INT && b->sort->id != SMT_SORT_REAL);
  assert(a->sort->get_data_width() == b->sort->get_data_width());
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_bvsge(
        z3_ctx,
        to_solver_smt_ast<z3_smt_ast>(a)->a,
        to_solver_smt_ast<z3_smt_ast>(b)->a)),
    boolean_sort);
}

smt_astt z3_convt::mk_eq(smt_astt a, smt_astt b)
{
  assert(a->sort->get_data_width() == b->sort->get_data_width());
  return new_ast(
    (to_solver_smt_ast<z3_smt_ast>(a)->a ==
     to_solver_smt_ast<z3_smt_ast>(b)->a),
    boolean_sort);
}

smt_astt z3_convt::mk_neq(smt_astt a, smt_astt b)
{
  assert(a->sort->get_data_width() == b->sort->get_data_width());
  return new_ast(
    (to_solver_smt_ast<z3_smt_ast>(a)->a !=
     to_solver_smt_ast<z3_smt_ast>(b)->a),
    boolean_sort);
}

smt_astt z3_convt::mk_store(smt_astt a, smt_astt b, smt_astt c)
{
  assert(a->sort->id == SMT_SORT_ARRAY);
  assert(a->sort->get_domain_width() == b->sort->get_data_width());
  assert(
    a->sort->get_range_sort()->get_data_width() == c->sort->get_data_width());
  return new_ast(
    store(
      to_solver_smt_ast<z3_smt_ast>(a)->a,
      to_solver_smt_ast<z3_smt_ast>(b)->a,
      to_solver_smt_ast<z3_smt_ast>(c)->a),
    a->sort);
}

smt_astt z3_convt::mk_select(smt_astt a, smt_astt b)
{
  assert(a->sort->id == SMT_SORT_ARRAY);
  assert(a->sort->get_domain_width() == b->sort->get_data_width());
  return new_ast(
    select(
      to_solver_smt_ast<z3_smt_ast>(a)->a, to_solver_smt_ast<z3_smt_ast>(b)->a),
    a->sort->get_range_sort());
}

smt_astt z3_convt::mk_real2int(smt_astt a)
{
  assert(a->sort->id == SMT_SORT_REAL);
  return new_ast(
    z3::to_expr(
      z3_ctx, Z3_mk_real2int(z3_ctx, to_solver_smt_ast<z3_smt_ast>(a)->a)),
    mk_int_sort());
}

smt_astt z3_convt::mk_int2real(smt_astt a)
{
  assert(a->sort->id == SMT_SORT_INT);
  return new_ast(
    z3::to_expr(
      z3_ctx, Z3_mk_int2real(z3_ctx, to_solver_smt_ast<z3_smt_ast>(a)->a)),
    mk_real_sort());
}

smt_astt z3_convt::mk_isint(smt_astt a)
{
  assert(a->sort->id == SMT_SORT_INT || a->sort->id == SMT_SORT_REAL);
  return new_ast(
    z3::to_expr(
      z3_ctx, Z3_mk_is_int(z3_ctx, to_solver_smt_ast<z3_smt_ast>(a)->a)),
    boolean_sort);
}

smt_astt z3_convt::mk_extract(smt_astt a, unsigned int high, unsigned int low)
{
  // If it's a floatbv, convert it to bv
  if (a->sort->id == SMT_SORT_FPBV)
    a = mk_from_fp_to_bv(a);

  smt_sortt s = mk_bv_sort(high - low + 1);
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_extract(z3_ctx, high, low, to_solver_smt_ast<z3_smt_ast>(a)->a)),
    s);
}

smt_astt z3_convt::mk_sign_ext(smt_astt a, unsigned int topwidth)
{
  smt_sortt s = mk_bv_sort(a->sort->get_data_width() + topwidth);
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_sign_ext(z3_ctx, topwidth, to_solver_smt_ast<z3_smt_ast>(a)->a)),
    s);
}

smt_astt z3_convt::mk_zero_ext(smt_astt a, unsigned int topwidth)
{
  smt_sortt s = mk_bv_sort(a->sort->get_data_width() + topwidth);
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_zero_ext(z3_ctx, topwidth, to_solver_smt_ast<z3_smt_ast>(a)->a)),
    s);
}

smt_astt z3_convt::mk_concat(smt_astt a, smt_astt b)
{
  smt_sortt s =
    mk_bv_sort(a->sort->get_data_width() + b->sort->get_data_width());

  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_concat(
        z3_ctx,
        to_solver_smt_ast<z3_smt_ast>(a)->a,
        to_solver_smt_ast<z3_smt_ast>(b)->a)),
    s);
}

smt_astt z3_convt::mk_ite(smt_astt cond, smt_astt t, smt_astt f)
{
  assert(cond->sort->id == SMT_SORT_BOOL);
  assert(t->sort->get_data_width() == f->sort->get_data_width());

  return new_ast(
    ite(
      to_solver_smt_ast<z3_smt_ast>(cond)->a,
      to_solver_smt_ast<z3_smt_ast>(t)->a,
      to_solver_smt_ast<z3_smt_ast>(f)->a),
    t->sort);
}

smt_astt z3_convt::mk_smt_int(const BigInt &theint)
{
  smt_sortt s = mk_int_sort();
  if (theint.is_negative())
    return new_ast(z3_ctx.int_val(theint.to_int64()), s);

  return new_ast(z3_ctx.int_val(theint.to_uint64()), s);
}

smt_astt z3_convt::mk_smt_real(const std::string &str)
{
  smt_sortt s = mk_real_sort();
  return new_ast(z3_ctx.real_val(str.c_str()), s);
}

smt_astt z3_convt::mk_smt_bv(const BigInt &theint, smt_sortt s)
{
  std::size_t w = s->get_data_width();
  z3::expr e(z3_ctx);

  if (theint.is_int64())
    e = z3_ctx.bv_val(theint.to_int64(), w);
  else if (theint.is_uint64())
    e = z3_ctx.bv_val(theint.to_uint64(), w);
  else
  {
    std::string dec = integer2string(theint, 10);
    e = z3_ctx.bv_val(dec.c_str(), w);
  }

  return new_ast(e, s);
}

smt_astt z3_convt::mk_smt_fpbv(const ieee_floatt &thereal)
{
  smt_sortt s = mk_real_fp_sort(thereal.spec.e, thereal.spec.f);

  const BigInt sig = thereal.get_fraction();

  // If the number is denormal, we set the exponent to -bias
  const BigInt exp =
    thereal.is_normal() ? thereal.get_exponent() + thereal.spec.bias() : 0;

  smt_astt sgn_bv = mk_smt_bv(BigInt(thereal.get_sign()), mk_bv_sort(1));
  smt_astt exp_bv = mk_smt_bv(exp, mk_bv_sort(thereal.spec.e));
  smt_astt sig_bv = mk_smt_bv(sig, mk_bv_sort(thereal.spec.f));

  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_fpa_fp(
        z3_ctx,
        to_solver_smt_ast<z3_smt_ast>(sgn_bv)->a,
        to_solver_smt_ast<z3_smt_ast>(exp_bv)->a,
        to_solver_smt_ast<z3_smt_ast>(sig_bv)->a)),
    s);
}

smt_astt z3_convt::mk_smt_fpbv_nan(bool sgn, unsigned ew, unsigned sw)
{
  smt_sortt s = mk_real_fp_sort(ew, sw - 1);
  smt_astt the_nan = new_ast(
    z3::to_expr(
      z3_ctx, Z3_mk_fpa_nan(z3_ctx, to_solver_smt_sort<z3::sort>(s)->s)),
    s);

  if (sgn)
    the_nan = fp_convt::mk_smt_fpbv_neg(the_nan);

  return the_nan;
}

smt_astt z3_convt::mk_smt_fpbv_inf(bool sgn, unsigned ew, unsigned sw)
{
  smt_sortt s = mk_real_fp_sort(ew, sw - 1);
  return new_ast(
    z3::to_expr(
      z3_ctx, Z3_mk_fpa_inf(z3_ctx, to_solver_smt_sort<z3::sort>(s)->s, sgn)),
    s);
}

smt_astt z3_convt::mk_smt_fpbv_rm(ieee_floatt::rounding_modet rm)
{
  smt_sortt s = mk_fpbv_rm_sort();

  switch (rm)
  {
  case ieee_floatt::ROUND_TO_EVEN:
    return new_ast(z3::to_expr(z3_ctx, Z3_mk_fpa_rne(z3_ctx)), s);

  case ieee_floatt::ROUND_TO_MINUS_INF:
    return new_ast(z3::to_expr(z3_ctx, Z3_mk_fpa_rtn(z3_ctx)), s);

  case ieee_floatt::ROUND_TO_PLUS_INF:
    return new_ast(z3::to_expr(z3_ctx, Z3_mk_fpa_rtp(z3_ctx)), s);

  case ieee_floatt::ROUND_TO_ZERO:
    return new_ast(z3::to_expr(z3_ctx, Z3_mk_fpa_rtz(z3_ctx)), s);

  default:
    break;
  }

  abort();
}

smt_astt z3_convt::mk_smt_bool(bool val)
{
  return new_ast(z3_ctx.bool_val(val), boolean_sort);
}

smt_astt z3_convt::mk_array_symbol(
  const std::string &name,
  const smt_sort *s,
  smt_sortt array_subtype [[maybe_unused]])
{
  return mk_smt_symbol(name, s);
}

smt_astt z3_convt::mk_smt_symbol(const std::string &name, const smt_sort *s)
{
  return new_ast(
    z3_ctx.constant(name.c_str(), to_solver_smt_sort<z3::sort>(s)->s), s);
}

smt_sortt z3_convt::mk_struct_sort(const type2tc &type)
{
  if (is_array_type(type))
  {
    const array_type2t &arrtype = to_array_type(type);
    smt_sortt subtypesort = convert_sort(arrtype.subtype);
    smt_sortt d = mk_int_bv_sort(make_array_domain_type(arrtype)->get_width());
    return mk_array_sort(d, subtypesort);
  }

  const struct_type2t &strct = to_struct_type(type);
  const std::size_t num_members = strct.members.size();

  z3::array<Z3_symbol> member_names(num_members);
  z3::array<Z3_sort> member_sorts(num_members);
  for (std::size_t i = 0; i < num_members; ++i)
  {
    member_names[i] =
      z3_ctx.str_symbol(strct.member_names[i].as_string().c_str());
    member_sorts[i] =
      to_solver_smt_sort<z3::sort>(convert_sort(strct.members[i]))->s;
  }

  z3::symbol tuple_name = z3_ctx.str_symbol(
    std::string("struct_type_" + strct.name.as_string()).c_str());

  Z3_func_decl mk_tuple_decl;
  z3::array<Z3_func_decl> proj_decls(num_members);
  z3::sort sort = to_sort(
    z3_ctx,
    Z3_mk_tuple_sort(
      z3_ctx,
      tuple_name,
      num_members,
      member_names.ptr(),
      member_sorts.ptr(),
      &mk_tuple_decl,
      proj_decls.ptr()));

  return new solver_smt_sort<z3::sort>(SMT_SORT_STRUCT, sort, type);
}

smt_astt z3_smt_ast::update(
  smt_convt *conv,
  smt_astt value,
  unsigned int idx,
  expr2tc idx_expr) const
{
  if (sort->id == SMT_SORT_ARRAY)
    return smt_ast::update(conv, value, idx, idx_expr);

  assert(sort->id == SMT_SORT_STRUCT);
  assert(is_nil_expr(idx_expr) && "Can only update constant index tuple elems");

  z3_convt *z3_conv = static_cast<z3_convt *>(conv);
  const z3_smt_ast *updateval = to_solver_smt_ast<z3_smt_ast>(value);
  return z3_conv->new_ast(z3_conv->mk_tuple_update(a, idx, updateval->a), sort);
}

smt_astt z3_smt_ast::project(smt_convt *conv, unsigned int elem) const
{
  z3_convt *z3_conv = static_cast<z3_convt *>(conv);

  assert(!is_nil_type(sort->get_tuple_type()));
  const struct_union_data &data = conv->get_type_def(sort->get_tuple_type());

  assert(elem < data.members.size());
  const smt_sort *idx_sort = conv->convert_sort(data.members[elem]);

  return z3_conv->new_ast(z3_conv->mk_tuple_select(a, elem), idx_sort);
}

smt_astt z3_convt::tuple_create(const expr2tc &structdef)
{
  const constant_struct2t &strct = to_constant_struct2t(structdef);
  const struct_union_data &type =
    static_cast<const struct_union_data &>(*strct.type);

  // Converts a static struct - IE, one that hasn't had any "with"
  // operations applied to it, perhaps due to initialization or constant
  // propagation.
  const std::vector<expr2tc> &members = strct.datatype_members;
  const std::vector<type2tc> &member_types = type.members;

  // Populate tuple with members of that struct
  z3::expr_vector args(z3_ctx);
  for (std::size_t i = 0; i < member_types.size(); ++i)
    args.push_back(to_solver_smt_ast<z3_smt_ast>(convert_ast(members[i]))->a);

  // Create tuple itself, return to caller. This is a lump of data, we don't
  // need to bind it to a name or symbol.
  smt_sortt s = mk_struct_sort(structdef->type);

  z3::func_decl z3_tuple = z3::to_func_decl(
    z3_ctx,
    Z3_get_tuple_sort_mk_decl(z3_ctx, to_solver_smt_sort<z3::sort>(s)->s));
  return new_ast(z3_tuple(args), s);
}

smt_astt z3_convt::tuple_fresh(const smt_sort *s, std::string name)
{
  return new_ast(
    z3_ctx.constant(name.c_str(), to_solver_smt_sort<z3::sort>(s)->s), s);
}

smt_astt
z3_convt::convert_array_of(smt_astt init_val, unsigned long domain_width)
{
  smt_sortt dom_sort = mk_int_bv_sort(domain_width);

  z3::expr val = to_solver_smt_ast<z3_smt_ast>(init_val)->a;
  z3::expr output = z3::to_expr(
    z3_ctx,
    Z3_mk_const_array(z3_ctx, to_solver_smt_sort<z3::sort>(dom_sort)->s, val));

  return new_ast(output, mk_array_sort(dom_sort, init_val->sort));
}

smt_astt z3_convt::tuple_array_create(
  const type2tc &arr_type,
  smt_astt *input_args,
  bool const_array,
  const smt_sort *domain)
{
  const array_type2t &arrtype = to_array_type(arr_type);
  z3::sort array_sort = to_solver_smt_sort<z3::sort>(convert_sort(arr_type))->s;
  z3::sort dom_sort = array_sort.array_domain();

  smt_sortt ssort = mk_struct_sort(arrtype.subtype);
  smt_sortt asort = mk_array_sort(domain, ssort);

  if (const_array)
  {
    const z3_smt_ast *tmpast = to_solver_smt_ast<z3_smt_ast>(*input_args);
    z3::expr value = tmpast->a;

    if (is_bool_type(arrtype.subtype))
      value = z3_ctx.bool_val(false);

    return new_ast(
      z3::to_expr(z3_ctx, Z3_mk_const_array(z3_ctx, dom_sort, value)), asort);
  }

  assert(
    !is_nil_expr(arrtype.array_size) &&
    "Non-const array-of's can't be infinitely sized");

  assert(
    is_constant_int2t(arrtype.array_size) &&
    "array_of sizes should be constant");

  std::string name = mk_fresh_name("z3_convt::tuple_array_create");
  z3::expr output = z3_ctx.constant(name.c_str(), array_sort);
  for (std::size_t i = 0; i < to_constant_int2t(arrtype.array_size).as_ulong();
       ++i)
  {
    z3::expr int_cte = z3_ctx.num_val(i, dom_sort);
    const z3_smt_ast *tmpast = to_solver_smt_ast<z3_smt_ast>(input_args[i]);
    output = z3::store(output, int_cte, tmpast->a);
  }

  return new_ast(output, asort);
}

smt_astt z3_convt::mk_tuple_symbol(const std::string &name, smt_sortt s)
{
  return mk_smt_symbol(name, s);
}

smt_astt z3_convt::mk_tuple_array_symbol(const expr2tc &expr)
{
  const symbol2t &sym = to_symbol2t(expr);
  return mk_smt_symbol(sym.get_symbol_name(), convert_sort(sym.type));
}

smt_astt
z3_convt::tuple_array_of(const expr2tc &init, unsigned long domain_width)
{
  return convert_array_of(convert_ast(init), domain_width);
}

expr2tc z3_convt::tuple_get(const type2tc &type, smt_astt sym)
{
  const struct_union_data &strct = get_type_def(type);

  if (is_pointer_type(type))
  {
    // Pointer have two fields, a base address and an offset, so we just
    // need to get the two numbers and call the pointer API

    smt_astt object = new_ast(
      mk_tuple_select(to_solver_smt_ast<z3_smt_ast>(sym)->a, 0),
      convert_sort(strct.members[0]));

    smt_astt offset = new_ast(
      mk_tuple_select(to_solver_smt_ast<z3_smt_ast>(sym)->a, 1),
      convert_sort(strct.members[1]));

    unsigned int num =
      get_bv(object, is_signedbv_type(strct.members[0])).to_uint64();
    unsigned int offs =
      get_bv(offset, is_signedbv_type(strct.members[1])).to_uint64();
    pointer_logict::pointert p(num, BigInt(offs));
    return pointer_logic.back().pointer_expr(p, type);
  }

  // Otherwise, run through all fields and dispatch to 'get_by_ast' again.
  std::vector<expr2tc> outmem;
  unsigned int i = 0;
  for (auto const &it : strct.members)
  {
    outmem.push_back(get_by_ast(
      it,
      new_ast(
        mk_tuple_select(to_solver_smt_ast<z3_smt_ast>(sym)->a, i),
        convert_sort(it))));
    i++;
  }

  return constant_struct2tc(type, std::move(outmem));
}

expr2tc z3_convt::tuple_get(const expr2tc &expr)
{
  const struct_union_data &strct = get_type_def(expr->type);

  if (is_pointer_type(expr->type))
  {
    // Pointer have two fields, a base address and an offset, so we just
    // need to get the two numbers and call the pointer API

    smt_astt sym = convert_ast(expr);

    smt_astt object = new_ast(
      mk_tuple_select(to_solver_smt_ast<z3_smt_ast>(sym)->a, 0),
      convert_sort(strct.members[0]));

    smt_astt offset = new_ast(
      mk_tuple_select(to_solver_smt_ast<z3_smt_ast>(sym)->a, 1),
      convert_sort(strct.members[1]));

    unsigned int num =
      get_bv(object, is_signedbv_type(strct.members[0])).to_uint64();
    unsigned int offs =
      get_bv(offset, is_signedbv_type(strct.members[1])).to_uint64();
    pointer_logict::pointert p(num, BigInt(offs));
    return pointer_logic.back().pointer_expr(p, expr->type);
  }

  // Otherwise, run through all fields and dispatch to 'get' again.
  std::vector<expr2tc> outmem;
  unsigned int i = 0;
  for (auto const &it : strct.members)
  {
    expr2tc memb = member2tc(it, expr, strct.member_names[i]);
    outmem.push_back(get(memb));
    i++;
  }

  return constant_struct2tc(expr->type, std::move(outmem));
}

// ***************************** 'get' api *******************************

bool z3_convt::get_bool(smt_astt a)
{
  const z3_smt_ast *za = to_solver_smt_ast<z3_smt_ast>(a);
  // Set the model_completion to TRUE.
  // Z3 will assign an interpretation to the Boolean constants,
  // which are essentially don't cares.
  z3::expr e = solver.get_model().eval(za->a, true);

  Z3_lbool result = Z3_get_bool_value(z3_ctx, e);

  bool res;
  switch (result)
  {
  case Z3_L_TRUE:
    res = true;
    break;
  case Z3_L_FALSE:
    res = false;
    break;
  default:
    log_error("Can't get boolean value from Z3");
    abort();
  }

  return res;
}

BigInt z3_convt::get_bv(smt_astt a, bool is_signed)
{
  const z3_smt_ast *za = to_solver_smt_ast<z3_smt_ast>(a);
  z3::expr e = solver.get_model().eval(za->a, true);

  if (int_encoding)
    return string2integer(Z3_get_numeral_string(z3_ctx, e));

  // Not a numeral? Let's not try to convert it
  std::string bin;
  bool is_numeral [[maybe_unused]] = e.as_binary(bin);
  assert(is_numeral);
  /* 'bin' contains the ascii representation of the bit-vector, msb-first,
   * no leading zeroes; zero-extend if possible */
  if (bin.size() < e.get_sort().bv_size())
    bin.insert(bin.begin(), '0');
  return binary2integer(bin, is_signed);
}

ieee_floatt z3_convt::get_fpbv(smt_astt a)
{
  const z3_smt_ast *za = to_solver_smt_ast<z3_smt_ast>(a);
  z3::expr e = solver.get_model().eval(za->a, true);

  assert(Z3_get_ast_kind(z3_ctx, e) == Z3_APP_AST);

  unsigned ew = Z3_fpa_get_ebits(z3_ctx, e.get_sort());

  // Remove an extra bit added when creating the sort,
  // because we represent the hidden bit like Z3 does
  unsigned sw = Z3_fpa_get_sbits(z3_ctx, e.get_sort()) - 1;

  ieee_floatt number(ieee_float_spect(sw, ew));
  number.make_zero();

  if (Z3_fpa_is_numeral_nan(z3_ctx, e))
    number.make_NaN();
  else if (Z3_fpa_is_numeral_inf(z3_ctx, e))
  {
    if (Z3_fpa_is_numeral_positive(z3_ctx, e))
      number.make_plus_infinity();
    else
      number.make_minus_infinity();
  }
  else
  {
    Z3_ast v;
    if (Z3_model_eval(
          z3_ctx, solver.get_model(), Z3_mk_fpa_to_ieee_bv(z3_ctx, e), 1, &v))
      number.unpack(BigInt(Z3_get_numeral_string(z3_ctx, v)));
  }

  return number;
}

expr2tc
z3_convt::get_array_elem(smt_astt array, uint64_t index, const type2tc &subtype)
{
  const z3_smt_ast *za = to_solver_smt_ast<z3_smt_ast>(array);
  unsigned long array_bound = array->sort->get_domain_width();
  const z3_smt_ast *idx;
  if (int_encoding)
    idx = to_solver_smt_ast<z3_smt_ast>(mk_smt_int(BigInt(index)));
  else
    idx = to_solver_smt_ast<z3_smt_ast>(
      mk_smt_bv(BigInt(index), mk_bv_sort(array_bound)));

  z3::expr e = solver.get_model().eval(select(za->a, idx->a), true);
  return get_by_ast(subtype, new_ast(e, convert_sort(subtype)));
}

expr2tc z3_convt::tuple_get_array_elem(
  smt_astt array,
  uint64_t index,
  const type2tc &subtype)
{
  return get_array_elem(array, index, get_flattened_array_subtype(subtype));
}

void z3_smt_ast::dump() const
{
  std::string ast(Z3_ast_to_string(a.ctx(), a));
  log_status(
    "{}\nsort is {}", ast, Z3_sort_to_string(a.ctx(), Z3_get_sort(a.ctx(), a)));
}

void z3_convt::dump_smt()
{
  const std::string &path = options.get_option("output");
  /* the iostream API is just unusable */
  if (path == "-")
    print_smt_formulae(std::cout);
  else
  {
    std::ofstream out(path);
    print_smt_formulae(out);
  }
}

void z3_convt::print_smt_formulae(std::ostream &dest)
{
  /*
   * HACK: ESBMC uses backslash (\) for some of its symbols, this is fine for
   * Z3 but not for all solvers. Maybe we should consider using another naming
   * for those symbols
   */
  auto smt_formula = solver.to_smt2();
  std::replace(smt_formula.begin(), smt_formula.end(), '\\', '_');

  Z3_ast_vector __z3_assertions = Z3_solver_get_assertions(z3_ctx, solver);
  // Add whatever logic is needed.
  // Add solver specific declarations as well.
  dest << "(set-info :smt-lib-version 2.6)\n";
  dest << "(set-option :produce-models true)\n";
  dest << "; Asserts from ESMBC starts\n";
  dest << smt_formula; // All VCC conditions in SMTLIB format.
  dest << "; Asserts from ESMBC ends\n";
  dest << "(get-model)\n";
  dest << "(exit)\n";
  log_status(
    "Total number of safety properties: {}",
    Z3_ast_vector_size(z3_ctx, __z3_assertions));
}

smt_astt z3_convt::mk_smt_fpbv_gt(smt_astt lhs, smt_astt rhs)
{
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_fpa_gt(
        z3_ctx,
        to_solver_smt_ast<z3_smt_ast>(lhs)->a,
        to_solver_smt_ast<z3_smt_ast>(rhs)->a)),
    boolean_sort);
}

smt_astt z3_convt::mk_smt_fpbv_lt(smt_astt lhs, smt_astt rhs)
{
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_fpa_lt(
        z3_ctx,
        to_solver_smt_ast<z3_smt_ast>(lhs)->a,
        to_solver_smt_ast<z3_smt_ast>(rhs)->a)),
    boolean_sort);
}

smt_astt z3_convt::mk_smt_fpbv_gte(smt_astt lhs, smt_astt rhs)
{
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_fpa_geq(
        z3_ctx,
        to_solver_smt_ast<z3_smt_ast>(lhs)->a,
        to_solver_smt_ast<z3_smt_ast>(rhs)->a)),
    boolean_sort);
}

smt_astt z3_convt::mk_smt_fpbv_lte(smt_astt lhs, smt_astt rhs)
{
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_fpa_leq(
        z3_ctx,
        to_solver_smt_ast<z3_smt_ast>(lhs)->a,
        to_solver_smt_ast<z3_smt_ast>(rhs)->a)),
    boolean_sort);
}

smt_astt z3_convt::mk_smt_fpbv_neg(smt_astt op)
{
  return new_ast(-to_solver_smt_ast<z3_smt_ast>(op)->a, op->sort);
}

void z3_convt::print_model()
{
  log_status("{}", Z3_model_to_string(z3_ctx, solver.get_model()));
}

smt_sortt z3_convt::mk_fpbv_sort(const unsigned ew, const unsigned sw)
{
  // We need to add an extra bit to the significand size,
  // as it has no hidden bit
  return new solver_smt_sort<z3::sort>(
    SMT_SORT_FPBV, z3_ctx.fpa_sort(ew, sw + 1), ew + sw + 1, sw + 1);
}

smt_sortt z3_convt::mk_fpbv_rm_sort()
{
  return new solver_smt_sort<z3::sort>(
    SMT_SORT_FPBV_RM,
    z3::sort(z3_ctx, Z3_mk_fpa_rounding_mode_sort(z3_ctx)),
    3);
}

smt_sortt z3_convt::mk_bvfp_sort(std::size_t ew, std::size_t sw)
{
  return new solver_smt_sort<z3::sort>(
    SMT_SORT_BVFP, z3_ctx.bv_sort(ew + sw + 1), ew + sw + 1, sw + 1);
}

smt_sortt z3_convt::mk_bvfp_rm_sort()
{
  return new solver_smt_sort<z3::sort>(SMT_SORT_BVFP_RM, z3_ctx.bv_sort(3), 3);
}

smt_sortt z3_convt::mk_bool_sort()
{
  return new solver_smt_sort<z3::sort>(SMT_SORT_BOOL, z3_ctx.bool_sort(), 1);
}

smt_sortt z3_convt::mk_real_sort()
{
  return new solver_smt_sort<z3::sort>(SMT_SORT_REAL, z3_ctx.real_sort());
}

smt_sortt z3_convt::mk_int_sort()
{
  return new solver_smt_sort<z3::sort>(SMT_SORT_INT, z3_ctx.int_sort());
}

smt_sortt z3_convt::mk_bv_sort(std::size_t width)
{
  return new solver_smt_sort<z3::sort>(
    SMT_SORT_BV, z3_ctx.bv_sort(width), width);
}

smt_sortt z3_convt::mk_fbv_sort(std::size_t width)
{
  return new solver_smt_sort<z3::sort>(
    SMT_SORT_FIXEDBV, z3_ctx.bv_sort(width), width);
}

smt_sortt z3_convt::mk_array_sort(smt_sortt domain, smt_sortt range)
{
  auto domain_sort = to_solver_smt_sort<z3::sort>(domain);
  auto range_sort = to_solver_smt_sort<z3::sort>(range);

  auto t = z3_ctx.array_sort(domain_sort->s, range_sort->s);
  return new solver_smt_sort<z3::sort>(
    SMT_SORT_ARRAY, t, domain->get_data_width(), range);
}

smt_astt z3_convt::mk_from_bv_to_fp(smt_astt op, smt_sortt to)
{
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_fpa_to_fp_bv(
        z3_ctx,
        to_solver_smt_ast<z3_smt_ast>(op)->a,
        to_solver_smt_sort<z3::sort>(to)->s)),
    to);
}

smt_astt z3_convt::mk_from_fp_to_bv(smt_astt op)
{
  smt_sortt to = mk_bvfp_sort(
    op->sort->get_exponent_width(), op->sort->get_significand_width() - 1);
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_fpa_to_ieee_bv(z3_ctx, to_solver_smt_ast<z3_smt_ast>(op)->a)),
    to);
}

smt_astt
z3_convt::mk_smt_typecast_from_fpbv_to_ubv(smt_astt from, std::size_t width)
{
  // Conversion from float to integers always truncate, so we assume
  // the round mode to be toward zero
  const z3_smt_ast *mrm =
    to_solver_smt_ast<z3_smt_ast>(mk_smt_fpbv_rm(ieee_floatt::ROUND_TO_ZERO));
  const z3_smt_ast *mfrom = to_solver_smt_ast<z3_smt_ast>(from);

  return new_ast(
    z3::to_expr(z3_ctx, Z3_mk_fpa_to_ubv(z3_ctx, mrm->a, mfrom->a, width)),
    mk_bv_sort(width));
}

smt_astt
z3_convt::mk_smt_typecast_from_fpbv_to_sbv(smt_astt from, std::size_t width)
{
  // Conversion from float to integers always truncate, so we assume
  // the round mode to be toward zero
  const z3_smt_ast *mrm =
    to_solver_smt_ast<z3_smt_ast>(mk_smt_fpbv_rm(ieee_floatt::ROUND_TO_ZERO));
  const z3_smt_ast *mfrom = to_solver_smt_ast<z3_smt_ast>(from);

  return new_ast(
    z3::to_expr(z3_ctx, Z3_mk_fpa_to_sbv(z3_ctx, mrm->a, mfrom->a, width)),
    mk_bv_sort(width));
}

smt_astt z3_convt::mk_smt_typecast_from_fpbv_to_fpbv(
  smt_astt from,
  smt_sortt to,
  smt_astt rm)
{
  const z3_smt_ast *mrm = to_solver_smt_ast<z3_smt_ast>(rm);
  const z3_smt_ast *mfrom = to_solver_smt_ast<z3_smt_ast>(from);
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_fpa_to_fp_float(
        z3_ctx, mrm->a, mfrom->a, to_solver_smt_sort<z3::sort>(to)->s)),
    to);
}

smt_astt
z3_convt::mk_smt_typecast_ubv_to_fpbv(smt_astt from, smt_sortt to, smt_astt rm)
{
  const z3_smt_ast *mrm = to_solver_smt_ast<z3_smt_ast>(rm);
  const z3_smt_ast *mfrom = to_solver_smt_ast<z3_smt_ast>(from);
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_fpa_to_fp_unsigned(
        z3_ctx, mrm->a, mfrom->a, to_solver_smt_sort<z3::sort>(to)->s)),
    to);
}

smt_astt
z3_convt::mk_smt_typecast_sbv_to_fpbv(smt_astt from, smt_sortt to, smt_astt rm)
{
  const z3_smt_ast *mrm = to_solver_smt_ast<z3_smt_ast>(rm);
  const z3_smt_ast *mfrom = to_solver_smt_ast<z3_smt_ast>(from);
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_fpa_to_fp_signed(
        z3_ctx, mrm->a, mfrom->a, to_solver_smt_sort<z3::sort>(to)->s)),
    to);
}

smt_astt z3_convt::mk_smt_nearbyint_from_float(smt_astt from, smt_astt rm)
{
  const z3_smt_ast *mrm = to_solver_smt_ast<z3_smt_ast>(rm);
  const z3_smt_ast *mfrom = to_solver_smt_ast<z3_smt_ast>(from);
  return new_ast(
    z3::to_expr(z3_ctx, Z3_mk_fpa_round_to_integral(z3_ctx, mrm->a, mfrom->a)),
    from->sort);
}

smt_astt z3_convt::mk_smt_fpbv_sqrt(smt_astt rd, smt_astt rm)
{
  const z3_smt_ast *mrm = to_solver_smt_ast<z3_smt_ast>(rm);
  const z3_smt_ast *mrd = to_solver_smt_ast<z3_smt_ast>(rd);
  return new_ast(
    z3::to_expr(z3_ctx, Z3_mk_fpa_sqrt(z3_ctx, mrm->a, mrd->a)), rd->sort);
}

smt_astt
z3_convt::mk_smt_fpbv_fma(smt_astt v1, smt_astt v2, smt_astt v3, smt_astt rm)
{
  const z3_smt_ast *mrm = to_solver_smt_ast<z3_smt_ast>(rm);
  const z3_smt_ast *mv1 = to_solver_smt_ast<z3_smt_ast>(v1);
  const z3_smt_ast *mv2 = to_solver_smt_ast<z3_smt_ast>(v2);
  const z3_smt_ast *mv3 = to_solver_smt_ast<z3_smt_ast>(v3);
  return new_ast(
    z3::to_expr(z3_ctx, Z3_mk_fpa_fma(z3_ctx, mrm->a, mv1->a, mv2->a, mv3->a)),
    v1->sort);
}

smt_astt z3_convt::mk_smt_fpbv_add(smt_astt lhs, smt_astt rhs, smt_astt rm)
{
  const z3_smt_ast *mlhs = to_solver_smt_ast<z3_smt_ast>(lhs);
  const z3_smt_ast *mrhs = to_solver_smt_ast<z3_smt_ast>(rhs);
  const z3_smt_ast *mrm = to_solver_smt_ast<z3_smt_ast>(rm);
  return new_ast(
    z3::to_expr(z3_ctx, Z3_mk_fpa_add(z3_ctx, mrm->a, mlhs->a, mrhs->a)),
    lhs->sort);
}

smt_astt z3_convt::mk_smt_fpbv_sub(smt_astt lhs, smt_astt rhs, smt_astt rm)
{
  const z3_smt_ast *mlhs = to_solver_smt_ast<z3_smt_ast>(lhs);
  const z3_smt_ast *mrhs = to_solver_smt_ast<z3_smt_ast>(rhs);
  const z3_smt_ast *mrm = to_solver_smt_ast<z3_smt_ast>(rm);
  return new_ast(
    z3::to_expr(z3_ctx, Z3_mk_fpa_sub(z3_ctx, mrm->a, mlhs->a, mrhs->a)),
    lhs->sort);
}

smt_astt z3_convt::mk_smt_fpbv_mul(smt_astt lhs, smt_astt rhs, smt_astt rm)
{
  const z3_smt_ast *mlhs = to_solver_smt_ast<z3_smt_ast>(lhs);
  const z3_smt_ast *mrhs = to_solver_smt_ast<z3_smt_ast>(rhs);
  const z3_smt_ast *mrm = to_solver_smt_ast<z3_smt_ast>(rm);
  return new_ast(
    z3::to_expr(z3_ctx, Z3_mk_fpa_mul(z3_ctx, mrm->a, mlhs->a, mrhs->a)),
    lhs->sort);
}

smt_astt z3_convt::mk_smt_fpbv_div(smt_astt lhs, smt_astt rhs, smt_astt rm)
{
  const z3_smt_ast *mlhs = to_solver_smt_ast<z3_smt_ast>(lhs);
  const z3_smt_ast *mrhs = to_solver_smt_ast<z3_smt_ast>(rhs);
  const z3_smt_ast *mrm = to_solver_smt_ast<z3_smt_ast>(rm);
  return new_ast(
    z3::to_expr(z3_ctx, Z3_mk_fpa_div(z3_ctx, mrm->a, mlhs->a, mrhs->a)),
    lhs->sort);
}

smt_astt z3_convt::mk_smt_fpbv_eq(smt_astt lhs, smt_astt rhs)
{
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_fpa_eq(
        z3_ctx,
        to_solver_smt_ast<z3_smt_ast>(lhs)->a,
        to_solver_smt_ast<z3_smt_ast>(rhs)->a)),
    boolean_sort);
}

smt_astt z3_convt::mk_smt_fpbv_is_nan(smt_astt op)
{
  return new_ast(
    z3::to_expr(
      z3_ctx, Z3_mk_fpa_is_nan(z3_ctx, to_solver_smt_ast<z3_smt_ast>(op)->a)),
    boolean_sort);
}

smt_astt z3_convt::mk_smt_fpbv_is_inf(smt_astt op)
{
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_fpa_is_infinite(z3_ctx, to_solver_smt_ast<z3_smt_ast>(op)->a)),
    boolean_sort);
}

smt_astt z3_convt::mk_smt_fpbv_is_normal(smt_astt op)
{
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_fpa_is_normal(z3_ctx, to_solver_smt_ast<z3_smt_ast>(op)->a)),
    boolean_sort);
}

smt_astt z3_convt::mk_smt_fpbv_is_zero(smt_astt op)
{
  return new_ast(
    z3::to_expr(
      z3_ctx, Z3_mk_fpa_is_zero(z3_ctx, to_solver_smt_ast<z3_smt_ast>(op)->a)),
    boolean_sort);
}

smt_astt z3_convt::mk_smt_fpbv_is_negative(smt_astt op)
{
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_fpa_is_negative(z3_ctx, to_solver_smt_ast<z3_smt_ast>(op)->a)),
    boolean_sort);
}

smt_astt z3_convt::mk_smt_fpbv_is_positive(smt_astt op)
{
  return new_ast(
    z3::to_expr(
      z3_ctx,
      Z3_mk_fpa_is_positive(z3_ctx, to_solver_smt_ast<z3_smt_ast>(op)->a)),
    boolean_sort);
}

smt_astt z3_convt::mk_smt_fpbv_abs(smt_astt op)
{
  return new_ast(
    z3::to_expr(
      z3_ctx, Z3_mk_fpa_abs(z3_ctx, to_solver_smt_ast<z3_smt_ast>(op)->a)),
    op->sort);
}
