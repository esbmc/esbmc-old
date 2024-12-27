#ifndef IREP2_H_
#define IREP2_H_

/** @file irep2.h
 *  Classes and definitions for non-stringy internal representation.
 */

#include <big-int/bigint.hh>
#include <boost/bind/placeholders.hpp>
#include <boost/crc.hpp>
#include <boost/functional/hash_fwd.hpp>
#include <boost/fusion/include/equal_to.hpp>
#include <boost/mpl/bool.hpp>
#include <boost/mpl/empty.hpp>
#include <boost/mpl/equal.hpp>
#include <boost/mpl/front.hpp>
#include <boost/mpl/if.hpp>
#include <boost/mpl/insert.hpp>
#include <boost/mpl/not.hpp>
#include <boost/mpl/pop_front.hpp>
#include <boost/mpl/push_front.hpp>
#include <boost/mpl/set.hpp>
#include <boost/mpl/size.hpp>
#include <boost/mpl/transform.hpp>
#include <boost/mpl/vector.hpp>
#include <boost/preprocessor/list/adt.hpp>
#include <boost/preprocessor/list/for_each.hpp>
#include <cstdarg>
#include <functional>
#include <util/compiler_defs.h>
#include <util/crypto_hash.h>
#include <util/dstring.h>
#include <util/irep.h>
#include <vector>

// Ahead of time: a list of all expressions and types, in a preprocessing
// list, for enumerating later. Should avoid manually enumerating anywhere
// else.

