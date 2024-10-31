#ifndef CPROVER_STD_TYPES_H
#define CPROVER_STD_TYPES_H

#include <cassert>
#include <util/mp_arith.h>
#include <util/std_expr.h>
#include <util/type.h>

class bool_typet : public typet
{
public:
  bool_typet()
  {
    id(t_bool);
  }
};

class empty_typet : public typet
{
public:
  empty_typet()
  {
    id(t_empty);
  }
};

class symbol_typet : public typet
{
public:
  symbol_typet() : typet(t_symbol)
  {
  }

  explicit symbol_typet(const irep_idt &identifier) : typet(t_symbol)
  {
    set_identifier(identifier);
  }

  void set_identifier(const irep_idt &identifier)
  {
    set(a_identifier, identifier);
  }

  const irep_idt &get_identifier() const
  {
    return get(a_identifier);
  }
};

/*! \brief Cast a generic typet to a \ref symbol_typet
 *
 * This is an unchecked conversion. \a type must be known to be \ref
 * symbol_typet.
 *
 * \param type Source type
 * \return Object of type \ref symbol_typet
 *
 * \ingroup gr_std_types
*/
extern inline const symbol_typet &to_symbol_type(const typet &type)
{
  assert(type.id() == "symbol");
  return static_cast<const symbol_typet &>(type);
}

/*! \copydoc to_symbol_type(const typet &)
 * \ingroup gr_std_types
*/
extern inline symbol_typet &to_symbol_type(typet &type)
{
  assert(type.id() == "symbol");
  return static_cast<symbol_typet &>(type);
}

// structs

class struct_union_typet : public typet
{
public:
  inline explicit struct_union_typet() = default;

  inline explicit struct_union_typet(const irep_idt &_id) : typet(_id)
  {
  }

  class componentt : public exprt
  {
  public:
    inline componentt() : exprt(a_component)
    {
    }

    inline componentt(const irep_idt &_name, const typet &_type)
      : exprt(a_component)
    {
      set_name(_name);
      type() = _type;
    }

    inline componentt(
      const irep_idt &_name,
      const irep_idt &_pretty_name,
      const typet &_type)
      : exprt(a_component)
    {
      set_name(_name);
      set_pretty_name(_pretty_name);
      type() = _type;
    }

    const irep_idt &get_name() const
    {
      return get(a_name);
    }

    void set_name(const irep_idt &name)
    {
      return set(a_name, name);
    }

    inline const irep_idt &get_base_name() const
    {
      return get("base_name");
    }

    inline void set_base_name(const irep_idt &base_name)
    {
      return set("base_name", base_name);
    }

    inline const irep_idt &get_access() const
    {
      return get("access");
    }

    inline void set_access(const irep_idt &access)
    {
      return set("access", access);
    }

    inline const irep_idt &get_pretty_name() const
    {
      return get("pretty_name");
    }

    inline void set_pretty_name(const irep_idt &name)
    {
      return set("pretty_name", name);
    }

    inline bool get_anonymous() const
    {
      return get_bool("anonymous");
    }

    inline void set_anonymous(bool anonymous)
    {
      return set("anonymous", anonymous);
    }

    bool get_is_padding() const
    {
      return get_bool("is_padding");
    }

    void set_is_padding(bool is_padding)
    {
      return set("is_padding", is_padding);
    }

    bool get_is_unnamed_bitfield() const
    {
      return get_bool("is_unnamed_bitfield");
    }

    void set_is_unnamed_bitfield(bool is_unnamed_bitfield)
    {
      return set("is_unnamed_bitfield", is_unnamed_bitfield);
    }
  };

  typedef std::vector<componentt> componentst;

  const componentst &components() const
  {
    return (const componentst &)(find(a_components).get_sub());
  }

  componentst &components()
  {
    return (componentst &)(add(a_components).get_sub());
  }

  bool has_component(const irep_idt &component_name) const
  {
    return get_component(component_name).is_not_nil();
  }

  const componentt &get_component(const irep_idt &component_name) const;

  unsigned component_number(const irep_idt &component_name) const;
  typet component_type(const irep_idt &component_name) const;

  const componentst &methods() const
  {
    return (const componentst &)(find(a_methods).get_sub());
  }

  componentst &methods()
  {
    return (componentst &)(add(a_methods).get_sub());
  }
};

extern inline const struct_union_typet &to_struct_union_type(const typet &type)
{
  assert(
    type.id() == typet::t_struct || type.id() == typet::t_union ||
    type.id() == typet::t_class);
  return static_cast<const struct_union_typet &>(type);
}

extern inline struct_union_typet &to_struct_union_type(typet &type)
{
  assert(
    type.id() == typet::t_struct || type.id() == typet::t_union ||
    type.id() == typet::t_class);
  return static_cast<struct_union_typet &>(type);
}

class struct_typet : public struct_union_typet
{
public:
  struct_typet() : struct_union_typet(t_struct)
  {
  }

  bool is_prefix_of(const struct_typet &other) const;
};

