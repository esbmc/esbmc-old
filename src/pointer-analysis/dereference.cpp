#include <cassert>
#include <langapi/language_util.h>
#include <pointer-analysis/dereference.h>
#include <pointer-analysis/value_set.h>
#include <sstream>
#include <util/arith_tools.h>
#include <util/array_name.h>
#include <util/base_type.h>
#include <util/c_misc.h>
#include <util/c_typecast.h>
#include <util/c_types.h>
#include <util/config.h>
#include <util/cprover_prefix.h>
#include <util/expr_util.h>
#include <util/i2string.h>
#include <irep2/irep2.h>
#include <util/message/format.h>
#include <util/migrate.h>
#include <util/prefix.h>
#include <util/pretty.h>
#include <util/rename.h>
#include <util/std_expr.h>
#include <util/type_byte_size.h>

// global data, horrible
unsigned int dereferencet::invalid_counter = 0;

// Look for the base of an expression such as &a->b[1];, where all we're doing
// is performing some pointer arithmetic, rather than actually performing some
// dereference operation.
static inline expr2tc get_base_dereference(const expr2tc &e)
{
  // XXX -- do we need to consider if2t's? And how?
  if (is_member2t(e))
  {
    return get_base_dereference(to_member2t(e).source_value);
  }
  if (is_index2t(e) && is_pointer_type(to_index2t(e).source_value))
  {
    return e;
  }
  else if (is_index2t(e))
  {
    return get_base_dereference(to_index2t(e).source_value);
  }
  else if (is_dereference2t(e))
  {
    return to_dereference2t(e).value;
  }
  else
  {
    return expr2tc();
  }
}

static inline expr2tc replace_dyn_offset_with_zero(const expr2tc &e)
{
  // Knowing the offset value is important when we try to
  // extract a value that is not aligned to a byte (e.g., suppose we have
  // a struct {unsigned field1 : 7; unsigned field2 : 10}, and to correctly
  // extract field2 we need to extract 3 bytes as field2 spans over 3 bytes
  // of the struct). Otherwise, the total number of bytes is completely
  // defined by its type size (i.e., type_byte_size_bits). If we are dealing
  // with a dynamic offset, we can make some reasonable assumptions.
  // Since the symbolic part of the dynamic_offset cannot encode an address
  // of a bit-field, we can safely assume that it is always aligned to a byte,
  // and we can replace the symbolic part with 0, thus obtaining the constant
  // offset to the field within the inner-most struct.
  // And this is all that's required to correctly calculate the number of bytes
  // occupied by a bit-field member.

  if (is_add2t(e))
    return add2tc(
      e->type,
      replace_dyn_offset_with_zero(to_add2t(e).side_1),
      replace_dyn_offset_with_zero(to_add2t(e).side_2));

  if (is_sub2t(e))
    return sub2tc(
      e->type,
      replace_dyn_offset_with_zero(to_sub2t(e).side_1),
      replace_dyn_offset_with_zero(to_sub2t(e).side_2));

  if (is_mul2t(e))
    return mul2tc(
      e->type,
      replace_dyn_offset_with_zero(to_mul2t(e).side_1),
      replace_dyn_offset_with_zero(to_mul2t(e).side_2));

  if (is_div2t(e))
    return div2tc(
      e->type,
      replace_dyn_offset_with_zero(to_div2t(e).side_1),
      replace_dyn_offset_with_zero(to_div2t(e).side_2));

  if (is_pointer_offset2t(e))
    return gen_long(e->type, 0);

  if (is_constant_int2t(e))
    return e;

  // If it is none of the above, just return 0
  return gen_long(e->type, 0);
}

bool dereferencet::has_dereference(const expr2tc &expr) const
{
  if (is_nil_expr(expr))
    return false;

  // Check over each operand,
  bool result = false;
  expr->foreach_operand([this, &result](const expr2tc &e) {
    if (has_dereference(e))
      result = true;
  });

  // If a derefing operand is found, return true.
  if (result == true)
    return true;

  if (
    is_dereference2t(expr) ||
    (is_index2t(expr) && is_pointer_type(to_index2t(expr).source_value)))
    return true;

  return false;
}

const expr2tc &dereferencet::get_symbol(const expr2tc &expr)
{
  if (is_member2t(expr))
    return get_symbol(to_member2t(expr).source_value);
  if (is_index2t(expr))
    return get_symbol(to_index2t(expr).source_value);

  return expr;
}

/************************* Expression decomposing code ************************/

void dereferencet::dereference_expr(expr2tc &expr, guardt &guard, modet mode)
{
  if (!has_dereference(expr))
    return;

  switch (expr->expr_id)
  {
  case expr2t::and_id:
  case expr2t::or_id:
  case expr2t::if_id:
    dereference_guard_expr(expr, guard, mode);
    break;

  case expr2t::address_of_id:
    dereference_addrof_expr(expr, guard, mode);
    break;

  case expr2t::dereference_id:
  {
    /* Interpret an actual dereference expression. First dereferences the
     * pointer expression, then dereferences the pointer itself, and stores the
     * result in 'expr'. */
    assert(is_dereference2t(expr));
    dereference2t &deref = to_dereference2t(expr);
    // first make sure there are no dereferences in there
    dereference_expr(deref.value, guard, dereferencet::READ);

    expr2tc tmp_obj = deref.value;
    expr2tc result = dereference(tmp_obj, deref.type, guard, mode, expr2tc());
    expr = result;
    break;
  }

  case expr2t::index_id:
  case expr2t::member_id:
  {
    // The result of this expression should be scalar: we're transitioning
    // from a scalar result to a nonscalar result.

    expr2tc res = dereference_expr_nonscalar(expr, guard, mode, expr);

    // If a dereference successfully occurred, replace expr at this level.
    // XXX -- explain this better.
    if (!is_nil_expr(res))
      expr = res;
    break;
  }

  default:
  {
    // Recurse over the operands
    expr->Foreach_operand([this, &guard, &mode](expr2tc &e) {
      if (is_nil_expr(e))
        return;
      dereference_expr(e, guard, mode);
    });
    break;
  }
  }
}

void dereferencet::dereference_guard_expr(
  expr2tc &expr,
  guardt &guard,
  modet mode)
{
  if (is_and2t(expr) || is_or2t(expr))
  {
    // If this is an and or or expression, then if the first operand short
    // circuits the truth of the expression, then we shouldn't evaluate the
    // second expression. That means that the dereference assertions in the
    // 2nd should be guarded with the fact that the first operand didn't short
    // circuit.
    assert(is_bool_type(expr));

    // Take the current size of the guard, so that we can reset it later.
    guardt old_guards(guard);

    expr->Foreach_operand([this, &guard, &expr](expr2tc &op) {
      assert(is_bool_type(op));

      // Handle any derererences in this operand
      if (has_dereference(op))
        dereference_expr(op, guard, dereferencet::READ);

      // Guard the next operand against this operand short circuiting us.
      if (is_or2t(expr))
      {
        expr2tc tmp = not2tc(op);
        guard.add(tmp);
      }
      else
      {
        guard.add(op);
      }
    });

    // Reset guard to where it was.
    guard.swap(old_guards);
    return;
  }

  assert(is_if2t(expr));
  // Only one side of this if gets evaluated according to the condition, which
  // means that pointer dereference assertion failures should have the
  // relevant guard applied. This makes sure they don't fire even when their
  // expression isn't evaluated.
  if2t &ifref = to_if2t(expr);
  dereference_expr(ifref.cond, guard, dereferencet::READ);

  bool o1 = has_dereference(ifref.true_value);
  bool o2 = has_dereference(ifref.false_value);

  if (o1)
  {
    guardt old_guards(guard);
    guard.add(ifref.cond);
    dereference_expr(ifref.true_value, guard, mode);
    guard.swap(old_guards);
  }

  if (o2)
  {
    guardt old_guards(guard);
    expr2tc tmp = not2tc(ifref.cond);
    guard.add(tmp);
    dereference_expr(ifref.false_value, guard, mode);
    guard.swap(old_guards);
  }

  return;
}

void dereferencet::dereference_addrof_expr(
  expr2tc &expr,
  guardt &guard,
  modet mode)
{
  // Crazy combinations of & and * that don't actually lead to a deref:

  // turn &*p to p
  // this has *no* side effect!
  address_of2t &addrof = to_address_of2t(expr);

  if (is_dereference2t(addrof.ptr_obj))
  {
    dereference2t &deref = to_dereference2t(addrof.ptr_obj);
    expr2tc result = deref.value;

    if (result->type != expr->type)
      result = typecast2tc(expr->type, result);

    expr = result;
  }
  else
  {
    // This might, alternately, be a chain of member and indexes applied to
    // a dereference. In which case what we're actually doing is computing
    // some pointer arith, manually.
    expr2tc base = get_base_dereference(addrof.ptr_obj);
    if (!is_nil_expr(base))
    {
      //  We have a base. There may be additional dereferences in it.
      dereference_expr(base, guard, mode);
      // Now compute the pointer offset involved.
      expr2tc offs = compute_pointer_offset(addrof.ptr_obj);
      assert(
        !is_nil_expr(offs) &&
        "Pointer offset of index/member "
        "combination should be valid int");

      offs = typecast2tc(pointer_type2(), offs);

      // Cast to a byte pointer; add; cast back. Is essentially pointer arith.
      expr2tc output = typecast2tc(pointer_type2tc(get_uint8_type()), base);
      output = add2tc(output->type, output, offs);
      output = typecast2tc(expr->type, output);
      expr = output;
    }
    else
    {
      // It's not something that we can simplify from &foo->bar[baz] to not have
      // a dereference, but might still contain a dereference.
      dereference_expr(addrof.ptr_obj, guard, mode);
    }
  }

  // We modified this expression, but, we might have injected some pointer
  // arithmetic that contains another dereference. So we need to re-deref this
  // new expression.
  dereference_expr(expr, guard, mode);
}

