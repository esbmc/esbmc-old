#include <cassert>
#include <langapi/language_util.h>
#include <pointer-analysis/value_set.h>
#include <util/arith_tools.h>
#include <util/base_type.h>
#include <util/c_types.h>
#include <util/config.h>
#include <util/context.h>
#include <util/expr_util.h>
#include <util/i2string.h>
#include <irep2/irep2.h>
#include <util/migrate.h>
#include <util/message.h>
#include <util/message/format.h>
#include <util/prefix.h>
#include <util/simplify_expr.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/type_byte_size.h>

object_numberingt value_sett::object_numbering;
object_number_numberingt value_sett::obj_numbering_refset;

void value_sett::output(std::ostream &out) const
{
  // Iterate over all tracked variables, dumping a list of all the things it
  // might point at.
  for(const auto &value : values)
  {
    std::string identifier, display_name;

    const entryt &e = value.second;

    if(has_prefix(e.identifier, "value_set::dynamic_object"))
    {
      display_name = e.identifier + e.suffix;
      identifier = "";
    }
    else if(e.identifier == "value_set::return_value")
    {
      display_name = "RETURN_VALUE" + e.suffix;
      identifier = "";
    }
    else
    {
#if 0
      const symbolt &symbol=ns.lookup(e.identifier);
      display_name=symbol.display_name()+e.suffix;
      identifier=symbol.name;
#else
      identifier = e.identifier;
      display_name = identifier + e.suffix;
#endif
    }

    out << display_name;

    out << " = { ";

    unsigned width = 0;

    for(object_mapt::const_iterator o_it = e.object_map.begin();
        o_it != e.object_map.end();
        o_it++)
    {
      const expr2tc &o = object_numbering[o_it->first];

      std::string result;

      // Display invalid / unknown objects as just that,
      if(is_invalid2t(o) || is_unknown2t(o))
        result = from_expr(ns, identifier, o);
      else
      {
        // Everything else, display as a triple of <object, offset, type>.
        result = "<" + from_expr(ns, identifier, o) + ", ";

        if(o_it->second.offset_is_set)
          result += integer2string(o_it->second.offset) + "";
        else
          result += "*";

        result += ", " + from_type(ns, identifier, o->type);

        result += ">";
      }

      // Actually print.
      out << result;

      width += result.size();

      object_mapt::const_iterator next(o_it);
      next++;

      if(next != e.object_map.end())
      {
        out << ", ";
        if(width >= 40)
          out << "\n      ";
      }
    }

    out << " } "
        << "\n";
  }
}

expr2tc value_sett::to_expr(object_mapt::const_iterator it) const
{
  const expr2tc &object = object_numbering[it->first];

  if(is_invalid2t(object) || is_unknown2t(object))
    return object;

  expr2tc offs;
  if(it->second.offset_is_set)
    offs = gen_ulong(it->second.offset.to_int64());
  else
    offs = unknown2tc(index_type2());

  expr2tc obj = object_descriptor2tc(
    object->type, object, offs, it->second.offset_alignment);
  return obj;
}

bool value_sett::make_union(const value_sett::valuest &new_values, bool keepnew)
{
  bool result = false;

  // Iterate over all new values; if they're in the current value set, merge
  // them. If not, only merge it in if keepnew is true.
  for(const auto &new_value : new_values)
  {
    valuest::iterator it2 = values.find(new_value.first);

    // If the new variable isnt in this' set,
    if(it2 == values.end())
    {
      // We always track these when merging value sets, as these store data
      // that's transfered back and forth between function calls. So, the
      // variables not existing in the state we're merging into is irrelevant.
      if(
        has_prefix(
          id2string(new_value.second.identifier),
          "value_set::dynamic_object") ||
        new_value.second.identifier == "value_set::return_value" || keepnew)
      {
        values.insert(new_value);
        result = true;
      }

      continue;
    }

    // The variable was in this' set, merge the values.
    entryt &e = it2->second;
    const entryt &new_e = new_value.second;

    if(make_union(e.object_map, new_e.object_map))
      result = true;
  }

  return result;
}

bool value_sett::make_union(object_mapt &dest, const object_mapt &src) const
{
  bool result = false;

  // Merge the pointed at objects in src into dest.
  for(object_mapt::const_iterator it = src.begin(); it != src.end(); it++)
  {
    if(insert(dest, it))
      result = true;
  }

  return result;
}

void value_sett::get_value_set(const expr2tc &expr, value_setst::valuest &dest)
  const
{
  object_mapt object_map;
  // Fetch all values into object_map,
  get_value_set(expr, object_map);

  // Convert values into expressions to return.
  for(object_mapt::const_iterator it = object_map.begin();
      it != object_map.end();
      it++)
    dest.push_back(to_expr(it));
}

void value_sett::get_value_set(const expr2tc &expr, object_mapt &dest) const
{
  // Simplify expr if possible,
  expr2tc new_expr = expr;
  simplify(new_expr);

  // Then, start fetching values.
  get_value_set_rec(new_expr, dest, "", new_expr->type);
}