// clang-format off
#define ESBMC_LIST_OF_EXPRS                                                    \
  BOOST_PP_LIST_CONS(constant_int,                                             \
  BOOST_PP_LIST_CONS(constant_fixedbv,                                         \
  BOOST_PP_LIST_CONS(constant_floatbv,                                         \
  BOOST_PP_LIST_CONS(constant_bool,                                            \
  BOOST_PP_LIST_CONS(constant_string,                                          \
  BOOST_PP_LIST_CONS(constant_struct,                                          \
  BOOST_PP_LIST_CONS(constant_union,                                           \
  BOOST_PP_LIST_CONS(constant_array,                                           \
  BOOST_PP_LIST_CONS(constant_vector,                                          \
  BOOST_PP_LIST_CONS(constant_array_of,                                        \
  BOOST_PP_LIST_CONS(symbol,                                                   \
  BOOST_PP_LIST_CONS(typecast,                                                 \
  BOOST_PP_LIST_CONS(bitcast,                                                  \
  BOOST_PP_LIST_CONS(nearbyint,                                                \
  BOOST_PP_LIST_CONS(if,                                                       \
  BOOST_PP_LIST_CONS(equality,                                                 \
  BOOST_PP_LIST_CONS(notequal,                                                 \
  BOOST_PP_LIST_CONS(lessthan,                                                 \
  BOOST_PP_LIST_CONS(greaterthan,                                              \
  BOOST_PP_LIST_CONS(lessthanequal,                                            \
  BOOST_PP_LIST_CONS(greaterthanequal,                                         \
  BOOST_PP_LIST_CONS(not,                                                      \
  BOOST_PP_LIST_CONS(and,                                                      \
  BOOST_PP_LIST_CONS(or,                                                       \
  BOOST_PP_LIST_CONS(xor,                                                      \
  BOOST_PP_LIST_CONS(implies,                                                  \
  BOOST_PP_LIST_CONS(bitand,                                                   \
  BOOST_PP_LIST_CONS(bitor,                                                    \
  BOOST_PP_LIST_CONS(bitxor,                                                   \
  BOOST_PP_LIST_CONS(bitnand,                                                  \
  BOOST_PP_LIST_CONS(bitnor,                                                   \
  BOOST_PP_LIST_CONS(bitnxor,                                                  \
  BOOST_PP_LIST_CONS(bitnot,                                                   \
  BOOST_PP_LIST_CONS(lshr,                                                     \
  BOOST_PP_LIST_CONS(neg,                                                      \
  BOOST_PP_LIST_CONS(abs,                                                      \
  BOOST_PP_LIST_CONS(add,                                                      \
  BOOST_PP_LIST_CONS(sub,                                                      \
  BOOST_PP_LIST_CONS(mul,                                                      \
  BOOST_PP_LIST_CONS(div,                                                      \
  BOOST_PP_LIST_CONS(ieee_add,                                                 \
  BOOST_PP_LIST_CONS(ieee_sub,                                                 \
  BOOST_PP_LIST_CONS(ieee_mul,                                                 \
  BOOST_PP_LIST_CONS(ieee_div,                                                 \
  BOOST_PP_LIST_CONS(ieee_fma,                                                 \
  BOOST_PP_LIST_CONS(ieee_sqrt,                                                \
  BOOST_PP_LIST_CONS(popcount,                                                 \
  BOOST_PP_LIST_CONS(bswap,                                                    \
  BOOST_PP_LIST_CONS(modulus,                                                  \
  BOOST_PP_LIST_CONS(shl,                                                      \
  BOOST_PP_LIST_CONS(ashr,                                                     \
  BOOST_PP_LIST_CONS(dynamic_object,                                           \
  BOOST_PP_LIST_CONS(same_object,                                              \
  BOOST_PP_LIST_CONS(pointer_offset,                                           \
  BOOST_PP_LIST_CONS(pointer_object,                                           \
  BOOST_PP_LIST_CONS(pointer_capability,                                       \
  BOOST_PP_LIST_CONS(address_of,                                               \
  BOOST_PP_LIST_CONS(byte_extract,                                             \
  BOOST_PP_LIST_CONS(byte_update,                                              \
  BOOST_PP_LIST_CONS(with,                                                     \
  BOOST_PP_LIST_CONS(member,                                                   \
  BOOST_PP_LIST_CONS(index,                                                    \
  BOOST_PP_LIST_CONS(isnan,                                                    \
  BOOST_PP_LIST_CONS(overflow,                                                 \
  BOOST_PP_LIST_CONS(overflow_cast,                                            \
  BOOST_PP_LIST_CONS(overflow_neg,                                             \
  BOOST_PP_LIST_CONS(unknown,                                                  \
  BOOST_PP_LIST_CONS(invalid,                                                  \
  BOOST_PP_LIST_CONS(null_object,                                              \
  BOOST_PP_LIST_CONS(dereference,                                              \
  BOOST_PP_LIST_CONS(valid_object,                                             \
  BOOST_PP_LIST_CONS(deallocated_obj,                                          \
  BOOST_PP_LIST_CONS(dynamic_size,                                             \
  BOOST_PP_LIST_CONS(sideeffect,                                               \
  BOOST_PP_LIST_CONS(code_block,                                               \
  BOOST_PP_LIST_CONS(code_assign,                                              \
  BOOST_PP_LIST_CONS(code_init,                                                \
  BOOST_PP_LIST_CONS(code_decl,                                                \
  BOOST_PP_LIST_CONS(code_dead,                                                \
  BOOST_PP_LIST_CONS(code_printf,                                              \
  BOOST_PP_LIST_CONS(code_expression,                                          \
  BOOST_PP_LIST_CONS(code_return,                                              \
  BOOST_PP_LIST_CONS(code_skip,                                                \
  BOOST_PP_LIST_CONS(code_free,                                                \
  BOOST_PP_LIST_CONS(code_goto,                                                \
  BOOST_PP_LIST_CONS(object_descriptor,                                        \
  BOOST_PP_LIST_CONS(code_function_call,                                       \
  BOOST_PP_LIST_CONS(code_comma,                                               \
  BOOST_PP_LIST_CONS(invalid_pointer,                                          \
  BOOST_PP_LIST_CONS(code_asm,                                                 \
  BOOST_PP_LIST_CONS(code_cpp_del_array,                                       \
  BOOST_PP_LIST_CONS(code_cpp_delete,                                          \
  BOOST_PP_LIST_CONS(code_cpp_catch,                                           \
  BOOST_PP_LIST_CONS(code_cpp_throw,                                           \
  BOOST_PP_LIST_CONS(code_cpp_throw_decl,                                      \
  BOOST_PP_LIST_CONS(code_cpp_throw_decl_end,                                  \
  BOOST_PP_LIST_CONS(isinf,                                                    \
  BOOST_PP_LIST_CONS(isnormal,                                                 \
  BOOST_PP_LIST_CONS(isfinite,                                                 \
  BOOST_PP_LIST_CONS(signbit,                                                  \
  BOOST_PP_LIST_CONS(concat,                                                   \
  BOOST_PP_LIST_CONS(extract,                                                  \
  BOOST_PP_LIST_NIL))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))

#define ESBMC_LIST_OF_TYPES                                                    \
  BOOST_PP_LIST_CONS(bool,                                                     \
  BOOST_PP_LIST_CONS(empty,                                                    \
  BOOST_PP_LIST_CONS(symbol,                                                   \
  BOOST_PP_LIST_CONS(struct,                                                   \
  BOOST_PP_LIST_CONS(union,                                                    \
  BOOST_PP_LIST_CONS(code,                                                     \
  BOOST_PP_LIST_CONS(array,                                                    \
  BOOST_PP_LIST_CONS(vector,                                                   \
  BOOST_PP_LIST_CONS(pointer,                                                  \
  BOOST_PP_LIST_CONS(unsignedbv,                                               \
  BOOST_PP_LIST_CONS(signedbv,                                                 \
  BOOST_PP_LIST_CONS(fixedbv,                                                  \
  BOOST_PP_LIST_CONS(string,                                                   \
  BOOST_PP_LIST_CONS(cpp_name,                                                 \
  BOOST_PP_LIST_NIL))))))))))))))
// clang-format on

// Even crazier forward decs,
namespace esbmct
{
template <typename... Args>
class expr2t_traits;
template <typename... Args>
class type2t_traits;
} // namespace esbmct

class type2t;
class expr2t;
class constant_array2t;
class constant_vector2t;

/** Reference counted container for expr2t based classes.
 *  This class extends boost shared_ptr's to contain anything that's a subclass
 *  of expr2t. It provides several ways of accessing the contained pointer;
 *  crucially it ensures that the only way to get a non-const reference or
 *  pointer is via the get() method, which call the detach() method.
 *
 *  This exists to ensure that we honour the model set forth by the old string
 *  based internal representation - specifically, that if you performed a const
 *  operation on an irept (fetching data) then the contained piece of data
 *  could continue to be shared between numerous data structures, for example
 *  a piece of code could exist in a contextt, a namespacet, and a goto_programt
 *  and all would share the same contained data structure, preventing additional
 *  memory consumption.
 *
 *  If anything copied an irept from one of these place it'd also share that
 *  contained data; but if it made a modifying operation (add, set, or just
 *  taking a non-const reference the contained data,) then the detach() method
 *  would be called, which duplicated the contained item and let the current
 *  piece of code modify the duplicate copy, while all the other storage
 *  locations continued to share the original.
 *
 *  So yeah, that's what this class attempts to implement, via the medium of
 *  std::shared_ptr. However, to the outside the shared_ptr is not accessible
 *  since that would break the const guarantees for operator* and .get() which
 *  this class provides.
 */
template <class T>
class irep_container : private std::shared_ptr<T>
{
public:
  constexpr irep_container() noexcept = default;
  constexpr irep_container(const irep_container &ref) = default;
  constexpr irep_container(irep_container &&ref) = default;

  irep_container &operator=(irep_container const &ref) = default;
  irep_container &operator=(irep_container &&ref) = default;

  // Move-construct from any std::shared_ptr of this type. That just moves the
  // reference over and leaves our caller with an empty shared_ptr. Doesn't
  // prevent copies from the original 'p' to exist, though.
  // Obviously this is fairly unwise because any std::shared_ptr
  // won't be using the detach facility to manipulate things, however it's
  // necessary for std::make_shared.
  explicit irep_container(std::shared_ptr<T> &&p)
    : std::shared_ptr<T>(std::move(p))
  {
  }

  /* provide own definitions for
   *   operator*
   *   operator->
   *   get()
   * to account for const-ness and detach if necessary.
   *
   * This interface is not 'equal' to std::shared_ptr's in the sense of
   * 'override' precisely because the const-ness of *this is moved to the
   * pointee, which std::shared_ptr doesn't do. We can reuse the noexcept
   * guarantee, though.
   */

  // the const versions just forward
  const T &operator*() const noexcept
  {
    return *get();
  }

  const T *operator->() const noexcept
  {
    return get();
  }

  const T *get() const noexcept
  {
    return std::shared_ptr<T>::get();
  }

  // the non-const versions detach
  T *get() // never throws
  {
    detach();
    T *tmp = std::shared_ptr<T>::get();
    tmp->crc_val = 0;
    return tmp;
  }

  T &operator*()
  {
    return *get();
  }

  T *operator->() // never throws
  {
    return get();
  }

  void detach()
  {
    /* TODO threads: this is unsafe for multi-threaded execution
     *
     * From the docs: In multithreaded environment, the value returned by
     * use_count is approximate (typical implementations use a
     * memory_order_relaxed load). */
    if (this->use_count() == 1)
      return; // No point remunging oneself if we're the only user of the ptr.

    // Assign-operate ourself into containing a fresh copy of the data. This
    // creates a new reference counted object, and assigns it to ourself,
    // which causes the existing reference to be decremented.
    const T *foo = std::shared_ptr<T>::get();
    *this = foo->clone();
  }

  using std::shared_ptr<T>::operator bool;
  using std::shared_ptr<T>::reset;

  friend void swap(irep_container &a, irep_container &b)
  {
    using std::swap;
    swap(
      static_cast<std::shared_ptr<T> &>(a),
      static_cast<std::shared_ptr<T> &>(b));
  }

  void swap(irep_container &b)
  {
    std::shared_ptr<T>::swap(b);
  }

  irep_container simplify() const
  {
    const T *foo = get();
    return foo->simplify();
  }

  size_t crc() const
  {
    const T *foo = get();
    if (foo->crc_val != 0)
      return foo->crc_val;

    return foo->do_crc();
  }

  /* Provide comparison operators here as inline friends so they don't pollute
   * the outer namespace; this reduces clutter when there are error messages
   * about these infix operators. It also means that no user-defined
   * conversions are considered unless at least one operand has the type of
   * this class or is derived from it. This is usually wanted since supplying
   * those conversions means someone else has to care about comparing whatever
   * values they potentially convert...
   *
   * This implementation assumes that the type T is totally ordered.
   *
   * TODO: when switching to >= C++20, replace these with only operator== and
   * operator<=>
   */

  friend bool operator==(const irep_container &a, const irep_container &b)
  {
    if (same(a, b))
      return true;

    if (!a || !b)
      return false;

    return *a == *b; // different pointees could still compare equal
  }

  friend bool operator!=(const irep_container &a, const irep_container &b)
  {
    return !(a == b);
  }

  friend bool operator<(const irep_container &a, const irep_container &b)
  {
    if (!b)
      return false; // If b is nil, nothing can be lower
    if (!a)
      return true; // nil is lower than non-nil

    if (same(a, b))
      return false;

    return *a < *b;
  }

  friend bool operator<=(const irep_container &a, const irep_container &b)
  {
    return !(a > b);
  }

  friend bool operator>=(const irep_container &a, const irep_container &b)
  {
    return !(a < b);
  }

  friend bool operator>(const irep_container &a, const irep_container &b)
  {
    return b < a;
  }

private:
  static bool same(const irep_container &a, const irep_container &b) noexcept
  {
    /* Note: Can't reliably test equality on pointers directly, see
     * <https://eel.is/c++draft/expr.eq#3.1>
     * Instead we'll use the implementation-defined total order guaranteed by
     * std::less. */
    const T *p = a.get(), *q = b.get();
    if (!std::less{}(p, q) && !std::less{}(q, p))
      return true; /* target is identical */
    return false;
  }
};

typedef irep_container<type2t> type2tc;
typedef irep_container<expr2t> expr2tc;

typedef std::pair<std::string, std::string> member_entryt;
typedef std::list<member_entryt> list_of_memberst;

class irep2t : public std::enable_shared_from_this<irep2t>
{
};

/** Base class for all types.
 *  Contains only a type identifier enumeration - for some types (such as bool,
 *  or empty,) there's no need for any significant amount of data to be stored.
 */
class type2t : public irep2t
{
public:
  /** Enumeration identifying each sort of type. */
  enum type_ids
  {
    bool_id,
    empty_id,
    symbol_id,
    struct_id,
    union_id,
    code_id,
    array_id,
    vector_id,
    pointer_id,
    unsignedbv_id,
    signedbv_id,
    fixedbv_id,
    floatbv_id,
    cpp_name_id,
    end_type_id
  };

  /* Define default traits */
  typedef typename esbmct::type2t_traits<> traits;

  /** Symbolic type exception class.
   *  To be thrown when attempting to fetch the width of a symbolic type, such
   *  as empty or code. Caller will have to worry about what to do about that.
   */
  class symbolic_type_excp
  {
  public:
    const char *what() const noexcept
    {
      return "symbolic type encountered";
    }
  };

  typedef std::function<void(const type2tc &t)> const_subtype_delegate;
  typedef std::function<void(type2tc &t)> subtype_delegate;

protected:
  /** Primary constructor.
   *  @param id Type ID of type being constructed
   */
  type2t(type_ids id);

  /** Copy constructor */
  type2t(const type2t &ref) = default;

  virtual void foreach_subtype_impl_const(const_subtype_delegate &t) const = 0;
  virtual void foreach_subtype_impl(subtype_delegate &t) = 0;

public:
  // Provide base / container types for some templates stuck on top:
  typedef type2tc container_type;
  typedef type2t base_type;

  virtual ~type2t() = default;

  /** Fetch bit width of this type.
   *  For a particular type, calculate its size in a bit representation of
   *  itself. May throw various exceptions depending on whether this operation
   *  is viable - for example, for symbol types, infinite sized or dynamically
   *  sized arrays.
   *
   *  Note that the bit width is _not_ the same as the ansi-c byte model
   *  representation of this type.
   *
   *  @throws symbolic_type_excp
   *  @throws array_type2t::inf_sized_array_excp
   *  @throws array_type2t::dyn_sized_array_excp
   *  @return Size of types byte representation, in bits
   */
  virtual unsigned int get_width() const = 0;

  bool operator==(const type2t &ref) const;
  bool operator!=(const type2t &ref) const;
  bool operator<(const type2t &ref) const;

  /** Produce a string representation of type.
   *  Takes body of the current type and produces a human readable
   *  representation. Similar to the string-irept's pretty method, although a
   *  different format.
   *  @param indent Number of spaces to indent lines by in the output
   *  @return String obj containing representation of this object
   */
  std::string pretty(unsigned int indent = 0) const;

  /** Dump object string representation to stdout.
   *  This take the output of the pretty method, and dumps it to stdout. To be
   *  used for debugging and when single stepping in gdb.
   *  @see pretty
   */
  DUMP_METHOD void dump() const;

  /** Produce a checksum/hash of the current object.
   *  Takes current object and produces a lossy digest of it. Originally used
   *  crc32, now uses a more hacky but faster hash function. For use in hash
   *  objects.
   *  @see do_crc
   *  @return Digest of the current type.
   */
  size_t crc() const;

  /** Perform checked invocation of cmp method.
   *  Takes reference to another type - if they have the same type id, invoke
   *  the cmp function and return its result. Otherwise, return false. Using
   *  this method ensures thatthe implementer of cmp knows the reference it
   *  operates on is on the same type as itself.
   *  @param ref Reference to type to compare this object against
   *  @return True if types are the same, false otherwise.
   */
  bool cmpchecked(const type2t &ref) const;

  /** Perform checked invocation of lt method.
   *  Identical to cmpchecked, except with the lt method.
   *  @see cmpchecked
   *  @param ref Reference to type to measure this against.
   *  @return 0 if types are the same, 1 if this > ref, -1 if ref > this.
   */
  int ltchecked(const type2t &ref) const;

  /** Virtual method to compare two types.
   *  To be overridden by an extending type; assumes that itself and the
   *  parameter are of the same extended type. Call via cmpchecked.
   *  @see cmpchecked
   *  @param ref Reference to (same class of) type to compare against
   *  @return True if types match, false otherwise
   */
  virtual bool cmp(const type2t &ref) const = 0;

  /** Virtual method to compare two types.
   *  To be overridden by an extending type; assumes that itself and the
   *  parameter are of the same extended type. Call via cmpchecked.
   *  @see cmpchecked
   *  @param ref Reference to (same class of) type to compare against
   *  @return 0 if types are the same, 1 if this > ref, -1 if ref > this.
   */
  virtual int lt(const type2t &ref) const;

  /** Extract a list of members from type as strings.
   *  Produces a list of pairs, mapping a member name to a string value. Used
   *  in the body of the pretty method.
   *  @see pretty
   *  @param indent Number of spaces to indent output strings with, if multiline
   *  @return list of name:value pairs.
   */
  virtual list_of_memberst tostring(unsigned int indent) const = 0;

  /** Perform crc operation accumulating into parameter.
   *  Performs the operation of the crc method, but overridden to be specific to
   *  a particular type. Accumulates data into the hash object parameter.
   *  @see cmp
   *  @param seed Hash to accumulate hash data into.
   *  @return Hash value
   */
  virtual size_t do_crc() const;

  /** Perform hash operation accumulating into parameter.
   *  Feeds data as appropriate to the type of the expression into the
   *  parameter, to be hashed. Like crc and do_crc, but for some other kind
   *  of hash scenario.
   *  @see cmp
   *  @see crc
   *  @see do_crc
   *  @param hash Object to accumulate hash data into.
   */
  virtual void hash(crypto_hash &hash) const;

  /** Clone method. Self explanatory.
   *  @return New container, containing a duplicate of this object.
   */
  virtual type2tc clone() const = 0;

  // Please see the equivalent methods in expr2t for documentation
  template <typename T>
  void foreach_subtype(T &&t) const
  {
    const_subtype_delegate wrapped(std::cref(t));
    foreach_subtype_impl_const(wrapped);
  }

  template <typename T>
  void Foreach_subtype(T &&t)
  {
    subtype_delegate wrapped(std::ref(t));
    foreach_subtype_impl(wrapped);
  }

  /** Instance of type_ids recording this types type. */
  // XXX XXX XXX this should be const
  type_ids type_id;

  mutable size_t crc_val;
};

/** Fetch identifying name for a type.
 *  I.E., this is the class of the type, what you'd get if you called type.id()
 *  with the old stringy irep. Ideally this should be a class method, but as it
 *  was added as a hack I haven't got round to it yet.
 *  @param type Type to fetch identifier for
 *  @return String containing name of type class.
 */
std::string get_type_id(const type2t &type);

/** Fetch identifying name for a type.
 *  Just passes through to type2t accepting function with the same name.
 *  @param type Type to fetch identifier for
 *  @return String containing name of type class.
 */
static inline std::string get_type_id(const type2tc &type)
{
  return get_type_id(*type);
}

/** Base class for all expressions.
 *  In this base, contains an expression id used for distinguishing different
 *  classes of expr, in addition we have a type as all exprs should have types.
 */
class expr2t : public irep2t
{
public:
  /** Enumeration identifying each sort of expr.
   */
  enum expr_ids
  {
// Boost preprocessor magic: enumerate over each expression and pump out
// a foo_id enum element. See list of ireps at top of file.
#define _ESBMC_IREP2_EXPRID_ENUM(r, data, elem) BOOST_PP_CAT(elem, _id),
    BOOST_PP_LIST_FOR_EACH(_ESBMC_IREP2_EXPRID_ENUM, foo, ESBMC_LIST_OF_EXPRS)
      end_expr_id
  };

  /** Type for list of constant expr operands */
  typedef std::list<const expr2tc *> expr_operands;
  /** Type for list of non-constant expr operands */
  typedef std::list<expr2tc *> Expr_operands;

  typedef std::function<void(const expr2tc &expr)> const_op_delegate;
  typedef std::function<void(expr2tc &expr)> op_delegate;

protected:
  /** Primary constructor.
   *  @param type Type of this new expr
   *  @param id Class identifier for this new expr
   */
  expr2t(const type2tc &type, expr_ids id);
  /** Copy constructor */
  expr2t(const expr2t &ref);

  virtual void foreach_operand_impl_const(const_op_delegate &expr) const = 0;
  virtual void foreach_operand_impl(op_delegate &expr) = 0;

public:
  // Provide base / container types for some templates stuck on top:
  typedef expr2tc container_type;
  typedef expr2t base_type;
  // Also provide base traits
  typedef esbmct::expr2t_traits<> traits;

  virtual ~expr2t() = default;

  /** Clone method. Self explanatory. */
  virtual expr2tc clone() const = 0;

  /* These are all self explanatory */
  bool operator==(const expr2t &ref) const;
  bool operator<(const expr2t &ref) const;
  bool operator!=(const expr2t &ref) const;

  /** Perform type-checked call to lt method.
   *  Checks that this object and the one we're comparing against have the same
   *  expr class, so that the lt method can assume it's working on objects of
   *  the same type.
   *  @see type2t::ltchecked
   *  @param ref Expression object we're comparing this object against.
   *  @return 0 If exprs are the same, 1 if this > ref, -1 if ref > this.
   */
  int ltchecked(const expr2t &ref) const;

  /** Produce textual representation of this expr.
   *  Like the stringy-irep's pretty method, this takes the current object and
   *  produces a textual representation that can be read by a human to
   *  understand what's going on.
   *  @param indent Number of spaces to indent the output string lines by
   *  @return String object containing textual expr representation.
   */
  std::string pretty(unsigned int indent = 0) const;

  /** Write textual representation of this object to stdout.
   *  For use in debugging - dumps the output of the pretty method to stdout.
   *  Can either be used in portion of code, or more commonly called from gdb.
   */
  DUMP_METHOD void dump() const;

  /** Calculate a hash/digest of the current expr.
   *  For use in hash data structures; used to be a crc32, but is now a 16 bit
   *  hash function generated by myself to be fast. May not have nice
   *  distribution properties, but is at least fast.
   *  @return Hash value of this expr
   */
  size_t crc() const;

  /** Perform comparison operation between this and another expr.
   *  Overridden by subclasses of expr2t to compare different members of this
   *  and the passed in object. Assumes that the passed in object is the same
   *  class type as this; Should be called via operator==, which will do that
   *  check automagically.
   *  @see type2t::cmp
   *  @param ref Expr object to compare this against
   *  @return True if objects are the same; false otherwise.
   */
  virtual bool cmp(const expr2t &ref) const;

  /** Compare two expr objects.
   *  Overridden by subclasses - takes two expr objects (this and ref) of the
   *  same type, and compares them, in the same manner as memcmp. The assumption
   *  that the objects are of the same type means lt should be called via
   *  ltchecked to check for different expr types.
   *  @see type2t::lt
   *  @param ref Expr object to compare this against
   *  @return 0 If exprs are the same, 1 if this > ref, -1 if ref > this.
   */
  virtual int lt(const expr2t &ref) const;

  /** Convert fields of subclasses to a string representation.
   *  Used internally by the pretty method - creates a list of pairs
   *  representing the fields in the subclass. Each pair is a pair of strings
   *  of the form fieldname : value. The value may be multiline, in which case
   *  the new line will have at least indent number of indenting spaces.
   *  @param indent Number of spaces to indent multiline output by
   *  @return list of string pairs, of form fieldname:value
   */
  virtual list_of_memberst tostring(unsigned int indent) const = 0;

  /** Perform digest/hash function on expr object.
   *  Takes all fields in this exprs and adds them to the passed in hash object
   *  to compute an expression-hash. Overridden by subclasses.
   *  @param seed Hash to accumulate expression data into.
   *  @return Hash value
   */
  virtual size_t do_crc() const;

  /** Perform hash operation accumulating into parameter.
   *  Feeds data as appropriate to the type of the expression into the
   *  parameter, to be hashed. Like crc and do_crc, but for some other kind
   *  of hash scenario.
   *  @see cmp
   *  @see crc
   *  @see do_crc
   *  @param hash Object to accumulate hash data into.
   */
  virtual void hash(crypto_hash &hash) const;

  /** Fetch a sub-operand.
   *  These can come out of any field that is an expr2tc, or contains them.
   *  No particular numbering order is promised.
   */
  virtual const expr2tc *get_sub_expr(unsigned int idx) const = 0;

  /** Fetch a sub-operand. Non-const version.
   *  These can come out of any field that is an expr2tc, or contains them.
   *  No particular numbering order is promised.
   */
  virtual expr2tc *get_sub_expr_nc(unsigned int idx) = 0;

  /** Count the number of sub-exprs there are.
   */
  virtual unsigned int get_num_sub_exprs() const = 0;

  /** Simplify an expression.
   *  Similar to simplification in the string-based irep, this generates an
   *  expression with any calculations or operations that can be simplified,
   *  simplified. In contrast to the old form though, this creates a new expr
   *  if something gets simplified, just to make it clear exactly what's
   *  going on.
   *  @return Either a nil expr (null pointer contents) if nothing could be
   *          simplified or a simplified expression.
   */
  expr2tc simplify() const;

  /** expr-specific simplification methods.
   *  By default, an expression can't be simplified, and this method returns
   *  a nil expression to show that. However if simplification is possible, the
   *  subclass overrides this and if it can simplify its operands, returns a
   *  new simplified expression. It should attempt to modify itself (it's
   *  const).
   *
   *  If simplification failed the first time around, the simplify method will
   *  simplify this expressions individual operands,
   *  and will then call an expr with the simplified operands to see if it's now
   *  become simplifiable. This call occurs whether or not any operands were
   *  actually simplified, see below.
   *
   *  The 'second' parameter can be used to avoid invoking expensive attempts
   *  to simplify an expression more than once - on the first call to
   *  do_simplify this parameter will be false, then on the second it's be true,
   *  allowing method implementation to save the expensive stuff until all of
   *  its operands have certainly been simplified.
   *
   *  Currently simplification does some things that it shouldn't: pointer
   *  arithmetic for example. I'm not sure where this can be relocated to
   *  though.
   *  @return expr2tc A nil expression if no simplifcation could occur, or a new
   *          simplified object if it can.
   */
  virtual expr2tc do_simplify() const;

  /** Indirect, abstract operand iteration.
   *
   *  Provide a lambda-based accessor equivalent to the forall_operands2 macro
   *  where anonymous code (actually a delegate?) gets run over each operand
   *  expression. Because the full type of the expression isn't known by the
   *  caller, and each delegate is it's own type, we need to wrap it in a
   *  std::function before funneling it through a virtual function.
   *
   *  For the purpose of this method, an operand is another instance of an
   *  expr2tc. This means the delegate will be called on any expr2tc field of
   *  the expression, in the order they appear in the traits. For a vector of
   *  expressions, the delegate will be called for each element, in order.
   *
   *  The uncapitalized version is const; the capitalized version is non-const
   *  (and so one needs to .get() a mutable expr2t pointer when calling). When
   *  modifying operands, preserving type correctness is imperative.
   *
   *  @param t A delegate to be called for each expression operand; must have
   *           a type of void f(const expr2tc &)
   */
  template <typename T>
  void foreach_operand(T &&t) const
  {
    const_op_delegate wrapped(std::cref(t));
    foreach_operand_impl_const(wrapped);
  }

  template <typename T>
  void Foreach_operand(T &&t)
  {
    op_delegate wrapped(std::ref(t));
    foreach_operand_impl(wrapped);
  }

  /** Instance of expr_ids recording tihs exprs type. */
  const expr_ids expr_id;

  /** Type of this expr. All exprs have a type. */
  type2tc type;

  mutable size_t crc_val;
};

inline bool is_nil_expr(const expr2tc &exp)
{
  return exp.get() == nullptr;
}

inline bool is_nil_type(const type2tc &t)
{
  return t.get() == nullptr;
}

// For boost multi-index hashing,
inline std::size_t hash_value(const expr2tc &expr)
{
  return expr.crc();
}

/** Fetch string identifier for an expression.
 *  Returns the class name of the expr passed in - this is equivalent to the
 *  result of expr.id() in old stringy irep. Should ideally be a method of
 *  expr2t, but haven't got around to moving it yet.
 *  @param expr Expression to operate upon
 *  @return String containing class name of expression.
 */
std::string get_expr_id(const expr2t &expr);

/** Fetch string identifier for an expression.
 *  Like the expr2t equivalent with the same name, but de-ensapculates an
 *  expr2tc.
 */
static inline std::string get_expr_id(const expr2tc &expr)
{
  return get_expr_id(*expr);
}

/** Template for providing templated methods to irep classes (type2t/expr2t).
 *
 *  What this does: we give irep_methods2 a type trait record that contains
 *  a boost::mpl::vector, the elements of which describe each field in the
 *  class we're operating on. For each field we get:
 *
 *    - The type of the field
 *    - The class that field is part of
 *    - A pointer offset to that field.
 *
 *  What this means, is that we can @a type @a generically access a member
 *  of a class from within the template, without knowing what type it is,
 *  what its name is, or even what type contains it.
 *
 *  We can then use that to make all the boring methods of ireps type
 *  generic too. For example: we can make the comparision method by accessing
 *  each field in the class we're dealing with, passing them to another
 *  function to do the comparison (with the type resolved by templates or
 *  via overloading), and then inspecting the output of that.
 *
 *  In fact, we can make type generic implementations of all the following
 *  methods in expr2t: clone, tostring, cmp, lt, do_crc, hash.
 *  Similar methods, minus the operands, can be made generic in type2t.
 *
 *  So, that's what these templates provide; an irep class can be made by
 *  inheriting from this template, telling it what class it'll end up with,
 *  and what to subclass from, and what the fields in the class being derived
 *  from look like. This means we can construct a type hierarchy with whatever
 *  inheritance we like and whatever fields we like, then latch irep_methods2
 *  on top of that to implement all the anoying boring boilerplate code.
 *
 *  ----
 *
 *  In addition, we also define container types for each irep, which is
 *  essentially a type-safeish wrapper around a std::shared_ptr (i.e.,
 *  reference counter). One can create a new irep with syntax such as:
 *
 *    foo2tc bar(type, operand1, operand2);
 *
 *  One can transparently access the irep fields through dereference, such as:
 *
 *    bar->operand1 = 0;
 *
 *  This all replicates the CBMC expression situation, but with the addition
 *  of types.
 *
 *  ----
 *
 *  The following functions can be used to inspect an irep2 object:
 *
 *    is_${suffix}()
 *    to_${suffix}()
 *
 *  For expr2tc the suffix is the name of the class, while for type2t it is the
 *  name of the class without the trailing "2t", e.g.
 *
 *    is_bool_type(type)
 *    to_constant_int2t(expr)
 *
 *  The to_* functions return a (const) reference for a (const) expr2tc or
 *  type2tc parameter. The non-const versions perform a so-called "detach"
 *  operation, which ensures that the to-be-modified object is not referenced by
 *  any other irep2 terms in use. This detach operation is explained in more
 *  detail in the comment about irep_container. Because const-ness is used to
 *  decide whether to detach or not, when working with irep2 it is *critical*
 *  that const_cast<>() is used only where it's safe to. Best practice is to
 *  put a formal safety proof into the comment about const_cast usage.
 *
 *  The above functions are defined by type_macros and expr_macros in the
 *  respective irep2 header.
 *
 *  ----
 *
 *  The traits defined here are used to generically implement the functions
 *  operating on a type2t's or an expr2t's fields, like .dump() and the
 *  iterators foreach_subtype() and foreach_operand().
 *
 *  (The required traits hacks need cleaning up too).
 */
namespace esbmct
{
/** Maximum number of fields to support in expr2t subclasses. This value
 *  controls the types of any arrays that need to consider the number of
 *  fields.
 *  I've yet to find a way of making this play nice with the new variardic
 *  way of defining ireps. */
const unsigned int num_type_fields = 6;

/** Record for properties of an irep field.
 *  This type records, for any particular field:
 *    * It's type
 *    * The class that it's a member of
 *    * A class pointer to this field
 *  The aim being that we have enough information about the field to
 *  manipulate it without any further traits. */
template <typename R, typename C, R C::*v>
class field_traits
{
public:
  typedef R result_type;
  typedef C source_class;
  typedef R C::*membr_ptr;
  static constexpr membr_ptr value = v;
};

template <typename R, typename C, R C::*v>
constexpr
  typename field_traits<R, C, v>::membr_ptr field_traits<R, C, v>::value;

/** Trait class for type2t ireps.
 *  This takes a list of field traits and puts it in a vector, with the record
 *  for the type_id field (common to all type2t's) put that the front. */
template <typename... Args>
class type2t_traits
{
public:
  typedef field_traits<type2t::type_ids, type2t, &type2t::type_id>
    type_id_field;
  typedef typename boost::mpl::
    push_front<boost::mpl::vector<Args...>, type_id_field>::type fields;
  typedef type2t base2t;
};

/** Trait class for expr2t ireps.
 *  This takes a list of field traits and puts it in a vector, with the record
 *  for the expr_id field (common to all expr2t's) put that the front. Records
 *  some additional flags about the usage of the expression -- specifically
 *  what a unary constructor will do (@see something2tc::something2tc) */
template <typename... Args>
class expr2t_traits
{
public:
  typedef field_traits<const expr2t::expr_ids, expr2t, &expr2t::expr_id>
    expr_id_field;
  typedef field_traits<type2tc, expr2t, &expr2t::type> type_field;
  typedef typename boost::mpl::push_front<
    typename boost::mpl::push_front<boost::mpl::vector<Args...>, type_field>::
      type,
    expr_id_field>::type fields;
  static constexpr unsigned int num_fields =
    boost::mpl::size<fields>::type::value;
  typedef expr2t base2t;
};

// "Specialisation" for expr kinds where the type is derived, like boolean
// typed exprs. Should actually become a more structured expr2t_traits
// that can be specialised in this way, at a later date. Might want to
// move the presumed type down to the _data class at that time too.
template <typename... Args>
class expr2t_traits_notype
{
public:
  typedef field_traits<const expr2t::expr_ids, expr2t, &expr2t::expr_id>
    expr_id_field;
  typedef typename boost::mpl::
    push_front<boost::mpl::vector<Args...>, expr_id_field>::type fields;
  static constexpr unsigned int num_fields =
    boost::mpl::size<fields>::type::value;
  typedef expr2t base2t;
};

// Declaration of irep and expr methods templates.
template <
  class derived,
  class baseclass,
  typename traits,
  typename fields = typename traits::fields,
  typename enable = void>
class irep_methods2;
template <
  class derived,
  class baseclass,
  typename traits,
  typename fields = typename traits::fields,
  typename enable = void>
class expr_methods2;
template <
  class derived,
  class baseclass,
  typename traits,
  typename fields = typename traits::fields,
  typename enable = void>
class type_methods2;

/** Definition of irep methods template.
 *
 *  @param derived The inheritor class, like add2t
 *  @param baseclass Class containing fields for methods to be defined over
 *  @param traits Type traits for baseclass
 *
 *  A typical irep inheritance looks like this, descending from the base
 *  irep class to the most derived class:
 *
 *    b          Base class, such as type2t or expr2t
 *    d          Data class, containing storage fields for ireps
 *    m          Terminal methods class (see below)
 *    M
 *    M            Recursive chain of irep_methods2 classes. Each one
 *    M            implements methods for one field, and calls to a superclass
 *    M            to handle remaining fields
 *    M
 *    t          Top level class such as add2t
 *
 *  The effect is thus: one takes a base class containing storage fields,
 *  instantiate irep_methods2 on top of it which unrolls to one template
 *  instance per field (plus a specialized terminal when there are no more
 *  fields). Then, have the top level class inherit from the chain of
 *  irep_methods classes. This avoids the writing of certain boilerplate
 *  methods at the expense of writing type trait information.
 *
 *  Technically one could typedef the top level irep_methods class to be the
 *  top level class itself; however putting a 'cap' on it (as it were) avoids
 *  decades worth of template errors if a programmer uses the irep
 *  incorrectly.
 */
template <
  class derived,
  class baseclass,
  typename traits,
  typename fields,
  typename enable>
class irep_methods2 : public irep_methods2<
                        derived,
                        baseclass,
                        traits,
                        typename boost::mpl::pop_front<fields>::type>
{
public:
  typedef irep_methods2<
    derived,
    baseclass,
    traits,
    typename boost::mpl::pop_front<fields>::type>
    superclass;
  typedef typename baseclass::base_type base2t;
  typedef irep_container<base2t> base_container2tc;

  template <typename... Args>
  irep_methods2(const Args &...args) : superclass(args...)
  {
  }

  // Copy constructor. Construct from derived ref rather than just
  // irep_methods2, because the template above will be able to directly
  // match a const derived &, and so the compiler won't cast it up to
  // const irep_methods2 & and call the copy constructor. Fix this by
  // defining a copy constructor that exactly matches the (only) use case.
  irep_methods2(const derived &ref) : superclass(ref)
  {
  }

  // Top level / public methods for this irep. These methods are virtual, set
  // up any relevant computation, and then call the recursive instances below
  // to perform the actual work over fields.
  base_container2tc clone() const override;
  list_of_memberst tostring(unsigned int indent) const override;
  bool cmp(const base2t &ref) const override;
  int lt(const base2t &ref) const override;
  size_t do_crc() const override;
  void hash(crypto_hash &hash) const override;

protected:
  // Fetch the type information about the field we are concerned with out
  // of the current type trait we're working on.
  typedef typename boost::mpl::front<fields>::type::result_type cur_type;
  typedef typename boost::mpl::front<fields>::type::source_class base_class;
  typedef typename boost::mpl::front<fields>::type membr_ptr;

  // Recursive instances of boilerplate methods.
  void tostring_rec(
    unsigned int idx,
    list_of_memberst &vec,
    unsigned int indent) const;
  bool cmp_rec(const base2t &ref) const;
  int lt_rec(const base2t &ref) const;
  void do_crc_rec() const;
  void hash_rec(crypto_hash &hash) const;

  // These methods are specific to expressions rather than types, and are
  // placed here to avoid un-necessary recursion in expr_methods2.
  const expr2tc *
  get_sub_expr_rec(unsigned int cur_count, unsigned int desired) const;
  expr2tc *get_sub_expr_nc_rec(unsigned int cur_count, unsigned int desired);
  unsigned int get_num_sub_exprs_rec() const;

  void foreach_operand_impl_rec(expr2t::op_delegate &f);
  void foreach_operand_impl_const_rec(expr2t::const_op_delegate &f) const;

  // Similar story, but for type2tc
  void foreach_subtype_impl_rec(type2t::subtype_delegate &t);
  void foreach_subtype_impl_const_rec(type2t::const_subtype_delegate &t) const;
};

// Base instance of irep_methods2. This is a template specialization that
// matches (via boost::enable_if) when the list of fields to operate on is
// now empty. Finish up the remaining computation, if any.
template <class derived, class baseclass, typename traits, typename fields>
class irep_methods2<
  derived,
  baseclass,
  traits,
  fields,
  typename boost::enable_if<typename boost::mpl::empty<fields>::type>::type>
  : public baseclass
{
public:
  template <typename... Args>
  irep_methods2(Args... args) : baseclass(args...)
  {
  }

  // Copy constructor. See note for non-specialized definition.
  irep_methods2(const derived &ref) : baseclass(ref)
  {
  }

protected:
  typedef typename baseclass::container_type container2tc;
  typedef typename baseclass::base_type base2t;

  void tostring_rec(
    unsigned int idx,
    list_of_memberst &vec,
    unsigned int indent) const
  {
    (void)idx;
    (void)vec;
    (void)indent;
  }

  bool cmp_rec(const base2t &ref) const
  {
    // If it made it this far, we passed
    (void)ref;
    return true;
  }

  int lt_rec(const base2t &ref) const
  {
    // If it made it this far, we passed
    (void)ref;
    return 0;
  }

  void do_crc_rec() const
  {
  }

  void hash_rec(crypto_hash &hash) const
  {
    (void)hash;
  }

  const expr2tc *
  get_sub_expr_rec(unsigned int cur_idx, unsigned int desired) const
  {
    // No result, so desired must exceed the number of idx's
    assert(cur_idx >= desired);
    (void)cur_idx;
    (void)desired;
    return nullptr;
  }

  expr2tc *get_sub_expr_nc_rec(unsigned int cur_idx, unsigned int desired)
  {
    // See above
    assert(cur_idx >= desired);
    (void)cur_idx;
    (void)desired;
    return nullptr;
  }

  unsigned int get_num_sub_exprs_rec() const
  {
    return 0;
  }

  void foreach_operand_impl_rec(expr2t::op_delegate &f)
  {
    (void)f;
  }

  void foreach_operand_impl_const_rec(expr2t::const_op_delegate &f) const
  {
    (void)f;
  }

  void foreach_subtype_impl_rec(type2t::subtype_delegate &t)
  {
    (void)t;
  }

  void foreach_subtype_impl_const_rec(type2t::const_subtype_delegate &t) const
  {
    (void)t;
  }
};

/** Expression methods template for expr ireps.
 *  This class works on the same principle as @irep_methods2 but provides
 *  head methods for get_sub_expr and so forth, which are
 *  specific to expression ireps. The actual implementation of these methods
 *  are provided in irep_methods to avoid un-necessary recursion but are
 *  protected; here we provide the head methods publically to allow the
 *  programmer to call in.
 *  */
template <
  class derived,
  class baseclass,
  typename traits,
  typename fields,
  typename enable>
class expr_methods2
  : public irep_methods2<derived, baseclass, traits, fields, enable>
{
public:
  typedef irep_methods2<derived, baseclass, traits, fields, enable> superclass;

  template <typename... Args>
  expr_methods2(const Args &...args) : superclass(args...)
  {
  }

  // See notes on irep_methods2 copy constructor
  expr_methods2(const derived &ref) : superclass(ref)
  {
  }

  const expr2tc *get_sub_expr(unsigned int i) const override;
  expr2tc *get_sub_expr_nc(unsigned int i) override;
  unsigned int get_num_sub_exprs() const override;

  void
  foreach_operand_impl_const(expr2t::const_op_delegate &expr) const override;
  void foreach_operand_impl(expr2t::op_delegate &expr) override;
};

/** Type methods template for type ireps.
 *  Like @expr_methods2, but for types. Also; written on the quick.
 *  */
template <
  class derived,
  class baseclass,
  typename traits,
  typename fields,
  typename enable>
class type_methods2
  : public irep_methods2<derived, baseclass, traits, fields, enable>
{
public:
  typedef irep_methods2<derived, baseclass, traits, fields, enable> superclass;

  template <typename... Args>
  type_methods2(const Args &...args) : superclass(args...)
  {
  }

  // See notes on irep_methods2 copy constructor
  type_methods2(const derived &ref) : superclass(ref)
  {
  }

  void
  foreach_subtype_impl_const(type2t::const_subtype_delegate &t) const override;
  void foreach_subtype_impl(type2t::subtype_delegate &t) override;
};

} // namespace esbmct

inline std::ostream &operator<<(std::ostream &out, const expr2tc &a)
{
  out << a->pretty(0);
  return out;
}

struct irep2_hash
{
  size_t operator()(const expr2tc &ref) const
  {
    return ref.crc();
  }
};

struct type2_hash
{
  size_t operator()(const type2tc &ref) const
  {
    return ref->crc();
  }
};

#endif /* IREP2_H_ */