static bool is_aligned_member(const expr2tc &expr)
{
  if (!is_member2t(expr))
    return false;

  const expr2tc &structure = to_member2t(expr).source_value;
  auto *ty = static_cast<const struct_union_data *>(structure->type.get());

  if (ty->packed)
  {
    /* Very (too?) conservative approach: all members of packed structures are to
     * be accessed in a known-unaligned way. Note, that's not true for GCC/Clang:
     * if they can prove some member is always aligned, they'll use the faster
     * instructions on aligned pointers. */
    return false;
  }

  /* non-packed structures have all members aligned
   *
   * TODO: This holds true only for non-padding members as padding is not
   *       actually a member. We just treat it as one, which here is wrong. */
  return true;
}

expr2tc dereferencet::dereference_expr_nonscalar(
  expr2tc &expr,
  guardt &guard,
  modet mode,
  const expr2tc &base)
{
  if (is_dereference2t(expr))
  {
    /* The first expression we're called with is index2t, member2t or non-scalar
     * if2t. Thus, expr differs from base. */
    assert(expr != base);

    // Check that either the base type that these steps are applied to matches
    // the type of the object we're wrapping in these steps. It's a type error
    // if there isn't a match.
    type2tc base_of_steps_type = ns.follow(expr->type);
    if (!dereference_type_compare(expr, base_of_steps_type))
    {
      // The base types are incompatible.
      bad_base_type_failure(
        guard, get_type_id(*expr->type), get_type_id(*base_of_steps_type));
      return expr2tc();
    }

    // Determine offset accumulated to this point (in bits)
    expr2tc offset_to_scalar = compute_pointer_offset_bits(base, &ns);
    simplify(offset_to_scalar);

    dereference2t &deref = to_dereference2t(expr);
    // first make sure there are no dereferences in there
    dereference_expr(deref.value, guard, dereferencet::READ);

    return dereference(deref.value, base->type, guard, mode, offset_to_scalar);
  }

  if (is_typecast2t(expr))
  {
    // Just blast straight through
    return dereference_expr_nonscalar(
      to_typecast2t(expr).from, guard, mode, base);
  }

  if (is_member2t(expr))
  {
    member2t &member = to_member2t(expr);
    expr2tc &structure = member.source_value;
    if (
      !options.get_bool_option("no-align-check") && !mode.unaligned &&
      !is_aligned_member(expr))
    {
      auto *t = static_cast<const struct_union_data *>(structure->type.get());
      log_warning(
        "not checking alignment for access to packed {} {}",
        get_type_id(*structure->type),
        t->name.as_string());
      mode.unaligned = true;
    }
    return dereference_expr_nonscalar(structure, guard, mode, base);
  }

  if (is_index2t(expr))
  {
    index2t &index = to_index2t(expr);
    dereference_expr(index.index, guard, dereferencet::READ);
    return dereference_expr_nonscalar(index.source_value, guard, mode, base);
  }

  if (is_constant_union2t(expr))
  {
    constant_union2t &u = to_constant_union2t(expr);
    /* In the frontend (until the SMT counter-example), constant union
     * expressions should have a single initializer expression, see also the
     * comment for constant_union2t in <irep2/itep2_expr.h>. */
    assert(u.datatype_members.size() == 1);
    assert(!is_write(mode));
    return dereference_expr_nonscalar(
      u.datatype_members.front(), guard, mode, base);
  }

  // there should be no sudden transition back to scalars, except through
  // dereferences. Return nil to indicate that there was no dereference at
  // the bottom of this.
  assert(!is_scalar_type(expr));
  assert(is_constant_expr(expr) || is_symbol2t(expr));
  assert(!has_dereference(expr));

  return expr2tc();
}

/********************** Intermediate reference munging code *******************/

expr2tc dereferencet::dereference(
  const expr2tc &orig_src,
  const type2tc &to_type,
  const guardt &guard,
  modet mode,
  const expr2tc &lexical_offset)
{
  internal_items.clear();

  // Awkwardly, the pointer might not be of pointer type, for example with
  // nested dereferences that point at crazy locations. Happily this is not
  // a real problem: just cast to a pointer, and let the dereference handlers
  // cope with the fact that this expression doesn't point at anything. Of
  // course, if it does point at something, dereferencing continues.
  expr2tc src = orig_src;
  if (!is_pointer_type(orig_src))
    src = typecast2tc(pointer_type2tc(get_empty_type()), src);

  type2tc type = ns.follow(to_type);

  // collect objects dest may point to
  value_setst::valuest points_to_set;

  dereference_callback.get_value_set(src, points_to_set);

  // now build big case split
  // only "good" objects

  /* If the value-set contains unknown or invalid, we cannot be sure it contains
   * all possible values and we have to add a fallback symbol in case all guards
   * evaluate to false. On the other hand when it is exhaustive, we only need to
   * encode (n-1) guards for the n values in the if-then-else chain below. This
   * is done by leaving 'value' initially empty.
   *
   * XXX fbrausse: get_value_set() should compute this information */
  bool known_exhaustive = true;
  for (const expr2tc &target : points_to_set)
    known_exhaustive &= !(is_unknown2t(target) || is_invalid2t(target));

  expr2tc value;
  if (!known_exhaustive)
    value = make_failed_symbol(type);

  for (const expr2tc &target : points_to_set)
  {
    expr2tc new_value, pointer_guard;

    new_value = build_reference_to(
      target, mode, src, type, guard, lexical_offset, pointer_guard);

    if (is_nil_expr(new_value))
      continue;

    assert(!is_nil_expr(pointer_guard));

    if (!dereference_type_compare(new_value, type))
    {
      guardt new_guard(guard);
      new_guard.add(pointer_guard);
      bad_base_type_failure(
        new_guard, get_type_id(*type), get_type_id(*new_value->type));
      continue;
    }

    // Chain a big if-then-else case.
    if (is_nil_expr(value))
      value = new_value;
    else
      value = if2tc(type, pointer_guard, new_value, value);
  }

  if (is_internal(mode))
  {
    // Deposit internal values with the caller, then clear.
    dereference_callback.dump_internal_state(internal_items);
    internal_items.clear();
  }
  else if (is_nil_expr(value))
  {
    /* Fallback if dereference failes entirely: to make this a valid formula,
     * return a failed symbol, so that this assignment gets a well typed free
     * value. */
    value = make_failed_symbol(type);
  }

  return value;
}

expr2tc dereferencet::make_failed_symbol(const type2tc &out_type)
{
  type2tc the_type = out_type;

  // else, do new symbol
  symbolt symbol;
  symbol.id = "symex::invalid_object" + i2string(invalid_counter++);
  symbol.name = "invalid_object";
  symbol.type = migrate_type_back(the_type);

  // make it a lvalue, so we can assign to it
  symbol.lvalue = true;

  get_new_name(symbol, ns);

  symbolt *s = nullptr;
  new_context.move(symbol, s);
  assert(s != nullptr);

  // Due to migration hiccups, migration must occur after the symbol
  // appears in the symbol table.
  namespacet new_ns(new_context);
  const namespacet *old_ns = std::exchange(migrate_namespace_lookup, &new_ns);

  expr2tc value;
  migrate_expr(symbol_expr(*s), value);
  migrate_namespace_lookup = old_ns;

  return value;
}

bool dereferencet::dereference_type_compare(
  expr2tc &object,
  const type2tc &dereference_type) const
{
  const type2tc object_type = object->type;

  // Test for simple equality
  if (object->type == dereference_type)
    return true;

  // Check for C++ subclasses; we can cast derived up to base safely.
  if (is_struct_type(object) && is_struct_type(dereference_type))
  {
    if (is_subclass_of(object->type, dereference_type, ns))
    {
      object = typecast2tc(dereference_type, object);
      return true;
    }
  }

  if (is_code_type(object) && is_code_type(dereference_type))
    return true;

  // check for struct prefixes

  type2tc ot_base(object_type), dt_base(dereference_type);

  base_type(ot_base, ns);
  base_type(dt_base, ns);

  if (is_struct_type(ot_base) && is_struct_type(dt_base))
  {
    typet tmp_ot_base = migrate_type_back(ot_base);
    typet tmp_dt_base = migrate_type_back(dt_base);
    if (to_struct_type(tmp_dt_base).is_prefix_of(to_struct_type(tmp_ot_base)))
    {
      object = typecast2tc(dereference_type, object);
      return true; // ok, dt is a prefix of ot
    }
  }

  // really different

  return false;
}