void value_sett::get_value_set_rec(
  const expr2tc &expr,
  object_mapt &dest,
  const std::string &suffix,
  const type2tc &original_type,
  bool under_deref) const
{
  if(is_unknown2t(expr) || is_invalid2t(expr))
  {
    // Unknown / invalid exprs mean we just point at something unknown (and
    // potentially invalid).
    insert(dest, unknown2tc(original_type), BigInt(0));
    return;
  }

  if(is_index2t(expr))
  {
    // This is an index, fetch values from the array being indexed.
    const index2t &idx = to_index2t(expr);

#ifndef NDEBUG
    const type2tc &source_type = idx.source_value->type;
    assert(is_array_type(source_type) || is_string_type(source_type));
#endif

    // Attach '[]' to the suffix, identifying the variable tracking all the
    // pointers in this array.
    get_value_set_rec(idx.source_value, dest, "[]" + suffix, original_type);
    return;
  }

  if(is_member2t(expr))
  {
    // We're selecting a member variable of a structure: fetch the values it
    // might point at.
    const member2t &memb = to_member2t(expr);

#ifndef NDEBUG
    const type2tc &source_type = memb.source_value->type;
    assert(is_struct_type(source_type) || is_union_type(source_type));
#endif

    irep_idt single_source;
    if(is_struct_type(memb.source_value))
      single_source = memb.member;
    else if(is_constant_union2t(memb.source_value))
      single_source = to_constant_union2t(memb.source_value).init_field;
    if(!single_source.empty())
    {
      // Add '.$field' to the suffix, identifying the member from the other
      // members of the struct's variable.
      get_value_set_rec(
        memb.source_value,
        dest,
        "." + single_source.as_string() + suffix,
        original_type);
    }
    else
    {
      /* We have a member of a union. The value-set of it is the same as the
       * union of the value-sets of each member. */
      assert(is_union_type(memb.source_value->type));
      auto *u =
        static_cast<const struct_union_data *>(memb.source_value->type.get());
      for(const irep_idt &name : u->member_names)
        get_value_set_rec(
          memb.source_value,
          dest,
          "." + name.as_string() + suffix,
          original_type);
    }
    return;
  }

  if(is_if2t(expr))
  {
    // This expression might evaluate to either side of this if (assuming that
    // the simplifier couldn't simplify it away. Grab the value set from either
    // side.
    const if2t &ifval = to_if2t(expr);

    get_value_set_rec(ifval.true_value, dest, suffix, original_type);
    get_value_set_rec(ifval.false_value, dest, suffix, original_type);
    return;
  }

  if(is_address_of2t(expr))
  {
    // The set of things this expression might point at is the set of things
    // that might be the operand to this address-of. So, get the reference set
    // of things it refers to, rather than the value set (of things it points
    // to).
    const address_of2t &addrof = to_address_of2t(expr);
    get_reference_set(addrof.ptr_obj, dest);
    return;
  }

  if(is_dereference2t(expr))
  {
    // Fetch the set of things that this dereference might point at... That
    // means if we have the code:
    //   int *a = NULL;
    //   int **b = &a;
    //   *b;
    // Then we're evaluating the final line, what does *b point at? To do this,
    // take all the things that (*b) refers to, which performs the actual
    // dereference itself. We then have a list of things that b might point at
    // (in this case just a); so we then compute the value set of all those
    // things.
    object_mapt reference_set;
    // Get reference set of dereference; this evaluates the dereference itself.
    get_reference_set(expr, reference_set);

    // Then get the value set of all the pointers we might dereference to.
    for(const auto &it1 : reference_set)
    {
      const expr2tc &object = object_numbering[it1.first];
      get_value_set_rec(object, dest, suffix, original_type);
    }

    return;
  }

  if(is_constant_expr(expr))
  {
    if(under_deref)
    {
      if(is_constant_int2t(expr))
      {
        constant_int2t ci = to_constant_int2t(expr);
        if(ci.value.is_zero())
        {
          expr2tc tmp = null_object2tc(expr->type);
          insert(dest, tmp, BigInt(0));
          return;
        }
        else if(is_signedbv_type(expr->type) || is_unsignedbv_type(expr->type))
          insert(dest, invalid2tc(original_type), BigInt(0));
        else
          insert(dest, unknown2tc(original_type), BigInt(0));
      }
    }
    else
    {
      // Constant numbers aren't pointers. Null check is in the value set code
      // for symbols.
    }
    return;
  }

  if(is_typecast2t(expr))
  {
    // Push straight through typecasts.
    const typecast2t &cast = to_typecast2t(expr);
    get_value_set_rec(cast.from, dest, suffix, original_type);
    return;
  }

  if(is_bitcast2t(expr))
  {
    // Bitcasts are just typecasts with additional semantics
    const bitcast2t &cast = to_bitcast2t(expr);
    get_value_set_rec(cast.from, dest, suffix, original_type);
    return;
  }

  if(is_sideeffect2t(expr))
  {
    // Consider a (potentially memory allocating) side effect. Perform crazy
    // black (and possibly broken) magic to track said memory during static
    // analysis.
    // During symbolic execution, the only assignments handed to value_sett
    // have all the sideeffects taken out of them (as they're SSA assignments),
    // so this is never triggered.
    const sideeffect2t &side = to_sideeffect2t(expr);
    switch(side.kind)
    {
    case sideeffect2t::alloca:
    case sideeffect2t::malloc:
    {
      assert(suffix == "");
      const type2tc &dynamic_type = side.alloctype;

      expr2tc locnum = gen_ulong(location_number);
      expr2tc dynobj = dynamic_object2tc(dynamic_type, locnum, false, false);

      insert(dest, dynobj, BigInt(0));
      return;
    }

    case sideeffect2t::cpp_new:
    case sideeffect2t::cpp_new_arr:
    {
      assert(suffix == "");
      assert(is_pointer_type(side.type));

      expr2tc locnum = gen_ulong(location_number);

      const pointer_type2t &ptr = to_pointer_type(side.type);

      expr2tc dynobj = dynamic_object2tc(ptr.subtype, locnum, false, false);

      insert(dest, dynobj, BigInt(0));
      return;
    }

    case sideeffect2t::nondet:
      // Introduction of nondeterminism does not introduce new pointer vars
      return;

    default:
      log_error("Unexpected side-effect: {}", *expr);
      abort();
    }
  }

  if(is_constant_struct2t(expr))
  {
    // The use of an explicit constant struct value evaluates to it's address.
    expr2tc tmp = address_of2tc(expr->type, expr);
    insert(dest, tmp, BigInt(0));
    return;
  }

  if(is_with2t(expr))
  {
    // Consider an array/struct update: the pointer we evaluate to may be in
    // the base array/struct, or depending on the index may be the update value.
    // So, consider both.
    // XXX jmorse -- this could be improved. What if source_value is a constant
    // array or something?
    const with2t &with = to_with2t(expr);

    // this is the array/struct
    object_mapt tmp_map0;
    get_value_set_rec(with.source_value, tmp_map0, suffix, original_type);

    // this is the update value -- note NO SUFFIX
    object_mapt tmp_map2;
    get_value_set_rec(with.update_value, tmp_map2, "", original_type);

    make_union(dest, tmp_map0);
    make_union(dest, tmp_map2);
    return;
  }

  if(is_constant_array_of2t(expr) || is_constant_array2t(expr))
  {
    // these are supposed to be done by assign()
    assert(0 && "Encountered array irep in get_value_set_rec");
    return;
  }

  if(is_dynamic_object2t(expr))
  {
    const dynamic_object2t &dyn = to_dynamic_object2t(expr);

    assert(is_constant_int2t(dyn.instance));
    const constant_int2t &intref = to_constant_int2t(dyn.instance);
    std::string idnum = integer2string(intref.value);
    const std::string name = "value_set::dynamic_object" + idnum + suffix;

    // look it up
    valuest::const_iterator v_it = values.find(name);

    if(v_it != values.end())
    {
      make_union(dest, v_it->second.object_map);
      return;
    }
  }

  if(is_concat2t(expr))
  {
    get_byte_stitching_value_set(expr, dest, suffix, original_type);
    return;
  }

  if(is_byte_extract2t(expr))
  {
    // This is cropping up when one assigns, for example, a pointer into a
    // byte array. The lhs gets portions of the pointer, bitcasted and then
    // byte extracted on the lhs. Thus, we need to blast through the byte
    // extract.
    const byte_extract2t &be = to_byte_extract2t(expr);
    get_value_set_rec(be.source_value, dest, suffix, original_type);
    return;
  }

  if(is_byte_update2t(expr))
  {
    const byte_update2t &bu = to_byte_update2t(expr);
    get_value_set_rec(bu.source_value, dest, suffix, original_type);
    return;
  }

  if(
    is_bitor2t(expr) || is_bitand2t(expr) || is_bitxor2t(expr) ||
    is_bitnand2t(expr) || is_bitnor2t(expr) || is_bitnxor2t(expr))
  {
    assert(expr->get_num_sub_exprs() == 2);
    get_value_set_rec(*expr->get_sub_expr(0), dest, suffix, original_type);
    get_value_set_rec(*expr->get_sub_expr(1), dest, suffix, original_type);
    return;
  }

  if(is_symbol2t(expr))
  {
    // This is a symbol, and if it's a pointer then this expression might
    // evalutate to what it points at. So, return this symbols value set.
    const symbol2t &sym = to_symbol2t(expr);

    // If it's null however, create a null_object2t with the appropriate type.
    if(sym.thename == "NULL" && is_pointer_type(expr))
    {
      const pointer_type2t &ptr_ref = to_pointer_type(expr->type);
      typet subtype = migrate_type_back(ptr_ref.subtype);
      if(subtype.id() == "symbol")
        subtype = ns.follow(subtype);

      expr2tc tmp = null_object2tc(ptr_ref.subtype);
      insert(dest, tmp, BigInt(0));
      return;
    }

    // Look up this symbol, with the given suffix to distinguish any arrays or
    // members we've picked out of it at a higher level.
    valuest::const_iterator v_it = values.find(sym.get_symbol_name() + suffix);

    if(sym.rlevel == symbol2t::renaming_level::level1_global)
      assert(sym.level1_num == 0);
    assert(sym.rlevel != symbol2t::renaming_level::level2_global);
    /* This assertion does not hold: during value_sett::assign() the RHS is the
     * L2 symbol c:pthread_lib.c@5466@F@pthread_create@startdata in e.g.
     * - regression/esbmc-unix/02_account_symbolic_06
     * - regression/esbmc/10_bicycle_01
     * - regression/nonz3/10_bicycle_02
    // assert(sym.rlevel != symbol2t::renaming_level::level2);
     */

    // If it points at things, put those things into the destination object map.
    if(v_it != values.end())
    {
      make_union(dest, v_it->second.object_map);
      return;
    }
  }

  if(is_add2t(expr) || is_sub2t(expr))
  {
    // Consider pointer arithmetic. This takes takes the form of finding the
    // value sets of the operands, then speculating on how the addition /
    // subtraction affects the offset.
    // In order to facilitate value-set tracking through integer -> pointer
    // casts, all arithmetic add/sub expressions are analyzed.

    // find the pointer operand
    // XXXjmorse - polymorphism.
    const expr2tc &op0 =
      (is_add2t(expr)) ? to_add2t(expr).side_1 : to_sub2t(expr).side_1;
    const expr2tc &op1 =
      (is_add2t(expr)) ? to_add2t(expr).side_2 : to_sub2t(expr).side_2;

    assert(
      (!is_pointer_type(expr) ||
       !(is_pointer_type(op0) && is_pointer_type(op1))) &&
      "Cannot have pointer arithmetic with two pointers as operands");

    // Find out what the pointer operand points at, and suck that data into
    // new object maps.
    object_mapt op0_set;
    object_mapt op1_set;
    get_value_set_rec(op0, op0_set, "", (op0)->type, false);
    get_value_set_rec(op1, op1_set, "", (op1)->type, false);

    /*
     * The main idea here is to detect if any address might be valid at all. If any side
     * of the pointer arithmetic is not a valid object, we can assume that it is not the
     * pointer operand.
     *
     * As of now, there is one KNOWNBUG (esbmc/github_695_2_true), which is triggered by
     * a cast of the index as char ptr. This makes ESBMC think that both operands are
     * valid, which is currently unsupported.
     */
    auto op_contains_valid_address = [this](value_sett::object_mapt o) -> bool {
      for(const auto &it : o)
      {
        auto expr = object_numbering[it.first];
        if(is_symbol2t(expr) || is_constant_string2t(expr))
          return true;
      }

      return false;
    };

    if(!op_contains_valid_address(op0_set))
      op0_set.clear();
    if(!op_contains_valid_address(op1_set))
      op1_set.clear();

    /* TODO: The case that both, op0_set and op1_set, are non-empty is not
     *       handled, yet. */

    if(op0_set.empty() != op1_set.empty())
    {
      bool op0_is_ptr = !op0_set.empty();

      const expr2tc &ptr_op = op0_is_ptr ? op0 : op1;
      const expr2tc &non_ptr_op = op0_is_ptr ? op1 : op0;
      const object_mapt &pointer_expr_set = op0_is_ptr ? op0_set : op1_set;

      type2tc subtype;
      if(is_pointer_type(ptr_op))
        subtype = to_pointer_type(ptr_op->type).subtype;

      // Calculate the offset caused by this addition, in _bytes_. Involves
      // pointer arithmetic. We also use the _perceived_ type of what we're
      // adding or subtracting from/to, it might be being typecasted.
      BigInt total_offs(0);
      bool is_const = false;
      try
      {
        if(is_constant_int2t(non_ptr_op))
        {
          if(to_constant_int2t(non_ptr_op).value.is_zero())
          {
            total_offs = 0;
          }
          else
          {
            BigInt elem_size = 1;
            if(!is_nil_type(subtype))
            {
              if(is_empty_type(subtype))
                throw type2t::symbolic_type_excp();

              // Potentially rename,
              elem_size = type_byte_size(subtype, &ns);
            }
            const BigInt &val = to_constant_int2t(non_ptr_op).value;
            total_offs = val * elem_size;
            if(is_sub2t(expr))
              total_offs.negate();
          }
          is_const = true;
        }
        else
        {
          is_const = false;
        }
      }
      catch(const array_type2t::dyn_sized_array_excp &e)
      { // Nondet'ly sized.
      }
      catch(const array_type2t::inf_sized_array_excp &e)
      {
      }
      catch(const type2t::symbolic_type_excp &e)
      {
        // This vastly annoying piece of code is making operations on void
        // pointers, or worse. If a void pointer, treat the multiplier of the
        // addition as being one. If not void pointer, throw cookies.
        if(is_empty_type(subtype))
        {
          total_offs = to_constant_int2t(non_ptr_op).value;
          is_const = true;
        }
        else
        {
          log_error(
            "Pointer arithmetic on type where we can't determine size\n{}",
            *subtype);
          abort();
        }
      }

      // For each object, update its offset data according to the integer
      // offset to this expr. Potential outcomes are keeping it nondet, making
      // it nondet, or calculating a new static offset.
      for(const auto &it : pointer_expr_set)
      {
        objectt object = it.second;

        unsigned int nat_align =
          get_natural_alignment(object_numbering[it.first]);
        unsigned int ptr_align = get_natural_alignment(ptr_op);

        if(is_const && object.offset_is_set)
        {
          // Both are const; we can accumulate offsets;
          object.offset += total_offs;
        }
        else if(is_const && !object.offset_is_set)
        {
          // Offset is const, but existing pointer isn't. The alignment is now
          // at least as small as the operand alignment.
          object.offset_alignment =
            std::min(nat_align, object.offset_alignment);
        }
        else if(!is_const && object.offset_is_set)
        {
          // Nondet but aligned offset from arithmetic; but offset set in
          // current object. Take the minimum alignment again.
          unsigned int offset_align = 0;
          if((object.offset % nat_align) != 0)
          {
            // We have some kind of offset into this data object, but it's less
            // than the data objects natural alignment. So, the maximum
            // alignment we can have is that of the pointer type being added
            // or subtracted. The minimum, depends on the offset into the
            // data object we're pointing at.
            offset_align = ptr_align;
            if(object.offset % ptr_align != 0)
              // Too complex to calculate; clamp to bytes.
              offset_align = 1;
          }
          else
          {
            offset_align = nat_align;
          }

          object.offset_is_set = false;
          object.offset_alignment = std::min(nat_align, offset_align);
        }
        else
        {
          // Final case: nondet offset from operation, and nondet offset in
          // the current object. So, just take the minimum available.
          object.offset_alignment =
            std::min(nat_align, object.offset_alignment);
        }

        // Once updated, store object reference into destination map.
        insert(dest, it.first, object);
      }

      return;
    }
  }

  // If none of those expressions matched, then we don't really know what this
  // expression evaluates to. So just record it as being unknown.
  log_debug(
    "value-set",
    "unknown expr {} of type {} -> inserting unknown value",
    get_expr_id(expr),
    get_type_id(expr->type));
  expr2tc tmp = unknown2tc(original_type);
  insert(dest, tmp, BigInt(0));
}