extern inline const struct_typet &to_struct_type(const typet &type)
{
  assert(
    type.id() == typet::t_struct || type.id() == typet::t_union ||
    type.id() == typet::t_class);
  return static_cast<const struct_typet &>(type);
}

extern inline struct_typet &to_struct_type(typet &type)
{
  assert(
    type.id() == typet::t_struct || type.id() == typet::t_union ||
    type.id() == typet::t_class);
  return static_cast<struct_typet &>(type);
}

class union_typet : public struct_union_typet
{
public:
  union_typet() : struct_union_typet(t_union)
  {
  }
};

extern inline const union_typet &to_union_type(const typet &type)
{
  assert(type.id() == typet::t_union);
  return static_cast<const union_typet &>(type);
}

extern inline union_typet &to_union_type(typet &type)
{
  assert(type.id() == typet::t_union);
  return static_cast<union_typet &>(type);
}

// functions

class code_typet : public typet
{
public:
  class argumentt;
  typedef std::vector<argumentt> argumentst;

  code_typet()
  {
    id(t_code);
  }

  code_typet(argumentst _parameters, typet _return_type) : typet(t_code)
  {
    arguments().swap(_parameters);
    return_type().swap(_return_type);
  }

  class argumentt : public exprt
  {
  public:
    argumentt() : exprt(argument)
    {
    }

    argumentt(const typet &type) : exprt(argument, type)
    {
    }

    const exprt &default_value() const
    {
      return find_expr("#default_value");
    }

    bool has_default_value() const
    {
      return default_value().is_not_nil();
    }

    exprt &default_value()
    {
      return add_expr("#default_value");
    }

    void set_identifier(const irep_idt &identifier)
    {
      cmt_identifier(identifier);
    }

    void set_base_name(const irep_idt &name)
    {
      cmt_base_name(name);
    }

    const irep_idt &get_identifier() const
    {
      return cmt_identifier();
    }

    const irep_idt &get_base_name() const
    {
      return cmt_base_name();
    }
  };

  bool has_ellipsis() const
  {
    return find(a_arguments).ellipsis();
  }

  void make_ellipsis()
  {
    add(a_arguments).ellipsis(true);
  }

  const typet &return_type() const
  {
    return find_type("return_type");
  }

  typet &return_type()
  {
    return add_type("return_type");
  }

  const argumentst &arguments() const
  {
    return (const argumentst &)find(a_arguments).get_sub();
  }

  argumentst &arguments()
  {
    return (argumentst &)add(a_arguments).get_sub();
  }

  inline bool get_inlined() const
  {
    return get_bool("#inlined");
  }

  inline void set_inlined(bool value)
  {
    set("#inlined", value);
  }
};

extern inline const code_typet &to_code_type(const typet &type)
{
  assert(type.id() == typet::t_code);
  return static_cast<const code_typet &>(type);
}

extern inline code_typet &to_code_type(typet &type)
{
  if(type.id() != typet::t_code)
  {
    type.dump();
  }
  assert(type.id() == typet::t_code);
  return static_cast<code_typet &>(type);
}

class array_typet : public typet
{
public:
  array_typet() : typet(t_array)
  {
  }

  array_typet(const typet &_subtype) : typet(t_array)
  {
    subtype() = _subtype;
  }

  array_typet(const typet &_subtype, const exprt &_size) : typet(t_array)
  {
    subtype() = _subtype;
    size() = _size;
  }

  const exprt &size() const
  {
    return (const exprt &)find(a_size);
  }

  exprt &size()
  {
    return (exprt &)add(a_size);
  }
};

extern inline const array_typet &to_array_type(const typet &type)
{
  assert(type.id() == typet::t_array);
  return static_cast<const array_typet &>(type);
}

extern inline array_typet &to_array_type(typet &type)
{
  assert(type.id() == typet::t_array);
  return static_cast<array_typet &>(type);
}

class pointer_typet : public typet
{
public:
  pointer_typet()
  {
    id(t_pointer);
  }

  explicit pointer_typet(const typet &_subtype)
  {
    id(t_pointer);
    subtype() = _subtype;
  }
};

/// \brief Cast a typet to a \ref pointer_typet
///
/// This is an unchecked conversion. \a type must be known to be \ref
/// pointer_typet. Will fail with a precondition violation if type
/// doesn't match.
///
/// \param type: Source type.
/// \return Object of type \ref pointer_typet.
inline const pointer_typet &to_pointer_type(const typet &type)
{
  assert(type.id() == "pointer");
  return static_cast<const pointer_typet &>(type);
}

/// \copydoc to_pointer_type(const typet &)
inline pointer_typet &to_pointer_type(typet &type)
{
  assert(type.id() == "pointer");
  return static_cast<pointer_typet &>(type);
}

class reference_typet : public pointer_typet
{
public:
  reference_typet()
  {
    set(t_reference, true);
  }
};

bool is_reference(const typet &type);
bool is_rvalue_reference(const typet &type);
bool is_lvalue_or_rvalue_reference(const typet &type);