expr2tc dereferencet::build_reference_to(
  const expr2tc &what,
  modet mode,
  const expr2tc &deref_expr,
  const type2tc &type,
  const guardt &guard,
  const expr2tc &lexical_offset,
  expr2tc &pointer_guard)
{
  expr2tc value;
  pointer_guard = gen_false_expr();

  if (is_unknown2t(what) || is_invalid2t(what))
  {
    deref_invalid_ptr(deref_expr, guard, mode);
    return value;
  }

  if (!is_object_descriptor2t(what))
  {
    log_error("unknown points-to: {}", get_expr_id(what));
    abort();
  }

  const object_descriptor2t &o = to_object_descriptor2t(what);

  const expr2tc &root_object = o.get_root_object();
  const expr2tc &object = o.object;

  if (is_null_object2t(root_object) && !is_free(mode) && !is_internal(mode))
  {
    type2tc nullptrtype = pointer_type2tc(type);
    expr2tc null_ptr = symbol2tc(nullptrtype, "NULL");

    expr2tc pointer_guard = same_object2tc(deref_expr, null_ptr);

    guardt tmp_guard(guard);
    tmp_guard.add(pointer_guard);

    dereference_failure("pointer dereference", "NULL pointer", tmp_guard);

    // Don't build a reference to this. You can't actually access NULL, and the
    // solver will only get confused.
    return value;
  }
  if (is_null_object2t(root_object) && (is_free(mode) || is_internal(mode)))
  {
    // Freeing NULL is completely legit according to C
    return value;
  }

  value = object;

  // Produce a guard that the dereferenced pointer points at this object.
  type2tc ptr_type = pointer_type2tc(object->type);
  expr2tc obj_ptr = address_of2tc(ptr_type, object);
  pointer_guard = same_object2tc(deref_expr, obj_ptr);
  guardt tmp_guard(guard);
  tmp_guard.add(pointer_guard);

  // Check that the object we're accessing is actually alive and valid for this
  // mode.
  valid_check(object, tmp_guard, mode);

  // Don't do anything further if we're freeing things
  if (is_free(mode))
    return expr2tc();

  // Value set tracking emits objects with some cruft built on top of them.
  value = get_base_object(value);

  // Final offset computations start here
  expr2tc final_offset = o.offset;
#if 0
  // FIXME: benchmark this, on tacas.
  dereference_callback.rename(final_offset);
#endif

  // If offset is unknown, or whatever, we have to consider it
  // nondeterministic, and let the reference builders deal with it.
  unsigned int alignment = o.alignment;
  if (!is_constant_int2t(final_offset))
  {
    assert(alignment != 0);

    /* The expression being dereferenced doesn't need to be just a symbol: it
     * might have all kind of things messing with alignment in there. */
    if (!is_symbol2t(deref_expr))
    {
      alignment = 1;
    }

    final_offset =
      pointer_offset2tc(get_int_type(config.ansi_c.address_width), deref_expr);
  }

  type2tc offset_type = bitsize_type2();
  if (final_offset->type != offset_type)
    final_offset = typecast2tc(offset_type, final_offset);

  // Converting final_offset from bytes to bits!
  final_offset =
    mul2tc(final_offset->type, final_offset, gen_long(final_offset->type, 8));

  // Add any offset introduced lexically at the dereference site, i.e. member
  // or index exprs, like foo->bar[3]. If bar is of integer type, we translate
  // that to be a dereference of foo + extra_offset, resulting in an integer.
  if (!is_nil_expr(lexical_offset))
    final_offset = add2tc(final_offset->type, final_offset, lexical_offset);

  // If we're in internal mode, collect all of our data into one struct, insert
  // it into the list of internal data, and then bail. The caller does not want
  // to have a reference built at all.
  if (is_internal(mode))
  {
    dereference_callbackt::internal_item internal;
    internal.object = value;
    // Converting offset to bytes
    internal.offset = typecast2tc(
      signed_size_type2(),
      div2tc(
        final_offset->type, final_offset, gen_long(final_offset->type, 8)));
    internal.guard = pointer_guard;
    internal_items.push_back(internal);
    return expr2tc();
  }

  if (is_code_type(value) || is_code_type(type))
  {
    if (!check_code_access(value, final_offset, type, tmp_guard, mode))
      return expr2tc();
    /* here, both of them are code */
  }
  else if (is_array_type(value)) // Encode some access bounds checks.
  {
    bounds_check(value, final_offset, type, tmp_guard);
  }
  else
  {
    check_data_obj_access(value, final_offset, type, tmp_guard, mode);
  }

  simplify(final_offset);

  // Converting alignment to bits here
  alignment *= 8;

  // Call reference building methods. For the given data object in value,
  // an expression of type type will be constructed that reads from it.
  build_reference_rec(value, final_offset, type, tmp_guard, mode, alignment);

  return value;
}

void dereferencet::deref_invalid_ptr(
  const expr2tc &deref_expr,
  const guardt &guard,
  modet mode)
{
  if (is_internal(mode))
    // The caller just wants a list of references -- ensuring that the correct
    // assertions fire is a problem for something or someone else
    return;

  // constraint that it actually is an invalid pointer
  expr2tc invalid_pointer_expr = invalid_pointer2tc(deref_expr);

  expr2tc validity_test;
  std::string foo;

  // Adjust error message and test depending on the context
  if (is_free(mode))
  {
    // You're allowed to free NULL.
    expr2tc null_ptr = symbol2tc(pointer_type2tc(get_empty_type()), "NULL");
    expr2tc neq = notequal2tc(null_ptr, deref_expr);
    expr2tc and_ = and2tc(neq, invalid_pointer_expr);
    validity_test = and_;
    foo = "invalid pointer freed";
  }
  else
  {
    validity_test = invalid_pointer_expr;
    foo = "invalid pointer";
  }

  // produce new guard

  guardt tmp_guard(guard);
  tmp_guard.add(validity_test);

  dereference_failure("pointer dereference", foo, tmp_guard);
}

/************************** Rereference building code *************************/

enum target_flags
{
  flag_src_scalar = 0,
  flag_src_array = 1,
  flag_src_struct = 2,
  flag_src_union = 3,

  flag_dst_scalar = 0,
  flag_dst_array = 4,
  flag_dst_struct = 8,
  flag_dst_union = 0xC,

  flag_is_const_offs = 0x10,
  flag_is_dyn_offs = 0,
};

/*
 * Legend:
 * - src = value
 * - dst = type
 * - off = offset
 *   - c = constant, constant_int2t
 *   - d = dynamic, any other expr2t
 * - note:
 *   - st = uses stitching via stitch_together_from_byte_array()
 *   - rec = recurses into build_reference_rec()
 *   - rec* = same as rec*, but also restricted recursion into itself or others
 *   - rec' = only restricted recursion into itself
 *
 * src and dst categories:
 * - s: scalar
 * - S: struct
 * - U: union
 * - A: array or string
 * - c: code
 *
 *   src | dst | off | method                                         | note
 *  -----+-----+-----+------------------------------------------------+---------
 *    c  |  *  |  *  | <none>                                         |
 *    *  |  c  |  *  | <none>                                         |
 *  -----+-----+-----+------------------------------------------------+---------
 *    *  |  A  |  *  | <unsupported>: "Can't construct rvalue ref..." |
 *  -----+-----+-----+------------------------------------------------+---------
 *    s  |  s  |  c  | construct_from_const_offset                    | st
 *    S  |  s  |  c  | construct_from_const_struct_offset             | rec
 *    U  |  s  |  c  | <ad-hoc>                                       | rec
 *    A  |  s  |  c  | construct_from_array                           | rec, st
 *  -----+-----+-----+------------------------------------------------+---------
 *    s  |  S  |  c  | <bad>: "Structure pointer pointed at scalar"   |
 *    S  |  S  |  c  | construct_struct_ref_from_const_offset         | rec'
 *    U  |  S  |  c  | <ad-hoc>                                       | rec
 *    A  |  S  |  c  | construct_struct_ref_from_const_offset_array   | rec, st
 *  -----+-----+-----+------------------------------------------------+---------
 *    s  |  U  |  c  | <bad>: "Union pointer pointed at scalar"       |
 *    S  |  U  |  c  | construct_struct_ref_from_const_offset         | rec'
 *    U  |  U  |  c  | construct_struct_ref_from_const_offset         | rec'
 *    A  |  U  |  c  | construct_struct_ref_from_const_offset_array   | rec, st
 *  -----+-----+-----+------------------------------------------------+---------
 *    s  |  s  |  d  | construct_from_dyn_offset                      | st
 *    S  |  s  |  d  | construct_from_dyn_struct_offset               | rec
 *    U  |  s  |  d  | <ad-hoc>                                       | rec
 *    A  |  s  |  d  | construct_from_array                           | rec, st
 *  -----+-----+-----+------------------------------------------------+---------
 *    s  |  S  |  d  | <bad>: "Struct pointer pointed at scalar"      |
 *    S  |  S  |  d  | construct_struct_ref_from_dyn_offset           | rec, st
 *    U  |  S  |  d  | <ad-hoc>                                       | rec
 *    A  |  S  |  d  | construct_struct_ref_from_dyn_offset           | rec, st
 *  -----+-----+-----+------------------------------------------------+---------
 *    s  |  U  |  d  | <bad>: "Union pointer pointed at scalar"       |
 *    S  |  U  |  d  | construct_struct_ref_from_dyn_offset           | rec, st
 *    U  |  U  |  d  | construct_struct_ref_from_dyn_offset           | rec, st
 *    A  |  U  |  d  | construct_struct_ref_from_dyn_offset           | rec, st
 */