void value_sett::get_byte_stitching_value_set(
  const expr2tc &expr,
  object_mapt &dest,
  const std::string &suffix,
  const type2tc &original_type) const
{
  if(is_concat2t(expr))
  {
    const concat2t &ref = to_concat2t(expr);
    get_byte_stitching_value_set(ref.side_1, dest, suffix, original_type);
    get_byte_stitching_value_set(ref.side_2, dest, suffix, original_type);
    return;
  }

  if(is_lshr2t(expr))
  {
    const lshr2t &ref = to_lshr2t(expr);
    get_byte_stitching_value_set(ref.side_1, dest, suffix, original_type);
    return;
  }

  if(is_byte_extract2t(expr))
  {
    const byte_extract2t &ref = to_byte_extract2t(expr);
    // XXX XXX XXX this knackers offsets
    get_value_set_rec(ref.source_value, dest, suffix, original_type);
    return;
  }

  get_value_set_rec(expr, dest, suffix, original_type);
}

void value_sett::get_reference_set(
  const expr2tc &expr,
  value_setst::valuest &dest) const
{
  // Fetch all the symbols expr refers to into this object map.
  object_mapt object_map;
  get_reference_set(expr, object_map);

  // Then convert to expressions into the destination list.
  for(object_mapt::const_iterator it = object_map.begin();
      it != object_map.end();
      it++)
    dest.push_back(to_expr(it));
}

