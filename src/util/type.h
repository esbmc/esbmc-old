#ifndef CPROVER_TYPE_H
#define CPROVER_TYPE_H

#include <list>
#include <util/irep.h>
#include <util/location.h>

class typet : public irept
{
public:
  typet() = default;

  explicit typet(const irep_idt &_id) : irept(_id)
  {
  }

  const typet &subtype() const
  {
    return (typet &)find(f_subtype);
  }

  typet &subtype()
  {
    return (typet &)add(f_subtype);
  }

  typedef std::vector<typet> subtypest;

  subtypest &subtypes()
  {
    return (subtypest &)add(f_subtypes).get_sub();
  }

  const subtypest &subtypes() const
  {
    return (const subtypest &)find(f_subtypes).get_sub();
  }

  bool has_subtypes() const
  {
    return !find(f_subtypes).is_nil();
  }

  bool has_subtype() const
  {
    return !find(f_subtype).is_nil();
  }

  void move_to_subtypes(typet &type); // destroys expr

  const locationt &location() const
  {
    return (const locationt &)find(f_location);
  }

  locationt &location()
  {
    return (locationt &)add(f_location);
  }

  typet &add_type(const std::string &name)
  {
    return (typet &)add(name);
  }

  const typet &find_type(const std::string &name) const
  {
    return (const typet &)find(name);
  }

  bool can_carry_provenance() const
  {
    return get_bool(f_can_carry_provenance);
  }

  void can_carry_provenance(bool yes)
  {
    set(f_can_carry_provenance, yes);
  }

  static irep_idt t_integer;
  static irep_idt t_signedbv;
  static irep_idt t_unsignedbv;
  static irep_idt t_rational;
  static irep_idt t_real;
  static irep_idt t_natural;
  static irep_idt t_complex;
  static irep_idt t_floatbv;
  static irep_idt t_fixedbv;
  static irep_idt t_bool;
  static irep_idt t_empty;
  static irep_idt t_symbol; // There're expressions of id "symbol" and types
                            // of id "symbol".
  static irep_idt t_struct;
  static irep_idt t_union;
  static irep_idt t_class;
  static irep_idt t_code;
  static irep_idt t_array;
  static irep_idt t_pointer;
  static irep_idt t_reference;
  static irep_idt t_bv;
  static irep_idt t_vector;

  static irep_idt t_intcap;
  static irep_idt t_uintcap;

  static irep_idt a_identifier;
  static irep_idt a_name;
  static irep_idt a_components;
  static irep_idt a_methods;
  static irep_idt a_arguments;
  static irep_idt a_return_type;
  static irep_idt a_size;
  static irep_idt a_width;
  static irep_idt a_integer_bits;
  static irep_idt a_f;

protected:
  static irep_idt f_subtype;
  static irep_idt f_subtypes;
  static irep_idt f_location;
  static irep_idt f_can_carry_provenance;
};

typedef std::list<typet> type_listt;

#define forall_type_list(it, type)                                             \
  for (type_listt::const_iterator it = (type).begin(); it != (type).end(); it++)

#define Forall_type_list(it, type)                                             \
  for (type_listt::iterator it = (type).begin(); it != (type).end(); it++)

#define forall_subtypes(it, type)                                              \
  if ((type).has_subtypes())                                                   \
    for (typet::subtypest::const_iterator it = (type).subtypes().begin();      \
         it != (type).subtypes().end();                                        \
         it++)

#define Forall_subtypes(it, type)                                              \
  if ((type).has_subtypes())                                                   \
    for (typet::subtypest::iterator it = (type).subtypes().begin();            \
         it != (type).subtypes().end();                                        \
         it++)

/*

pre-defined types:
  universe      // super type
  type          // another type
  predicate     // predicate expression (subtype and predicate)
  uninterpreted // uninterpreted type with identifier
  empty         // void
  bool          // true or false
  abstract      // abstract super type
  struct        // with components: each component has name and type
                // the ordering matters
  rational
  real
  integer
  complex
  string
  enum          // with elements
                // the ordering does not matter
  tuple         // with components: each component has type
                // the ordering matters
  mapping       // domain -> range
  bv            // no interpretation
  unsignedbv
  signedbv      // two's complement
  floatbv       // IEEE floating point format
  code
  pointer       // for ANSI-C (subtype)
  symbol        // look in symbol table (identifier)
  number        // generic number super type

*/

bool is_number(const typet &type);
// rational, real, integer, complex, unsignedbv, signedbv, floatbv
/**
 * @brief Returns wether the type behaves similarly to an array
 *
 * This is mainly used for DRY, e.g. checking for vector and array types.
 *
 * @param type
 * @return true
 * @return false
 */
bool is_array_like(const typet &type);

#endif