void dereferencet::build_reference_rec(
  expr2tc &value,
  const expr2tc &offset,
  const type2tc &type,
  const guardt &guard,
  modet mode,
  unsigned long alignment)
{
  int flags = 0;
  if (is_constant_int2t(offset))
    flags |= flag_is_const_offs;

  // All accesses to code need no further construction
  if (is_code_type(value) || is_code_type(type))
  {
    return;
  }

  if (is_struct_type(type))
    flags |= flag_dst_struct;
  else if (is_union_type(type))
    flags |= flag_dst_union;
  else if (is_scalar_type(type))
    flags |= flag_dst_scalar;
  else if (is_array_type(type))
  {
    std::string sol = options.get_option("sol");
    if (sol.empty())
    {
      log_error(
        "Can't construct rvalue reference to array type during dereference\n"
        "(It isn't allowed by C anyway)\n");
      abort();
    }
  }
  else
  {
    log_error("Unrecognized dest type during dereference\n{}", *type);
    abort();
  }

  if (is_struct_type(value))
    flags |= flag_src_struct;
  else if (is_union_type(value))
    flags |= flag_src_union;
  else if (is_scalar_type(value))
    flags |= flag_src_scalar;
  else if (is_array_type(value))
    flags |= flag_src_array;
  else
  {
    log_error("Unrecognized src type during dereference\n{}", *value->type);
    abort();
  }

  // Consider the myriad of reference construction cases here
  switch (flags)
  {
  case flag_src_scalar | flag_dst_scalar | flag_is_const_offs:
    // Access a scalar from a scalar.
    construct_from_const_offset(value, offset, type);
    break;
  case flag_src_struct | flag_dst_scalar | flag_is_const_offs:
    // Extract a scalar from within a structure
    construct_from_const_struct_offset(value, offset, type, guard, mode);
    break;
  case flag_src_array | flag_dst_scalar | flag_is_const_offs:
    // Extract a scalar from within an array
    construct_from_array(value, offset, type, guard, mode, alignment);
    break;

  case flag_src_scalar | flag_dst_struct | flag_is_const_offs:
    // Attempt to extract a structure from within a scalar. This is not
    // permitted as the base data objects have incompatible types
    dereference_failure(
      "Bad dereference", "Structure pointer pointed at scalar", guard);
    break;
  case flag_src_struct | flag_dst_struct | flag_is_const_offs:
    // Extract a structure from inside another struct.
    construct_struct_ref_from_const_offset(value, offset, type, guard, mode);
    break;
  case flag_src_array | flag_dst_struct | flag_is_const_offs:
    // Extract a structure from inside an array.
    construct_struct_ref_from_const_offset_array(
      value, offset, type, guard, mode, alignment);
    break;

  case flag_src_scalar | flag_dst_union | flag_is_const_offs:
    // Attempt to extract a union from within a scalar. This is not
    // permitted as the base data objects have incompatible types
    dereference_failure(
      "Bad dereference", "Union pointer pointed at scalar", guard);
    break;
  case flag_src_struct | flag_dst_union | flag_is_const_offs:
    // Extract a union from inside a structure.
    construct_struct_ref_from_const_offset(value, offset, type, guard, mode);
    break;
  case flag_src_array | flag_dst_union | flag_is_const_offs:
    // Extract a union from inside an array.
    construct_struct_ref_from_const_offset_array(
      value, offset, type, guard, mode, alignment);
    break;

  case flag_src_scalar | flag_dst_scalar | flag_is_dyn_offs:
    // Access a scalar within a scalar (dyn offset)
    construct_from_dyn_offset(value, offset, type);
    break;
  case flag_src_struct | flag_dst_scalar | flag_is_dyn_offs:
    // Extract a scalar from within a structure (dyn offset)
    construct_from_dyn_struct_offset(
      value, offset, type, guard, alignment, mode);
    break;
  case flag_src_array | flag_dst_scalar | flag_is_dyn_offs:
    // Extract a scalar from within an array (dyn offset)
    construct_from_array(value, offset, type, guard, mode, alignment);
    break;

  case flag_src_scalar | flag_dst_struct | flag_is_dyn_offs:
    // Attempt to extract a structure from within a scalar. This is not
    // permitted as the base data objects have incompatible types
    dereference_failure(
      "Bad dereference", "Struct pointer pointed at scalar", guard);
    break;
  case flag_src_struct | flag_dst_struct | flag_is_dyn_offs:
  case flag_src_array | flag_dst_struct | flag_is_dyn_offs:
    // Extract a structure from inside an array or another struct. Single
    // function supports both (which is bad).
    construct_struct_ref_from_dyn_offset(value, offset, type, guard, mode);
    break;

  case flag_src_scalar | flag_dst_union | flag_is_dyn_offs:
    // Attempt to extract a union from within a scalar. This is not
    // permitted as the base data objects have incompatible types
    dereference_failure(
      "Bad dereference", "Union pointer pointed at scalar", guard);
    break;
  case flag_src_struct | flag_dst_union | flag_is_dyn_offs:
  case flag_src_array | flag_dst_union | flag_is_dyn_offs:
    // Extract a structure from inside an array or another struct. Single
    // function supports both (which is bad).
    construct_struct_ref_from_dyn_offset(value, offset, type, guard, mode);
    break;

  case flag_src_union | flag_dst_union | flag_is_const_offs:
    construct_struct_ref_from_const_offset(value, offset, type, guard, mode);
    break;
  case flag_src_union | flag_dst_union | flag_is_dyn_offs:
    construct_struct_ref_from_dyn_offset(value, offset, type, guard, mode);
    break;

  // All union-src situations are currently approximations
  case flag_src_union | flag_dst_scalar | flag_is_const_offs:
  case flag_src_union | flag_dst_struct | flag_is_const_offs:
  case flag_src_union | flag_dst_scalar | flag_is_dyn_offs:
  case flag_src_union | flag_dst_struct | flag_is_dyn_offs:
  {
    const union_type2t &uni_type = to_union_type(value->type);
    assert(uni_type.members.size() != 0);
    BigInt union_total_size = type_byte_size(value->type);
    // Let's find a member with the biggest size
    size_t selected_member_index = SIZE_MAX;
    for (size_t i = 0; i < uni_type.members.size(); i++)
      if (type_byte_size(uni_type.members[i]) == union_total_size)
      {
        selected_member_index = i;
        break;
      }
    assert(selected_member_index < SIZE_MAX);

    value = member2tc(
      uni_type.members[selected_member_index],
      value,
      uni_type.member_names[selected_member_index]);

    /* type didn't change, it's not an array, so recursion is safe */
    build_reference_rec(value, offset, type, guard, mode, alignment);
    break;
  }

  // No scope for constructing references to arrays
  default:
    log_error("Unrecognized input to build_reference_rec");
    abort();
  }
}

void dereferencet::construct_from_array(
  expr2tc &value,
  const expr2tc &offset,
  const type2tc &type,
  const guardt &guard,
  modet mode,
  unsigned long alignment)
{
  assert(is_array_type(value));

  const array_type2t arr_type = to_array_type(value->type);
  type2tc arr_subtype = arr_type.subtype;

  if (is_array_type(arr_subtype))
  {
    construct_from_multidir_array(value, offset, type, guard, alignment, mode);
    return;
  }

  unsigned int subtype_size = type_byte_size_bits(arr_subtype).to_uint64();
  expr2tc subtype_sz_expr = constant_int2tc(offset->type, BigInt(subtype_size));
  // The value of "div" does not depend on the offset units (i.e., bits or bytes)
  // as it essentially represents an index in the array of the given subtype
  expr2tc div =
    typecast2tc(pointer_type2(), div2tc(offset->type, offset, subtype_sz_expr));
  simplify(div);

  expr2tc mod = modulus2tc(offset->type, offset, subtype_sz_expr);
  simplify(mod);

  // Two different ways we can access elements
  //  1) Just treat them as an element and select them out, possibly with some
  //     byte extracts applied to it
  //  2) Stitch everything together with extracts and concats.

  unsigned int deref_size = type->get_width();

  // Can we just select this out?
  bool is_correctly_aligned = false;
  // Additional complexity occurs if it's aligned but overflows boundaries
  bool overflows_boundaries;
  if (is_constant_int2t(offset))
  {
    // Constant offset is aligned with array boundaries?
    unsigned int offs = to_constant_int2t(offset).value.to_uint64();
    unsigned int elem_offs = offs % subtype_size;
    is_correctly_aligned = (elem_offs == 0);
    overflows_boundaries = (elem_offs + deref_size > subtype_size);
  }
  else
  {
    // Dyn offset -- is alignment guarantee strong enough?
    is_correctly_aligned = (alignment >= subtype_size);
    overflows_boundaries = !is_correctly_aligned || deref_size > subtype_size;
  }

  // No alignment guarantee: assert that it's correct.
  if (!is_correctly_aligned)
    check_alignment(deref_size, std::move(mod), guard);

  if (!overflows_boundaries)
  {
    // Just extract an element and apply other standard extraction stuff.
    // No scope for stitching being required.
    if (arr_type.array_size && arr_type.array_size->type != div->type)
      div = typecast2tc(arr_type.array_size->type, div);
    value = index2tc(arr_subtype, value, div);
    build_reference_rec(value, mod, type, guard, mode, alignment);
  }
  else
  {
    // Might read from more than one element, legitimately. Requires stitching.
    // Alignment assertion / guarantee ensures we don't do something silly.
    // This will construct from whatever the subtype is...
    // Make sure that we extract a correct number of bytes
    // if the offset is dynamic
    expr2tc replaced_dyn_offset = replace_dyn_offset_with_zero(offset);
    simplify(replaced_dyn_offset);
    unsigned int num_bytes = compute_num_bytes_to_extract(
      replaced_dyn_offset, type_byte_size_bits(type).to_uint64());

    // Converting offset to bytes for byte extracting
    expr2tc offset_bytes = typecast2tc(
      size_type2(), div2tc(offset->type, offset, gen_long(offset->type, 8)));
    simplify(offset_bytes);

    // Extracting and stitching bytes together
    std::vector<expr2tc> bytes = extract_bytes(value, num_bytes, offset_bytes);
    value = stitch_together_from_byte_array(num_bytes, bytes);

    expr2tc offset_bits = typecast2tc(
      size_type2(),
      modulus2tc(offset->type, offset, gen_long(offset->type, 8)));
    simplify(offset_bits);

    // Extracting bits from the produced bv
    value = bitcast2tc(
      type,
      extract_bits_from_byte_array(
        value, offset_bits, type_byte_size_bits(type).to_uint64()));
  }
}

void dereferencet::construct_from_const_offset(
  expr2tc &value,
  const expr2tc &offset,
  const type2tc &type)
{
  const constant_int2t &theint = to_constant_int2t(offset);

  assert(is_scalar_type(value));
  // We're accessing some kind of scalar type; might be a valid, correct
  // access, or we might need to be byte extracting it.

  if (theint.value == 0 && value->type->get_width() == type->get_width())
  {
    // Offset is zero, and we select the entire contents of the field. We may
    // need to perform a cast though.
    if (!base_type_eq(value->type, type, ns))
      value = bitcast2tc(type, value);
    return;
  }

  if (value->type->get_width() < type->get_width())
  {
    // Oversized read
    if (!is_member2t(value))
    {
      // give up, rely on dereference failure
      value = expr2tc();
      return;
    }
    // We are accessing more than one member of a struct.
    // So, get step down one level to the source_value and
    // resort to extracting bytes from it.
    else
    {
      const member2t &themember = to_member2t(value);
      value = themember.source_value;
    }
  }

  unsigned int num_bytes =
    compute_num_bytes_to_extract(offset, type_byte_size_bits(type).to_uint64());

  // Converting offset to bytes before bytes extraction
  expr2tc offset_bytes = typecast2tc(
    size_type2(), div2tc(offset->type, offset, gen_long(offset->type, 8)));
  simplify(offset_bytes);

  // Extracting and stitching bytes together
  value = stitch_together_from_byte_array(
    num_bytes, extract_bytes(value, num_bytes, offset_bytes));

  expr2tc offset_bits =
    modulus2tc(offset->type, offset, gen_long(offset->type, 8));
  simplify(offset_bits);

  value = extract_bits_from_byte_array(
    value, offset_bits, type_byte_size_bits(type).to_uint64());

  // Extracting bits from the produced bv
  value = bitcast2tc(type, value);
}