void value_sett::get_reference_set_rec(const expr2tc &expr, object_mapt &dest)
  const
{
  if(
    is_symbol2t(expr) || is_dynamic_object2t(expr) ||
    is_constant_string2t(expr) || is_constant_array2t(expr))
  {
    // Any symbol we refer to, store into the destination object map.
    // Given that this is a simple symbol, we can be sure that the offset to
    // it is zero.
    insert(dest, expr, objectt(true, 0));
    return;
  }

  if(is_dereference2t(expr))
  {
    // The set of variables referred to here are the set of things the operand
    // may point at. So, find its value set, and return that.
    const dereference2t &deref = to_dereference2t(expr);
    get_value_set_rec(deref.value, dest, "", deref.type);
    return;
  }

  if(is_index2t(expr))
  {
    // This index may be dereferencing a pointer. So, get the reference set of
    // the source value, and store a reference to all those things.
    const index2t &index = to_index2t(expr);

    assert(
      is_array_type(index.source_value) || is_string_type(index.source_value));

    // Compute the offset introduced by this index.
    BigInt index_offset;
    bool has_const_index_offset = false;
    try
    {
      /* We put some effort into determining whether the offset is constant
       * since during symex many are and if they are overlooked we end up
       * building huge if-then-else chains in all the
       * dereference::construct_*_dyn_*_offset() methods. */
      expr2tc idx = index.index;
      simplify(idx);
      if(is_constant_int2t(idx))
      {
        index_offset =
          to_constant_int2t(idx).value * type_byte_size(index.type);
        has_const_index_offset = true;
      }
    }
    catch(const array_type2t::dyn_sized_array_excp &e)
    {
      // Not a constant index offset then.
    }

    object_mapt array_references;
    get_reference_set(index.source_value, array_references);

    for(const auto &a_it : array_references)
    {
      expr2tc object = object_numbering[a_it.first];

      if(is_unknown2t(object))
      {
        // Once an unknown, always an unknown.
        expr2tc unknown = unknown2tc(expr->type);
        insert(dest, unknown, BigInt(0));
      }
      else
      {
        // Whatever the base object is, apply the offset represented by this
        // index expression.
        objectt o = a_it.second;

        if(has_const_index_offset && index_offset == 0)
        {
          ;
        }
        else if(has_const_index_offset && o.offset_is_zero())
        {
          o.offset = index_offset;
        }
        else
        {
          // Non constant offset -- work out what the lowest alignment is.
          // Fetch the type size of the array index element.
          BigInt m = type_byte_size_default(index.type, 1);

          // This index operation, whatever the offset, will always multiply
          // by the size of the element type.
          uint64_t index_align = m.to_uint64();

          // Extract an offset from the old offset if set, otherwise the
          // alignment field.
          uint64_t old_align = (o.offset_is_set)
                                 ? offset2align(object, o.offset)
                                 : o.offset_alignment;

          o.offset_alignment = std::min(index_align, old_align);
          o.offset_is_set = false;
        }

        insert(dest, object, o);
      }
    }

    return;
  }

  if(is_member2t(expr))
  {
    // The set of things referred to here are all the things the struct source
    // value may refer to, plus an additional member operation. So, fetch that
    // reference set, and add the relevant offset to the offset expr.
    const member2t &memb = to_member2t(expr);
    BigInt offset_in_bytes;

    if(is_union_type(memb.source_value->type))
      offset_in_bytes = BigInt(0);
    else
      offset_in_bytes = member_offset(memb.source_value->type, memb.member);

    object_mapt struct_references;
    get_reference_set(memb.source_value, struct_references);

    for(const auto &it : struct_references)
    {
      expr2tc object = object_numbering[it.first];

      // An unknown or null base is /always/ unknown or null.
      if(
        is_unknown2t(object) || is_null_object2t(object) ||
        (is_typecast2t(object) && is_null_object2t(to_typecast2t(object).from)))
      {
        expr2tc unknown = unknown2tc(memb.type);
        insert(dest, unknown, BigInt(0));
      }
      else
      {
        objectt o = it.second;

        // XXX -- in terms of alignment, I believe this doesn't require
        // anything, as we're constructing an expression that takes account
        // of this. Also the same for references to indexes?
        if(o.offset_is_set)
          o.offset += offset_in_bytes;

        insert(dest, object, o);
      }
    }

    return;
  }

  if(is_if2t(expr))
  {
    // This if expr couldn't be simplified out; take the reference set of each
    // side.
    const if2t &anif = to_if2t(expr);
    get_reference_set_rec(anif.true_value, dest);
    get_reference_set_rec(anif.false_value, dest);
    return;
  }

  if(is_typecast2t(expr))
  {
    // Blast straight through typecasts.
    const typecast2t &cast = to_typecast2t(expr);
    get_reference_set_rec(cast.from, dest);
    return;
  }

  if(is_bitcast2t(expr))
  {
    // Blast straight through typecasts.
    const bitcast2t &cast = to_bitcast2t(expr);
    get_reference_set_rec(cast.from, dest);
    return;
  }

  if(is_byte_extract2t(expr))
  {
    // Address of byte extracts can refer to the object that is being extracted
    // from.
    const byte_extract2t &extract = to_byte_extract2t(expr);

    // This may or may not have a constant offset
    objectt o =
      (is_constant_int2t(extract.source_offset))
        ? objectt(true, to_constant_int2t(extract.source_offset).value)
        // Unclear what to do about alignments; default to nothing.
        : objectt(false, 1);

    insert(dest, extract.source_value, o);
    return;
  }

  if(is_concat2t(expr))
  {
    const concat2t &concat = to_concat2t(expr);
    get_reference_set_rec(concat.side_1, dest);
    get_reference_set_rec(concat.side_2, dest);
    return;
  }

  // If we didn't recognize the expression, then we have no idea what this
  // refers to, so store an unknown expr.
  log_debug(
    "value-set",
    "unknown expr {} of type {} -> inserting unknown reference",
    get_expr_id(expr),
    get_type_id(expr->type));
  expr2tc unknown = unknown2tc(expr->type);
  insert(dest, unknown, BigInt(0));
}