class bv_typet : public typet
{
public:
  bv_typet()
  {
    id(t_bv);
  }

  explicit bv_typet(unsigned width)
  {
    id(t_bv);
    set_width(width);
  }

  unsigned get_width() const;

  void set_width(unsigned width)
  {
    set(a_width, width);
  }
};

class unsignedbv_typet : public bv_typet
{
public:
  unsignedbv_typet()
  {
    id(t_unsignedbv);
  }

  explicit unsignedbv_typet(unsigned width)
  {
    id(t_unsignedbv);
    set_width(width);
  }

  BigInt smallest() const;
  BigInt largest() const;
  constant_exprt smallest_expr() const;
  constant_exprt zero_expr() const;
  constant_exprt largest_expr() const;
};

/*! \brief Cast a generic typet to an \ref unsignedbv_typet
 *
 * This is an unchecked conversion. \a type must be known to be \ref
 * unsignedbv_typet.
 *
 * \param type Source type
 * \return Object of type \ref unsignedbv_typet
 *
 * \ingroup gr_std_types
*/
inline const unsignedbv_typet &to_unsignedbv_type(const typet &type)
{
  assert(type.id() == "unsignedbv");
  return static_cast<const unsignedbv_typet &>(type);
}

/*! \copydoc to_unsignedbv_type(const typet &)
 * \ingroup gr_std_types
*/
inline unsignedbv_typet &to_unsignedbv_type(typet &type)
{
  assert(type.id() == "unsignedbv");
  return static_cast<unsignedbv_typet &>(type);
}

class signedbv_typet : public bv_typet
{
public:
  signedbv_typet()
  {
    id(t_signedbv);
  }

  explicit signedbv_typet(unsigned width)
  {
    id(t_signedbv);
    set_width(width);
  }

  BigInt smallest() const;
  BigInt largest() const;
  constant_exprt smallest_expr() const;
  constant_exprt zero_expr() const;
  constant_exprt largest_expr() const;
};

/*! \brief Cast a generic typet to a \ref signedbv_typet
 *
 * This is an unchecked conversion. \a type must be known to be \ref
 * signedbv_typet.
 *
 * \param type Source type
 * \return Object of type \ref signedbv_typet
 *
 * \ingroup gr_std_types
*/
inline const signedbv_typet &to_signedbv_type(const typet &type)
{
  assert(type.id() == "signedbv");
  return static_cast<const signedbv_typet &>(type);
}

/*! \copydoc to_signedbv_type(const typet &)
 * \ingroup gr_std_types
*/
inline signedbv_typet &to_signedbv_type(typet &type)
{
  assert(type.id() == "signedbv");
  return static_cast<signedbv_typet &>(type);
}
class fixedbv_typet : public bv_typet
{
public:
  fixedbv_typet()
  {
    id(t_fixedbv);
  }

  unsigned get_fraction_bits() const
  {
    return get_width() - get_integer_bits();
  }

  unsigned get_integer_bits() const;

  void set_integer_bits(unsigned b)
  {
    set(a_integer_bits, b);
  }

  friend const fixedbv_typet &to_fixedbv_type(const typet &type)
  {
    assert(type.id() == t_fixedbv);
    return static_cast<const fixedbv_typet &>(type);
  }
};

const fixedbv_typet &to_fixedbv_type(const typet &type);

class floatbv_typet : public bv_typet
{
public:
  floatbv_typet()
  {
    id(t_floatbv);
  }

  unsigned get_e() const
  {
    return get_width() - get_f() - 1;
  }

  unsigned get_f() const;

  void set_f(unsigned b)
  {
    set(a_f, b);
  }

  friend const floatbv_typet &to_floatbv_type(const typet &type)
  {
    assert(type.id() == t_floatbv);
    return static_cast<const floatbv_typet &>(type);
  }
};

const floatbv_typet &to_floatbv_type(const typet &type);

/**
 * @brief This type maps the vectors
 *
 * Vectors have similar access operations of arrays,
 * however they can also do arithmetic between integers
 * floats, and other vector (of the same size)
 *
 */
class vector_typet : public typet
{
public:
  vector_typet() : typet(t_vector)
  {
  }

  vector_typet(const typet &_subtype) : typet(t_vector)
  {
    subtype() = _subtype;
  }

  vector_typet(const typet &_subtype, const exprt &_size) : typet(t_vector)
  {
    subtype() = _subtype;
    size() = _size;
  }

  const exprt &size() const
  {
    return (const exprt &)find(a_size);
  }

  exprt &size()
  {
    return (exprt &)add(a_size);
  }
};

inline const vector_typet &to_vector_type(const typet &type)
{
  assert(type.id() == typet::t_vector);
  return static_cast<const vector_typet &>(type);
}

class intcap_typet : public typet
{
public:
  intcap_typet() : typet(t_intcap)
  {
  }
};

class uintcap_typet : public typet
{
public:
  uintcap_typet() : typet(t_uintcap)
  {
  }
};

#endif