void dereferencet::construct_from_const_struct_offset(
  expr2tc &value,
  const expr2tc &offset,
  const type2tc &type,
  const guardt &guard,
  modet mode)
{
  assert(is_struct_type(value->type));
  const struct_type2t &struct_type = to_struct_type(value->type);
  const BigInt int_offset = to_constant_int2t(offset).value;
  BigInt access_size = type_byte_size_bits(type);

  unsigned int i = 0;
  for (auto const &it : struct_type.members)
  {
    BigInt m_offs =
      member_offset_bits(value->type, struct_type.member_names[i], &ns);
    BigInt m_size = type_byte_size_bits(it, &ns);

    if (m_size == 0)
    {
      // This field has no size: it's most likely a struct that has no members.
      // Just skip over it: we can never correctly build a reference to a field
      // in that struct, because there are no fields. The next field in the
      // current struct lies at the same offset and is probably what the pointer
      // is supposed to point at.
      // If user is seeking a reference to this substruct, a different method
      // should have been called (construct_struct_ref_from_const_offset).
      assert(is_struct_type(it));
      assert(!is_struct_type(type));
      i++;
      continue;
    }

    if (int_offset < m_offs)
    {
      // The offset is behind this field, but wasn't accepted by the previous
      // member. That means that the offset falls in the undefined gap in the
      // middle. Which might be an error -- reading from it definitely is,
      // but we might write to it in the course of memset.
      value = expr2tc();
      if (is_write(mode))
      {
        // This write goes to an invalid symbol, but no assertion is encoded,
        // so it's entirely safe.
      }
      else
      {
        assert(is_read(mode));
        // Oh dear. Encode a failure assertion.
        dereference_failure(
          "pointer dereference",
          "Dereference reads between struct fields",
          guard);
      }
      return;
    }

    if (int_offset == m_offs)
    {
      // Does this over-read?
      if (
        access_size > m_size && options.get_bool_option("struct-fields-check"))
      {
        dereference_failure(
          "pointer dereference", "Over-sized read of struct field", guard);
        value = expr2tc();
        return;
      }

      // This is a valid access to this field. Extract it, recurse.
      value = member2tc(it, value, struct_type.member_names[i]);
      // The offset is 0 here. So does not matter bytes or bits
      build_reference_rec(value, gen_ulong(0), type, guard, mode);

      return;
    }

    if (int_offset > m_offs && (int_offset - m_offs + access_size <= m_size))
    {
      // This access is in the bounds of this member, but isn't at the start.
      // XXX that might be an alignment error.
      expr2tc memb = member2tc(it, value, struct_type.member_names[i]);
      expr2tc new_offs = constant_int2tc(pointer_type2(), int_offset - m_offs);

      // Extract.
      build_reference_rec(memb, new_offs, type, guard, mode);
      value = memb;
      return;
    }

    if (int_offset < (m_offs + m_size))
    {
      // This access starts in this field, but by process of elimination,
      // doesn't end in it. Which means reading padding data (or an alignment
      // error), which are both bad.
      alignment_failure("Misaligned access to struct field", guard);
      value = expr2tc();
      return;
    }

    // Wasn't that field.
    i++;
  }

  // Fell out of that struct -- means we've accessed out of bounds. Code at
  // a higher level will encode an assertion to this effect.
  value = expr2tc();
}

void dereferencet::construct_from_dyn_struct_offset(
  expr2tc &value,
  const expr2tc &offset,
  const type2tc &type,
  const guardt &guard,
  unsigned long alignment,
  modet mode,
  const expr2tc *failed_symbol)
{
  unsigned int access_sz = type_byte_size_bits(type).to_uint64();

  // if we are accessing the struct using a byte, we can ignore alignment
  // rules, so convert the struct to bv and dispatch it to
  // construct_from_dyn_offset
  if (access_sz == config.ansi_c.char_width)
  {
    value = bitcast2tc(
      get_uint_type(type_byte_size_bits(value->type, &ns).to_uint64()), value);
    return construct_from_dyn_offset(value, offset, type);
  }

  // For each element of the struct, look at the alignment, and produce an
  // appropriate access (that we'll switch on).
  assert(is_struct_type(value->type));
  const struct_type2t &struct_type = to_struct_type(value->type);
  expr2tc bits_offset = offset;

  expr2tc failed_container;
  if (failed_symbol == nullptr)
    failed_container = make_failed_symbol(type);
  else
    failed_container = *failed_symbol;

  // A list of guards, and outcomes. The result should be a gigantic
  // if-then-else chain based on those guards.
  std::list<std::pair<expr2tc, expr2tc>> extract_list;

  unsigned int i = 0;
  for (type2tc it : struct_type.members)
  {
    BigInt offs =
      member_offset_bits(value->type, struct_type.member_names[i], &ns);

    // Compute some kind of guard
    it = ns.follow(it);
    BigInt field_size = type_byte_size_bits(it, &ns);

    // Round up to word size
    expr2tc field_offset = constant_int2tc(offset->type, offs);
    expr2tc field_top = constant_int2tc(offset->type, offs + field_size);
    expr2tc lower_bound = greaterthanequal2tc(bits_offset, field_offset);
    expr2tc upper_bound = lessthan2tc(bits_offset, field_top);
    expr2tc field_guard = and2tc(lower_bound, upper_bound);
    expr2tc field = member2tc(it, value, struct_type.member_names[i]);
    expr2tc new_offset = sub2tc(offset->type, offset, field_offset);
    simplify(new_offset);

    if (is_struct_type(it))
    {
      // Handle recursive structs
      construct_from_dyn_struct_offset(
        field, new_offset, type, guard, alignment, mode, &failed_container);
      extract_list.emplace_back(field_guard, field);
    }
    else if (is_array_type(it))
    {
      construct_from_array(field, new_offset, type, guard, mode, alignment);
      extract_list.emplace_back(field_guard, field);
    }
    else if (
      access_sz > field_size && type->get_width() != config.ansi_c.char_width)
    {
      guardt newguard(guard);
      newguard.add(field_guard);
      dereference_failure(
        "pointer dereference", "Oversized field offset", newguard);
      // Push nothing back, allow fall-through of the if-then-else chain to
      // resolve to a failed deref symbol.
    }
    else if (
      alignment >= config.ansi_c.word_size &&
      it->get_width() == type->get_width())
    {
      // This is fully aligned, just pull it out and possibly cast,
      // XXX endian?
      if (!base_type_eq(field->type, type, ns))
        field = bitcast2tc(type, field);
      extract_list.emplace_back(field_guard, field);
    }
    else
    {
      // Try to resolve this recursively
      guardt newguard(guard);
      newguard.add(field_guard);
      build_reference_rec(field, new_offset, type, newguard, mode, alignment);
      extract_list.emplace_back(field_guard, field);
    }

    i++;
  }

  // Build up the new value, switching on the field guard, with the failed
  // symbol at the base.
  expr2tc new_value = failed_container;
  for (const auto &it : extract_list)
    new_value = if2tc(type, it.first, it.second, new_value);

  value = new_value;
}

void dereferencet::construct_from_dyn_offset(
  expr2tc &value,
  const expr2tc &offset,
  const type2tc &type)
{
  expr2tc orig_value = value;

  // Else, in the case of a scalar access at the bottom,
  assert(config.ansi_c.endianess != configt::ansi_ct::NO_ENDIANESS);
  assert(is_scalar_type(value));

  // If source and dest types match, then this access either is a direct hit
  // with offset == 0, or is out of bounds and should be a free value.
  if (base_type_eq(value->type, type, ns))
  {
    // Is offset zero?
    expr2tc eq = equality2tc(offset, gen_zero(offset->type));

    // Yes -> value, no -> free value
    expr2tc free_result = make_failed_symbol(type);
    expr2tc result = if2tc(type, eq, value, free_result);

    value = result;
    return;
  }

  expr2tc replaced_dyn_offset = replace_dyn_offset_with_zero(offset);
  simplify(replaced_dyn_offset);
  unsigned int num_bytes = compute_num_bytes_to_extract(
    replaced_dyn_offset, type_byte_size_bits(type).to_uint64());
  // Converting offset to bytes before bytes extraction
  expr2tc offset_bytes = typecast2tc(
    size_type2(), div2tc(offset->type, offset, gen_long(offset->type, 8)));
  simplify(offset_bytes);

  // Extracting and stitching bytes together
  value = stitch_together_from_byte_array(
    num_bytes, extract_bytes(value, num_bytes, offset_bytes));

  // Extracting bits from the produced bv
  value = extract_bits_from_byte_array(
    value, offset, type_byte_size_bits(type).to_uint64());
  value = bitcast2tc(type, value);
}

void dereferencet::construct_from_multidir_array(
  expr2tc &value,
  const expr2tc &offset,
  const type2tc &type,
  const guardt &guard,
  unsigned long alignment,
  modet mode)
{
  assert(is_array_type(value));
  const array_type2t arr_type = to_array_type(value->type);

  // Right: any access across the boundary of the outer dimension of this array
  // is an alignment violation as that can possess extra padding.
  // So, divide the offset by size of the inner dimention, make an index2t, and
  // construct a reference to that.
  expr2tc subtype_sz = type_byte_size_bits_expr(arr_type.subtype);
  if (subtype_sz->type != offset->type)
  {
    /* TODO: subtype_sz is in bits (with its type being bitsize_type2()).
     *       Need to make sure that this typecast is not truncating high bits.
     */
    subtype_sz = typecast2tc(offset->type, subtype_sz);
  }

  expr2tc div = div2tc(offset->type, offset, subtype_sz);
  simplify(div);

  expr2tc outer_idx = index2tc(arr_type.subtype, value, div);
  value = outer_idx;

  expr2tc mod = modulus2tc(offset->type, offset, subtype_sz);
  simplify(mod);

  construct_from_array(value, mod, type, guard, mode, alignment);
}