void value_sett::assign(
  const expr2tc &lhs,
  const expr2tc &rhs,
  bool add_to_sets)
{
  // Assignment interpretation.

  if(is_if2t(rhs))
  {
    // If the rhs could be either side of this if, perform the assigment of
    // either side. In case it refers to itself, assign to a temporary first,
    // then assign back.
    const if2t &ifref = to_if2t(rhs);

    // Build a sym specific to this type. Give l1 number to guard against
    // recursively entering this code path
    expr2tc xchg_sym =
      symbol2tc(lhs->type, xchg_name, symbol2t::level1, xchg_num++, 0, 0, 0);

    assign(xchg_sym, ifref.true_value, false);
    assign(xchg_sym, ifref.false_value, true);
    assign(lhs, xchg_sym, add_to_sets);

    erase(to_symbol2t(xchg_sym).get_symbol_name());
    return;
  }

  // Must have concrete type.
  assert(!is_symbol_type(lhs));
  const type2tc &lhs_type = lhs->type;

  if(is_struct_type(lhs_type) || is_union_type(lhs_type))
  {
    if(lhs_type->type_id == rhs->type->type_id)
    {
      /* either both union or both struct */
      assert(lhs_type->type_id == rhs->type->type_id);

      // Assign the values of all members of the rhs thing to the lhs. It's
      // sort-of-valid for the right hand side to be a superclass of the subclass,
      // in which case there are some fields not common between them, so we
      // iterate over the superclasses members.
      auto *rhs_data = static_cast<const struct_union_data *>(rhs->type.get());
      const std::vector<type2tc> &members = rhs_data->members;
      const std::vector<irep_idt> &member_names = rhs_data->member_names;

      for(size_t i = 0; i < members.size(); i++)
      {
        const type2tc &subtype = members[i];
        const irep_idt &name = member_names[i];

        // ignore methods
        if(is_code_type(subtype))
          continue;

        expr2tc lhs_member = member2tc(subtype, lhs, name);

        expr2tc rhs_member;
        if(is_unknown2t(rhs))
        {
          rhs_member = unknown2tc(subtype);
        }
        else if(is_invalid2t(rhs))
        {
          rhs_member = invalid2tc(subtype);
        }
        else
        {
          assert(
            base_type_eq(rhs->type, lhs_type, ns) ||
            is_subclass_of(lhs_type, rhs->type, ns));
          expr2tc rhs_member = make_member(rhs, name);

          // XXX -- shouldn't this be one level of indentation up?
          assign(lhs_member, rhs_member, add_to_sets);
        }
      }
    }
    else
    {
      /* types do not agree, this can happen during dereferences like this:
       *   struct S { int x; } a;
       *   int b;
       *   a = *(struct S *)&b;
       * and is caught as a dereference_failure by build_reference_to().
       *
       * Thus, we ignore this value-set assignment request here.
       */
    }
    return;
  }

  if(is_array_type(lhs_type))
  {
    const array_type2t &arr_type = to_array_type(lhs_type);
    expr2tc unknown = unknown2tc(
      arr_type.array_size ? arr_type.array_size->type : index_type2());
    expr2tc lhs_index = index2tc(arr_type.subtype, lhs, unknown);

    if(is_unknown2t(rhs) || is_invalid2t(rhs))
    {
      // Assign an unknown subtype value to the array's (unknown) index.
      expr2tc unknown_field = unknown2tc(arr_type.subtype);
      assign(lhs_index, unknown_field, add_to_sets);
    }
    else
    {
      assert(base_type_eq(rhs->type, lhs_type, ns));

      if(is_constant_array_of2t(rhs))
      {
        assign(lhs_index, to_constant_array_of2t(rhs).initializer, add_to_sets);
      }
      else if(is_constant_array2t(rhs) || is_constant_expr(rhs))
      {
        rhs->foreach_operand(
          [this, &add_to_sets, &lhs_index](const expr2tc &e) {
            assign(lhs_index, e, add_to_sets);
            add_to_sets = true;
          });
      }
      else if(is_with2t(rhs))
      {
        const with2t &with = to_with2t(rhs);

        expr2tc unknown = unknown2tc(index_type2());
        expr2tc idx = index2tc(arr_type.subtype, with.source_value, unknown);

        assign(lhs_index, idx, add_to_sets);
        assign(lhs_index, with.update_value, true);
      }
      else
      {
        expr2tc rhs_idx = index2tc(arr_type.subtype, rhs, unknown);
        assign(lhs_index, rhs_idx, true);
      }
    }
    return;
  }

  // basic type
  object_mapt values_rhs;
  get_value_set(rhs, values_rhs);
  assign_rec(lhs, values_rhs, "", add_to_sets);
}