void dereferencet::construct_struct_ref_from_const_offset_array(
  expr2tc &value,
  const expr2tc &offset,
  const type2tc &type,
  const guardt &guard,
  modet mode,
  unsigned long alignment)
{
  const constant_int2t &intref = to_constant_int2t(offset);

  assert(is_array_type(value->type));
  const type2tc &base_subtype = get_base_array_subtype(value->type);

  // All in all: we don't care what's being accessed at this level, unless
  // this struct is being constructed out of a byte array. If that's
  // not the case, just let the array recursive handler handle it. It'll bail
  // if access is unaligned, and reduces us to constructing a constant
  // reference from the base subtype, through the correct recursive handler.
  if (base_subtype->get_width() != 8)
  {
    construct_from_array(value, offset, type, guard, mode, alignment);
    return;
  }

  // Access is creating a structure reference from on top of a byte
  // array. Clearly, this is an expensive operation, but it's necessary for
  // the implementation of malloc.
  std::vector<expr2tc> fields;
  assert(is_struct_type(type));
  const struct_type2t &structtype = to_struct_type(type);
  BigInt struct_offset = intref.value;
  for (const type2tc &target_type : structtype.members)
  {
    expr2tc target;
    if (is_array_type(target_type))
    {
      target = stitch_together_from_byte_array(
        target_type, value, gen_ulong(struct_offset));
    }
    else
    {
      target = value; // The byte array;
      build_reference_rec(
        target, gen_ulong(struct_offset), target_type, guard, mode);
    }
    fields.push_back(target);
    struct_offset += type_byte_size_bits(target_type);
  }

  // We now have a vector of fields reconstructed from the byte array
  value = constant_struct2tc(type, fields);
}

void dereferencet::construct_struct_ref_from_const_offset(
  expr2tc &value,
  const expr2tc &offs,
  const type2tc &type,
  const guardt &guard,
  modet mode)
{
  // Minimal effort: the moment that we can throw this object out due to an
  // incompatible type, we do.
  const constant_int2t &intref = to_constant_int2t(offs);
  BigInt type_size = type_byte_size_bits(type);

  if (is_struct_type(value->type) || is_union_type(value->type))
  {
    // Right. In this situation, there are several possibilities. First, if the
    // offset is zero, and the struct type is compatible, we've succeeded.
    // If the offset isn't zero or the type is not compatible, then there are
    // some possibilities:
    //
    //   a) it's a misaligned access, which is an error
    //   b) there's a member within that we should recurse to.

    // Good, just return this expression.
    if ((intref.value == 0) && dereference_type_compare(value, type))
      return;

    // If it's not compatible, recurse into the next relevant field to see if
    // we can construct a ref in there. This would match structs within structs
    // (but compatible check already gets that;), arrays of structs; and other
    // crazy inside structs.

    auto *data = static_cast<const struct_union_data *>(value->type.get());
    unsigned int i = 0;
    for (auto const &it : data->members)
    {
      BigInt offs = member_offset_bits(value->type, data->member_names[i]);
      BigInt size = type_byte_size_bits(it);

      if (
        !is_scalar_type(it) && intref.value >= offs &&
        intref.value < (offs + size))
      {
        // It's this field. However, zero sized structs may have conspired
        // to make life miserable: we might be creating a reference to one,
        // or there might be one preceeding the desired struct.

        // Zero sized struct and we don't want one,
        if (size == 0 && type_size != 0)
          goto cont;

        // Zero sized struct and it's not the right one (!):
        if (
          size == 0 && type_size == 0 && !dereference_type_compare(value, type))
          goto cont;

        // OK, it's this substruct, and we've eliminated the zero-sized-struct
        // menace. Recurse to continue our checks.
        BigInt new_offs = intref.value - offs;
        expr2tc offs_expr = gen_ulong(new_offs);
        value = member2tc(it, value, data->member_names[i]);

        build_reference_rec(value, offs_expr, type, guard, mode);
        return;
      }
    cont:
      i++;
    }

    // Fell out of that loop. Either this offset is out of range, or lies in
    // padding.
    dereference_failure(
      "Memory model", "Object accessed with illegal offset", guard);
    return;
  }
  log_error(
    "Unexpectedly {} type'd argument to construct_struct_ref",
    get_type_id(value->type));
  abort();
}

void dereferencet::construct_struct_ref_from_dyn_offset(
  expr2tc &value,
  const expr2tc &offs,
  const type2tc &type,
  const guardt &guard,
  modet mode)
{
  // This is much more complicated -- because we don't know the offset here,
  // we need to go through all the possible fields that this might (legally)
  // resolve to and switch on them; then assert that one of them is accessed.
  // So:
  std::list<std::pair<expr2tc, expr2tc>> resolved_list;

  construct_struct_ref_from_dyn_offs_rec(
    value, offs, type, gen_true_expr(), mode, resolved_list);

  if (resolved_list.size() == 0)
  {
    // No legal accesses.
    value = expr2tc();
    bad_base_type_failure(guard, "legal dynamic offset", "nothing");
    return;
  }

  // Switch on the available offsets.
  expr2tc result = make_failed_symbol(type);
  for (std::list<std::pair<expr2tc, expr2tc>>::const_iterator it =
         resolved_list.begin();
       it != resolved_list.end();
       it++)
  {
    result = if2tc(type, it->first, it->second, result);
  }

  value = result;

  // Finally, record an assertion that if none of those accesses were legal,
  // then it's an illegal access.
  expr2tc accuml = gen_false_expr();
  for (std::list<std::pair<expr2tc, expr2tc>>::const_iterator it =
         resolved_list.begin();
       it != resolved_list.end();
       it++)
  {
    accuml = or2tc(accuml, it->first);
  }

  accuml = not2tc(accuml); // Creates a new 'not' expr. Doesn't copy construct.
  guardt tmp_guard = guard;
  tmp_guard.add(accuml);
  bad_base_type_failure(tmp_guard, "legal dynamic offset", "illegal offset");
}

void dereferencet::construct_struct_ref_from_dyn_offs_rec(
  const expr2tc &value,
  const expr2tc &offs,
  const type2tc &type,
  const expr2tc &accuml_guard,
  modet mode,
  std::list<std::pair<expr2tc, expr2tc>> &output)
{
  // Look for all the possible offsets that could result in a legitimate access
  // to the given (struct?) type. Insert into the output list, with a guard
  // based on the 'offs' argument, that identifies when this field is legally
  // accessed.

  // Is this a non-byte-array array?
  if (
    is_array_type(value->type) &&
    get_base_array_subtype(value->type)->get_width() != 8)
  {
    const array_type2t &arr_type = to_array_type(value->type);
    // We can legally access various offsets into arrays. Generate an index
    // and recurse. The complicated part is the new offset and guard: we need
    // to guard for offsets that are inside this array, and modulus the offset
    // by the array size.

    BigInt subtype_size = type_byte_size_bits(arr_type.subtype);
    expr2tc sub_size = constant_int2tc(offs->type, subtype_size);
    expr2tc div = div2tc(offs->type, offs, sub_size);
    expr2tc mod = modulus2tc(offs->type, offs, sub_size);
    expr2tc index = index2tc(arr_type.subtype, value, div);

    // We have our index; now compute guard/offset. Guard expression is
    // (offs >= 0 && offs < size_of_this_array)
    expr2tc new_offset = mod;
    expr2tc gte = greaterthanequal2tc(offs, gen_long(offs->type, 0));
    expr2tc array_size = arr_type.array_size;
    if (array_size->type != sub_size->type)
      array_size = typecast2tc(sub_size->type, array_size);
    expr2tc arr_size_in_bits = mul2tc(sub_size->type, array_size, sub_size);
    expr2tc lt = lessthan2tc(offs, arr_size_in_bits);
    expr2tc range_guard = and2tc(accuml_guard, and2tc(gte, lt));
    simplify(range_guard);

    construct_struct_ref_from_dyn_offs_rec(
      index, new_offset, type, range_guard, mode, output);
    return;
  }

  if (is_struct_type(value->type))
  {
    // OK. If this type is compatible and matches, we're good. There can't
    // be any subtypes in this struct that match because then it'd be defined
    // recursively.
    expr2tc tmp = value;
    if (dereference_type_compare(tmp, type))
    {
      // Excellent. Guard that the offset is zero.
      // Still need to consider the fields, though, since the offset is dynamic.
      expr2tc offs_is_zero =
        and2tc(accuml_guard, equality2tc(offs, gen_long(offs->type, 0)));
      output.emplace_back(offs_is_zero, tmp);
    }

    // It's not compatible, but a subtype may be. Iterate over all of them.
    const struct_type2t &struct_type = to_struct_type(value->type);
    unsigned int i = 0;
    for (auto const &it : struct_type.members)
    {
      // Quickly skip over scalar subtypes.
      if (is_scalar_type(it))
      {
        i++;
        continue;
      }

      BigInt memb_offs =
        member_offset_bits(value->type, struct_type.member_names[i]);
      BigInt size = type_byte_size_bits(it);
      expr2tc memb_offs_expr = gen_long(bitsize_type2(), memb_offs);
      expr2tc limit_expr = gen_long(offs->type, memb_offs + size);
      expr2tc memb = member2tc(it, value, struct_type.member_names[i]);

      // Compute a guard and update the offset for an access to this field.
      // Guard is that the offset is in the range of this field. Offset has
      // offset to this field subtracted.
      expr2tc new_offset = sub2tc(offs->type, offs, memb_offs_expr);
      expr2tc gte = greaterthanequal2tc(offs, memb_offs_expr);
      expr2tc lt = lessthan2tc(offs, limit_expr);
      expr2tc range_guard = and2tc(accuml_guard, and2tc(gte, lt));

      simplify(new_offset);
      construct_struct_ref_from_dyn_offs_rec(
        memb, new_offset, type, range_guard, mode, output);
      i++;
    }
    return;
  }

  if (
    is_array_type(value->type) &&
    get_base_array_subtype(value->type)->get_width() == 8)
  {
    // This is a byte array. We can reconstruct a structure from this, if
    // we don't overflow bounds. Start by encoding an assertion.
    guardt tmp;
    tmp.add(accuml_guard);

    // Only encode a bounds check if we're directly accessing an array symbol:
    // if it isn't, then it's a member of some other struct. If it's the wrong
    // size, a higher level check will encode relevant assertions.
    // Offset is converted to bits for the bounds check.
    if (is_symbol2t(value))
      bounds_check(value, offs, type, tmp);

    // We are left with constructing a structure from a byte array. XXX, this
    // is duplicated from above, refactor?
    std::vector<expr2tc> fields;
    assert(is_struct_type(type));
    const struct_type2t &structtype = to_struct_type(type);
    expr2tc array_offset = offs;
    for (const type2tc &target_type : structtype.members)
    {
      expr2tc target = value; // The byte array;

      simplify(array_offset);
      if (is_array_type(target_type))
        construct_from_array(target, array_offset, target_type, tmp, mode);
      else
        build_reference_rec(target, array_offset, target_type, tmp, mode);
      fields.push_back(target);

      // Update dynamic offset into array
      array_offset = add2tc(
        array_offset->type,
        array_offset,
        gen_long(array_offset->type, type_byte_size_bits(target_type)));
    }

    // We now have a vector of fields reconstructed from the byte array
    expr2tc the_struct = constant_struct2tc(type, std::move(fields));
    output.emplace_back(accuml_guard, the_struct);
    return;
  }
}

/**************************** Dereference utilities ***************************/

void dereferencet::dereference_failure(
  const std::string &error_class,
  const std::string &error_name,
  const guardt &guard)
{
  // This just wraps dereference failure in a no-pointer-check check.
  if (!options.get_bool_option("no-pointer-check") && !block_assertions)
  {
    if (options.get_bool_option("conv-assert-to-assume"))
      dereference_callback.dereference_assume(guard);
    else
      dereference_callback.dereference_failure(error_class, error_name, guard);
  }
}

void dereferencet::bad_base_type_failure(
  const guardt &guard,
  const std::string &wants,
  const std::string &have)
{
  std::stringstream ss;
  ss << "Object accessed with incompatible base type. Wanted " << wants
     << " but got " << have;
  dereference_failure("Memory model", ss.str(), guard);
}

void dereferencet::alignment_failure(
  const std::string &error_name,
  const guardt &guard)
{
  // This just wraps dereference failure in a no-pointer-check check.
  if (!options.get_bool_option("no-align-check"))
    dereference_failure("Pointer alignment", error_name, guard);
}

std::vector<expr2tc> dereferencet::extract_bytes(
  const expr2tc &object,
  unsigned int num_bytes,
  const expr2tc &offset) const
{
  assert(num_bytes != 0);

  std::vector<expr2tc> bytes;
  bytes.reserve(num_bytes);

  /* The SMT backend doesn't handle byte-extract expressions on arrays. To avoid
   * extracting bytes from array symbols directly, for each byte to extract
   * build the index expressions into it in the main loop over num_bytes below.
   *
   * Since the subtypes are known statically, pre-compute the subtypes and their
   * size expressions (in case they are symbolically sized), for all array-like
   * subtypes from the outer to the inner one. If the source object is not of
   * array type, this vector will stay empty.
   */
  std::vector<std::pair<type2tc, expr2tc>> subtypes_sizes;
  type2tc base = object->type;
  const type2tc &bytetype = get_uint8_type();
  while (1)
  {
    if (is_array_type(base))
      base = to_array_type(base).subtype;
    else if (is_vector_type(base))
      base = to_vector_type(base).subtype;
    else
      break;

    subtypes_sizes.emplace_back(base, type_byte_size_expr(base));
  }

  bool base_is_byte = is_byte_type(base);
  for (unsigned i = 0; i < num_bytes; i++)
  {
    expr2tc off = offset;
    if (i)
      off = add2tc(off->type, off, gen_long(off->type, i));

    /* As the offset is dynamic, build the index expressions for each byte to
     * extract individually.  We could cache the offset expressions for each
     * subtype level since modulo circles around, but num_bytes usually is small
     * and this only works on arrays that are not VLAs. */
    expr2tc src = object;
    for (const auto &[type, size] : subtypes_sizes)
    {
      src = index2tc(type, src, div2tc(off->type, off, size));
      off = modulus2tc(off->type, off, size);
    }

    assert(src->type == base);
    if (!base_is_byte) // Don't produce a byte update of a byte.
      src = byte_extract2tc(bytetype, src, off, is_big_endian);
    else if (!is_unsignedbv_type(base))
      src = bitcast2tc(bytetype, src);

    bytes.emplace_back(src);
  }

  return bytes;
}

expr2tc dereferencet::stitch_together_from_byte_array(
  unsigned int num_bytes,
  const std::vector<expr2tc> &bytes)
{
  assert(num_bytes != 0);

  // We are composing a larger data type out of bytes -- we must consider
  // what byte order we are giong to stitch it together out of.
  expr2tc accuml;
  if (is_big_endian)
  {
    // First bytes at top of accumulated bitstring
    accuml = bytes[0];
    for (unsigned int i = 1; i < num_bytes; i++)
    {
      type2tc res_type = get_uint_type(accuml->type->get_width() + 8);
      accuml = concat2tc(res_type, accuml, bytes[i]);
    }
  }
  else
  {
    // Little endian, accumulate in reverse order
    accuml = bytes[num_bytes - 1];
    for (int i = num_bytes - 2; i >= 0; i--)
    {
      type2tc res_type = get_uint_type(accuml->type->get_width() + 8);
      accuml = concat2tc(res_type, accuml, bytes[i]);
    }
  }

  return accuml;
}

expr2tc dereferencet::stitch_together_from_byte_array(
  const type2tc &type,
  const expr2tc &byte_array,
  expr2tc offset_bits)
{
  /* TODO: check array bounds, (alignment?) */
  assert(is_array_type(byte_array));
  assert(to_array_type(byte_array->type).subtype->get_width() == 8);

  /* Is the value to be constructed also a byte-array? */
  if (is_array_type(type) && is_constant_int2t(offset_bits))
  {
    const array_type2t &ret_type = to_array_type(type);
    const array_type2t &arr_type = to_array_type(byte_array->type);
    /* of known and matching size and zero offset? */
    if (
      is_constant_int2t(arr_type.array_size) &&
      is_constant_int2t(ret_type.array_size) &&
      to_constant_int2t(offset_bits).value == 0 &&
      arr_type.subtype == ret_type.subtype &&
      to_constant_int2t(arr_type.array_size).value ==
        to_constant_int2t(ret_type.array_size).value)
      return byte_array;
  }

  expr2tc offset_bytes =
    div2tc(offset_bits->type, offset_bits, gen_long(offset_bits->type, 8));
  simplify(offset_bytes);

  BigInt num_bits = type_byte_size_bits(type);
  assert(num_bits.is_uint64());
  uint64_t num_bits64 = num_bits.to_uint64();
  assert(num_bits64 <= ULONG_MAX);
  unsigned int num_bytes =
    compute_num_bytes_to_extract(offset_bits, num_bits64);

  offset_bits =
    modulus2tc(offset_bits->type, offset_bits, gen_long(offset_bits->type, 8));
  simplify(offset_bits);

  return bitcast2tc(
    type,
    extract_bits_from_byte_array(
      stitch_together_from_byte_array(
        num_bytes, extract_bytes(byte_array, num_bytes, offset_bytes)),
      offset_bits,
      num_bits64));
}

void dereferencet::valid_check(
  const expr2tc &object,
  const guardt &guard,
  modet mode)
{
  const expr2tc &symbol = get_symbol(object);

  if (is_constant_string2t(symbol))
  {
    // always valid, but can't write

    if (is_write(mode))
    {
      dereference_failure(
        "pointer dereference", "write access to string constant", guard);
    }
  }
  else if (is_nil_expr(symbol))
  {
    // always "valid", shut up
    return;
  }
  else if (is_symbol2t(symbol))
  {
    // Hacks, but as dereferencet object isn't persistent, necessary. Fix by
    // making dereferencet persistent.
    if (has_prefix(
          to_symbol2t(symbol).thename.as_string(), "symex::invalid_object"))
    {
      // This is an invalid object; if we're in read or write mode, that's an error.
      if (is_read(mode) || is_write(mode))
        dereference_failure("pointer dereference", "invalid pointer", guard);
      return;
    }

    const symbolt &sym = *ns.lookup(to_symbol2t(symbol).thename);
    if (has_prefix(sym.id.as_string(), "symex_dynamic::"))
    {
      // Assert that it hasn't (nondeterministically) been invalidated.
      expr2tc addrof = address_of2tc(symbol->type, symbol);
      expr2tc valid_expr = valid_object2tc(addrof);
      expr2tc not_valid_expr = not2tc(valid_expr);

      guardt tmp_guard(guard);
      tmp_guard.add(not_valid_expr);

      std::string foo = is_free(mode) ? "invalidated dynamic object freed"
                                      : "invalidated dynamic object";
      dereference_failure("pointer dereference", foo, tmp_guard);
    }
    else
    {
      // Not dynamic; if we're in free mode, that's an error.
      if (is_free(mode))
      {
        dereference_failure(
          "pointer dereference", "free() of non-dynamic memory", guard);
        return;
      }

      // Otherwise, this is a pointer to some kind of lexical variable, with
      // either global or function-local scope. Ask symex to determine if
      // it's live.
      if (!dereference_callback.is_live_variable(symbol))
      {
        // Any access where this guard is true -> failure
        dereference_failure(
          "pointer dereference",
          "accessed expired variable pointer `" +
            get_pretty_name(to_symbol2t(symbol).thename.as_string()) + "'",
          guard);
        return;
      }

      /* Writes to globals of const-qualified type are not allowed either. */
      if (is_write(mode) && sym.static_lifetime && sym.type.cmt_constant())
      {
        dereference_failure(
          "pointer dereference",
          "write access to const object `" +
            get_pretty_name(sym.id.as_string()) + "'",
          guard);
        return;
      }
    }
  }
}