void value_sett::do_free(const expr2tc &op)
{
  // op must be a pointer
  assert(is_pointer_type(op));

  // find out what it points to
  object_mapt value_set;
  get_value_set(op, value_set);

  // find out which *instances* interest us
  expr_sett to_mark;

  for(const auto &it : value_set)
  {
    const expr2tc &object = object_numbering[it.first];

    if(is_dynamic_object2t(object))
    {
      const dynamic_object2t &dynamic_object = to_dynamic_object2t(object);

      if(!dynamic_object.invalid)
      {
        to_mark.insert(dynamic_object.instance);
      }
    }
  }

  // mark these as 'may be invalid'
  // this, unfortunately, destroys the sharing
  for(auto &value : values)
  {
    object_mapt new_object_map;

    bool changed = false;

    for(object_mapt::const_iterator o_it = value.second.object_map.begin();
        o_it != value.second.object_map.end();
        o_it++)
    {
      const expr2tc &object = object_numbering[o_it->first];

      if(is_dynamic_object2t(object))
      {
        const expr2tc &instance = to_dynamic_object2t(object).instance;

        if(to_mark.count(instance) == 0)
          set(new_object_map, o_it);
        else
        {
          // adjust
          objectt o = o_it->second;
          dynamic_object2t new_dyn = to_dynamic_object2t(object); // copy
          new_dyn.invalid = false;
          new_dyn.unknown = true;
          insert(new_object_map, dynamic_object2tc(std::move(new_dyn)), o);
          changed = true;
        }
      }
      else
        set(new_object_map, o_it);
    }

    if(changed)
      value.second.object_map = new_object_map;
  }
}