void dereferencet::bounds_check(
  const expr2tc &expr,
  const expr2tc &offset,
  const type2tc &type,
  const guardt &guard)
{
  if (options.get_bool_option("no-bounds-check"))
    return;

  assert(is_array_type(expr));
  const array_type2t arr_type = to_array_type(expr->type);

  if (!arr_type.array_size)
  {
    /* Infinite size array, doesn't have bounds to check. We arrive here in two
     * situations: access to an incomplete type (originally an array, struct or
     * union) or access to an array marked to have infinite size. These cases
     * only happen when the program actually builds and dereferences pointers
     * to the object.
     *
     * We actually allow also the first case since there is not enough
     * information about the type (it's incomplete). This is in line with how
     * we handle functions with no body. See also the comments in migrate_type()
     * about incomplete types and Github issue #1210. */
    assert(arr_type.size_is_infinite);
    return;
  }

  unsigned int access_size = type_byte_size(type).to_uint64();

  expr2tc arrsize;
  if (
    !is_constant_expr(expr) &&
    has_prefix(
      ns.lookup(to_symbol2t(expr).thename)->id.as_string(), "symex_dynamic::"))
  {
    // Construct a dynamic_size irep.
    expr2tc addrof = address_of2tc(expr->type, expr);
    arrsize = dynamic_size2tc(addrof);
  }
  else if (!is_constant_int2t(arr_type.array_size))
  {
    // Also a dynamic_size irep.
    expr2tc addrof = address_of2tc(expr->type, expr);
    arrsize = dynamic_size2tc(addrof);
  }
  else
  {
    // Calculate size from type.

    // Dance around getting the array type normalised.
    type2tc new_string_type;

    // XXX -- arrays were assigned names, but we're skipping that for the moment
    // std::string name = array_name(ns, expr.source_value);

    // Firstly, bail if this is an infinite sized array. There are no bounds
    // checks to be performed.
    if (arr_type.size_is_infinite)
      return;

    // Secondly, try to calc the size of the array.
    expr2tc subtype_size =
      constant_int2tc(size_type2(), type_byte_size(arr_type.subtype));
    expr2tc array_size = typecast2tc(size_type2(), arr_type.array_size);
    arrsize = mul2tc(size_type2(), array_size, subtype_size);
  }

  // Transforming offset to bytes
  expr2tc unsigned_offset = typecast2tc(
    size_type2(), div2tc(offset->type, offset, gen_long(offset->type, 8)));

  // Then, expressions as to whether the access is over or under the array
  // size.
  expr2tc access_size_e = constant_int2tc(size_type2(), BigInt(access_size));
  expr2tc upper_byte = add2tc(size_type2(), unsigned_offset, access_size_e);

  expr2tc gt = greaterthan2tc(unsigned_offset, arrsize);
  expr2tc gt2 = greaterthan2tc(upper_byte, arrsize);
  expr2tc is_in_bounds = or2tc(gt, gt2);

  // Report these as assertions; they'll be simplified away if they're constant

  guardt tmp_guard1(guard);
  tmp_guard1.add(is_in_bounds);
  dereference_failure("array bounds", "array bounds violated", tmp_guard1);
}

bool dereferencet::check_code_access(
  expr2tc &value,
  const expr2tc &offset,
  const type2tc &type,
  const guardt &guard,
  modet mode)
{
  assert(is_code_type(value) || is_code_type(type));

  if (!is_code_type(type))
  {
    dereference_failure(
      "Code separation", "Program code accessed with non-code type", guard);
    return false;
  }

  if (!is_code_type(value))
  {
    dereference_failure(
      "Code separation", "Data object accessed with code type", guard);
    return false;
  }

  if (!is_read(mode))
  {
    dereference_failure(
      "Code separation", "Program code accessed in write or free mode", guard);
  }

  // Only other constraint is that the offset has to be zero; there are no
  // other rules about what code objects look like.
  expr2tc neq = notequal2tc(offset, gen_zero(offset->type));
  guardt tmp_guard = guard;
  tmp_guard.add(neq);
  dereference_failure(
    "Code separation", "Program code accessed with non-zero offset", tmp_guard);

  // As for setting the 'value', it's currently already set to the base code
  // object. There's nothing we can actually change it to mean anything, so
  // don't fiddle with it.

  return true;
}

void dereferencet::check_data_obj_access(
  const expr2tc &value,
  const expr2tc &offset,
  const type2tc &type,
  const guardt &guard,
  modet mode)
{
  assert(!is_array_type(value));
  assert(offset->type == bitsize_type2());

  BigInt data_sz = type_byte_size_bits(value->type);
  BigInt access_sz = type_byte_size_bits(type);
  expr2tc data_sz_e = gen_long(offset->type, data_sz);
  expr2tc access_sz_e = gen_long(offset->type, access_sz);

  // Only erroneous thing we check for right now is that the offset is out of
  // bounds, misaligned access happense elsewhere. The highest byte read is at
  // offset+access_sz-1, so check fail if the (offset+access_sz) > data_sz.
  // Lower bound not checked, instead we just treat everything as unsigned,
  // which has the same effect.
  expr2tc add = add2tc(access_sz_e->type, offset, access_sz_e);
  expr2tc gt = greaterthan2tc(add, data_sz_e);

  if (!options.get_bool_option("no-bounds-check"))
  {
    guardt tmp_guard = guard;
    tmp_guard.add(gt);
    dereference_failure(
      "pointer dereference", "Access to object out of bounds", tmp_guard);
  }

  /* Also, if if it's a scalar and the access is not performed in an unaligned
   * manner (e.g. for __attribute__((packed)) structures),
   * check that the access being made is aligned. */
  if (is_scalar_type(type) && !mode.unaligned)
    check_alignment(access_sz, std::move(offset), guard);
}

void dereferencet::check_alignment(
  BigInt minwidth,
  const expr2tc &offset_bits,
  const guardt &guard)
{
  // If we are dealing with a bitfield, then
  // skip the alignment check
  if (minwidth % 8 != 0)
    return;

  if (is_constant_int2t(offset_bits))
  {
    if (to_constant_int2t(offset_bits).value.to_uint64() % 8 != 0)
      return;
  }

  // Perform conversion to bytes here
  minwidth = minwidth / 8;
  expr2tc offset = typecast2tc(
    size_type2(),
    div2tc(offset_bits->type, offset_bits, gen_long(offset_bits->type, 8)));
  simplify(offset);

  expr2tc mask_expr = gen_ulong(minwidth - 1);
  expr2tc neq;

  if (options.get_bool_option("int-encoding"))
  {
    expr2tc align = gen_ulong(minwidth);
    expr2tc moded = modulus2tc(align->type, offset, align);
    neq = notequal2tc(moded, gen_zero(moded->type));
  }
  else
  {
    expr2tc anded = bitand2tc(mask_expr->type, mask_expr, offset);
    neq = notequal2tc(anded, gen_zero(anded->type));
  }

  guardt tmp_guard2 = guard;
  tmp_guard2.add(neq);
  alignment_failure(
    "Incorrect alignment when accessing data object", tmp_guard2);
}

unsigned int dereferencet::compute_num_bytes_to_extract(
  const expr2tc offset,
  unsigned long num_bits)
{
  // We need to calculate the correct number of bytes to extract.
  // This is so that we do not miss any bits in case there are
  // bitfields lying on the border of two neighbouring bytes
  // (e.g., |ooooooox|xooooooo|).
  //
  // By default we assume that the "offset" is aligned to 8 bits.
  // So we just compute the number of bytes that completely contain
  // the target bits (hence, adding 7 before division).
  unsigned int num_bytes = (num_bits + 7) / 8;

  // If "offset" is known (i.e., constant), we should take this into
  // account when calculating the number of bytes to extract.
  if (is_constant_int2t(offset))
  {
    unsigned long offset_int = to_constant_int2t(offset).value.to_uint64();
    unsigned int bits_in_first_byte = offset_int % 8;
    // Again, adding 7 here to make sure that we do not "cut off"
    // any bits from the last byte.
    num_bytes =
      (bits_in_first_byte + num_bits + 7) / 8 - (bits_in_first_byte / 8);
  }
  return num_bytes;
}

expr2tc dereferencet::extract_bits_from_byte_array(
  const expr2tc &value,
  const expr2tc &offset,
  unsigned long num_bits)
{
  // Extract the target bits using bitwise AND
  // and bit-shifting as follows:
  //
  //   value := ((rtype)value >> shft) & mask;
  //
  // where
  //   mask - is a bitvector with 1's starting at 'offset' and
  //          for as long as 'type->width', and 0's everywhere else
  //   shft = offset - (offset / 8) * 8 (i.e., offset in bits minus
  //          the number of full bytes converted to bits)
  //   rtype = unsignedbv type of width equal to num_bits

  type2tc rtype = get_uint_type(num_bits);

  if (is_constant_int2t(offset))
  {
    // If everything is aligned to 8 bits, we can just return the initial value
    if (
      (num_bits % 8 == 0) &&
      (to_constant_int2t(offset).value.to_uint64() % 8 == 0))
      return value->type == rtype ? value : typecast2tc(rtype, value);
  }
  else
  {
    // If the offset is not known (i.e., just some pointer_offset2t)
    // but the number of bits to be extracted is a multiple of 8,
    // we are just going to return the value
    if (num_bits % 8 == 0)
      return value->type == rtype ? value : typecast2tc(rtype, value);
  }

  expr2tc shft_expr =
    modulus2tc(offset->type, offset, gen_long(offset->type, 8));
  simplify(shft_expr);

  expr2tc mask_expr = constant_int2tc(rtype, (BigInt(1) << num_bits) - 1);

  assert(num_bits <= UINT_MAX);

  expr2tc result;
  result = lshr2tc(value->type, value, shft_expr);
  result = typecast2tc(rtype, result);
  result = bitand2tc(rtype, result, mask_expr);
  simplify(result);
  return result;
}