void value_sett::assign_rec(
  const expr2tc &lhs,
  const object_mapt &values_rhs,
  const std::string &suffix,
  bool add_to_sets)
{
  if(is_symbol2t(lhs))
  {
    std::string identifier = to_symbol2t(lhs).get_symbol_name();

    if(add_to_sets)
      make_union(get_entry(identifier, suffix).object_map, values_rhs);
    else
      get_entry(identifier, suffix).object_map = values_rhs;
  }
  else if(is_dynamic_object2t(lhs))
  {
    const dynamic_object2t &dynamic_object = to_dynamic_object2t(lhs);

    if(is_unknown2t(dynamic_object.instance))
      return; // We're assigning to something unknown. Not much we can do.
    assert(is_constant_int2t(dynamic_object.instance));
    unsigned int idnum =
      to_constant_int2t(dynamic_object.instance).value.to_uint64();
    const std::string name = "value_set::dynamic_object" + i2string(idnum);

    make_union(get_entry(name, suffix).object_map, values_rhs);
  }
  else if(is_dereference2t(lhs))
  {
    object_mapt reference_set;
    get_reference_set(lhs, reference_set);

    if(reference_set.size() != 1)
      add_to_sets = true;

    for(const auto &it : reference_set)
    {
      const expr2tc obj = object_numbering[it.first];

      if(!is_unknown2t(obj))
        assign_rec(obj, values_rhs, suffix, add_to_sets);
    }
  }
  else if(is_index2t(lhs))
  {
    assert(
      is_array_type(to_index2t(lhs).source_value) ||
      is_string_type(to_index2t(lhs).source_value) ||
      is_dynamic_object2t(to_index2t(lhs).source_value));

    assign_rec(to_index2t(lhs).source_value, values_rhs, "[]" + suffix, true);
  }
  else if(is_member2t(lhs))
  {
    type2tc tmp;
    const member2t &member = to_member2t(lhs);
    const std::string &component_name = member.member.as_string();

    // Might travel through a dereference, in which case type resolving is
    // required
    const type2tc *ourtype = &member.source_value->type;
    if(is_symbol_type(*ourtype))
    {
      tmp = ns.follow(*ourtype);
      ourtype = &tmp;
    }

    assert(
      is_struct_type(*ourtype) || is_union_type(*ourtype) ||
      is_dynamic_object2t(member.source_value));

    assign_rec(
      to_member2t(lhs).source_value,
      values_rhs,
      "." + component_name + suffix,
      add_to_sets);
  }
  else if(
    is_constant_string2t(lhs) || is_null_object2t(lhs) ||
    is_valid_object2t(lhs) || is_deallocated_obj2t(lhs) ||
    is_dynamic_size2t(lhs) || is_constant_array2t(lhs))
  {
    // Ignored
  }
  else if(is_typecast2t(lhs))
  {
    assign_rec(to_typecast2t(lhs).from, values_rhs, suffix, add_to_sets);
  }
  else if(is_byte_extract2t(lhs))
  {
    assign_rec(to_byte_extract2t(lhs).source_value, values_rhs, suffix, true);
  }
  else
  {
    throw std::runtime_error("assign NYI: `{}'" + get_expr_id(lhs));
  }
}

void value_sett::do_function_call(
  const symbolt &symbol,
  const std::vector<expr2tc> &arguments)
{
  const code_typet &type = to_code_type(symbol.type);

  type2tc tmp_migrated_type = migrate_type(type);
  const code_type2t &migrated_type =
    dynamic_cast<const code_type2t &>(*tmp_migrated_type.get());

  const std::vector<type2tc> &argument_types = migrated_type.arguments;
  const std::vector<irep_idt> &argument_names = migrated_type.argument_names;

  // these first need to be assigned to dummy, temporary arguments
  // and only thereafter to the actuals, in order
  // to avoid overwriting actuals that are needed for recursive
  // calls

  for(unsigned i = 0; i < arguments.size(); i++)
  {
    const std::string identifier = "value_set::dummy_arg_" + i2string(i);
    add_var(identifier, "");

    expr2tc dummy_lhs;
    expr2tc tmp_arg = arguments[i];
    if(is_nil_expr(tmp_arg))
    {
      // As a workaround for the "--function" option, which feeds "nil"
      // arguments in here, take the expected function argument type rather
      // than the type from the argument.
      tmp_arg = unknown2tc(argument_types[i]);
      dummy_lhs = symbol2tc(argument_types[i], identifier);
    }
    else
    {
      dummy_lhs = symbol2tc(arguments[i]->type, identifier);
    }

    assign(dummy_lhs, tmp_arg, true);
  }

  // now assign to 'actual actuals'

  unsigned i = 0;

  std::vector<type2tc>::const_iterator it2 = argument_types.begin();
  for(std::vector<irep_idt>::const_iterator it = argument_names.begin();
      it != argument_names.end();
      it++, it2++)
  {
    const std::string &identifier = it->as_string();
    if(identifier == "")
      continue;

    add_var(identifier, "");

    expr2tc v_expr = symbol2tc(*it2, "value_set::dummy_arg_" + i2string(i));

    expr2tc actual_lhs = symbol2tc(*it2, identifier);
    assign(actual_lhs, v_expr, true);
    i++;
  }

  // And now delete the value set dummy args. They're going to end up
  // accumulating values from each function call that is made, which is a
  // bad plan.
  for(unsigned i = 0; i < arguments.size(); i++)
  {
    del_var("value_set::dummy_arg_" + i2string(i), "");
  }
}

void value_sett::do_end_function(const expr2tc &lhs)
{
  if(is_nil_expr(lhs))
    return;

  expr2tc rhs = symbol2tc(lhs->type, irep_idt("value_set::return_value"));

  assign(lhs, rhs);
}

void value_sett::apply_code(const expr2tc &code)
{
  if(is_code_block2t(code))
  {
    const code_block2t &ref = to_code_block2t(code);
    for(auto const &it : ref.operands)
      apply_code(it);
  }
  else if(is_code_assign2t(code))
  {
    const code_assign2t &ref = to_code_assign2t(code);
    assign(ref.target, ref.source);
  }
  else if(is_code_init2t(code))
  {
    const code_init2t &ref = to_code_init2t(code);
    assign(ref.target, ref.source);
  }
  else if(is_code_decl2t(code))
  {
    const code_decl2t &ref = to_code_decl2t(code);
    expr2tc sym = symbol2tc(ref.type, ref.value);
    expr2tc invalid = invalid2tc(ref.type);
    assign(sym, invalid);
  }
  else if(is_code_expression2t(code))
  {
    // can be ignored, we don't expect sideeffects here
  }
  else if(is_code_free2t(code))
  {
    // this may kill a valid bit
    const code_free2t &ref = to_code_free2t(code);
    do_free(ref.operand);
  }
  else if(is_code_printf2t(code))
  {
    // doesn't do anything
  }
  else if(is_code_return2t(code))
  {
    // this is turned into an assignment
    const code_return2t &ref = to_code_return2t(code);
    if(!is_nil_expr(ref.operand))
    {
      expr2tc sym = symbol2tc(ref.operand->type, "value_set::return_value");
      assign(sym, ref.operand);
    }
  }
  else if(is_code_asm2t(code))
  {
    // Ignore assembly. No idea why it isn't preprocessed out anyway.
  }
  else if(is_code_cpp_delete2t(code) || is_code_cpp_del_array2t(code))
  {
    // Ignore these too
  }
  else
  {
    std::ostringstream str;
    str << code << "\nvalue_sett: unexpected statement";
    throw std::runtime_error(str.str());
  }
}

expr2tc
value_sett::make_member(const expr2tc &src, const irep_idt &component_name)
{
  const type2tc &type = src->type;
  assert(is_struct_type(type) || is_union_type(type));

  auto *data = static_cast<const struct_union_data *>(type.get());
  const std::vector<type2tc> &members = data->members;

  if(is_constant_struct2t(src))
  {
    unsigned no = data->get_component_number(component_name);
    return to_constant_struct2t(src).datatype_members[no];
  }
  if(is_constant_union2t(src))
  {
    const constant_union2t &un = to_constant_union2t(src);
    if(un.init_field == component_name)
    {
      assert(un.datatype_members.size() == 1);
      return un.datatype_members[0];
    }
  }
  if(is_with2t(src))
  {
    const with2t &with = to_with2t(src);
    assert(is_constant_string2t(with.update_field));
    const constant_string2t &memb_name =
      to_constant_string2t(with.update_field);

    if(component_name == memb_name.value)
      // yes! just take op2
      return with.update_value;

    // no! do this recursively
    return make_member(with.source_value, component_name);
  }
  else if(is_typecast2t(src))
  {
    // push through typecast
    return make_member(to_typecast2t(src).from, component_name);
  }

  // give up
  unsigned no = data->get_component_number(component_name);
  const type2tc &subtype = members[no];
  expr2tc memb = member2tc(subtype, src, component_name);
  return memb;
}

void value_sett::dump() const
{
  std::ostringstream oss;
  output(oss);
  log_status("{}", oss.str());
}

void value_sett::obj_numbering_ref(unsigned int num)
{
  obj_numbering_refset[num]++;
}

void value_sett::obj_numbering_deref(unsigned int num)
{
  unsigned int refcount = --obj_numbering_refset[num];
  if(refcount == 0)
  {
    object_numbering.erase(num);
    obj_numbering_refset.erase(num);
  }
}
