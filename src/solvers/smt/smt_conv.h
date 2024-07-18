#ifndef _ESBMC_PROP_SMT_SMT_CONV_H_
#define _ESBMC_PROP_SMT_SMT_CONV_H_

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>
#include <cstdint>
#include <solvers/prop/literal.h>
#include <solvers/prop/pointer_logic.h>
#include <irep2/irep2_utils.h>
#include <util/message.h>
#include <util/namespace.h>
#include <util/threeval.h>

/** @file smt_conv.h
 *  SMT conversion tools and utilities.
 *  smt_convt is the base class for everything that attempts to convert the
 *  contents of an SSA program into something else, generally SMT or SAT based.
 *
 *  The class itself does various accounting and structuring of the conversion,
 *  however the challenge is that as we convert the SSA program into anything
 *  else, we must deal with the fact that expressions in ESBMC are somewhat
 *  bespoke, and don't follow any particular formalism or logic. Therefore
 *  a lot of translation has to occur to reduce it to the desired logic, a
 *  process that Kroening refers to in CBMC as 'Flattenning'.
 *
 *  The conceptual data flow is that an SSA program held by
 *  symex_target_equationt is converted into a series of boolean propositions
 *  in some kind of solver context, the handle to which are objects of class
 *  smt_ast. These are then asserted as appropriate (or conjoined or
 *  disjuncted), after which the solver may be asked whether the formula is
 *  or not. If it is, the value of symbols in the formula may be retrieved
 *  from the solver.
 *
 *  To do that, the user must allocate a solver converter object, which extends
 *  the class smt_convt. Current, create_solver() will do this, in the
 *  factory-pattern manner (ish). Each solver converter implements all the
 *  abstract methods of smt_convt. When handed an expression to convert,
 *  smt_convt deconstructs it into a series of function applications, which it
 *  creates by calling various abstract methods implemented by the converter
 *  (in particular mk_func_app).
 *
 *  The actual function applications are in smt_ast objects. Following the
 *  SMTLIB definition, these are basically a term.
 *
 *  In no particular order, the following expression translation problems exist
 *  and are solved at various layers:
 *
 *  For all solvers, the following problems are flattenned:
 *    * The C memory address space
 *    * Representation of pointer types
 *    * Casts
 *    * Byte operations on objects (extract/update)
 *    * FixedBV representation of floats
 *    * Unions -> something else
 *
 *  While these problems are supported by some SMT solvers, but are flattened
 *  in others (as SMT doesn't support these):
 *    * Bitvector integer overflow detection
 *    * Tuple representation (and arrays of them)
 *
 *  SAT solvers have the following aspects flattened:
 *    * Arrays (using Kroenings array decision procedure)
 *    * First order logic bitvector calculations to boolean formulas
 *    * Boolean formulas to CNF
 *
 *  If you find yourself having to make the SMT translation translate more than
 *  these things, ask yourself whether what you're doing is better handled at
 *  a different layer, such as symbolic execution. A nonexhaustive list of these
 *  include:
 *    * Anything involving pointer dereferencing at all
 *    * Anything that considers the control flow guard at any point
 *    * Pointer liveness or dynamic allocation consideration
 *
 *  A reasonable amount of SMT conversion complexity comes from the
 *  eccentricities of different solvers. This is a necessary part of working in
 *  the tar pit.
 *
 *  @see smt_convt
 *  @see symex_target_equationt
 *  @see create_solver
 *  @see smt_convt::mk_func_app
 */

// Forward dec.
class fp_convt;
class smt_convt;

#include <solvers/smt/smt_array.h>
#include <solvers/smt/tuple/smt_tuple.h>
#include <solvers/smt/fp/fp_conv.h>

/** The base SMT-conversion class/interface.
 *  smt_convt handles a number of decisions that must be made when
 *  deconstructing ESBMC expressions down into SMT representation. See
 *  smt_conv.h for more high level documentation of this.
 *
 *  The basic flow is thus: a class that can create SMT formula in some solver
 *  subclasses smt_convt, implementing abstract methods, in particular
 *  mk_func_app. The rest of ESBMC then calls convert with an expression, and
 *  this class deconstructs it into a series of applications, as documented by
 *  the smt_func_kind enumeration. These are then created via mk_func_app or
 *  some more specific method calls. Boolean sorted ASTs are then asserted
 *  into the solver context.
 *
 *  The exact lifetime of smt asts here is currently undefined, unfortunately,
 *  although smt_convt posesses a cache, so they generally have a reference
 *  in there. This will probably be fixed in the future.
 *
 *  In theory this class supports pushing and popping of solver contexts,
 *  although of course that depends too on the subclass supporting it. However,
 *  this hasn't really been tested since everything here was rewritten from
 *  The Old Way, so don't trust it.
 *
 *  While mk_func_app is supposed to be the primary interface to making SMT
 *  function applications, in some cases we want to introduce some
 *  abstractions, and this becomes unweildy. Thus, tuple and array operations
 *  are performed via virtual function calls. By default, array operations are
 *  then passed through to mk_func_app, while tuples are decomposed into sets
 *  of variables which are then created through mk_func_app. If this isn't
 *  what a solver wants to happen, it can override this and handle that itself.
 *  The idea is that, in the manner of metaSMT, you can then compose a series
 *  of subclasses that perform the desired amount of flattening, and then work
 *  from there. (Some of this is still WIP though).
 *
 *  NB: the whole smt_asts-are-const situation needs to be resolved too.
 *
 *  @see smt_conv.h
 *  @see smt_func_kind */
class smt_convt
{
public:
  /* NOTE: I've made this horrible so we remember that there is an
   * even uglier implementation that just returns an empty
   * look at where this variable is used for more info :) */
  bool extracting_from_array_tuple_is_error = false;
  /** Shorthand for a vector of smt_ast's */
  typedef std::vector<smt_astt> ast_vec;

  template <typename the_solver_ast>
  smt_astt
  new_solver_ast(typename the_solver_ast::solver_ast_type ast, smt_sortt sort)
  {
    return new the_solver_ast(this, ast, sort);
  }

  /** Primary constructor. After construction, smt_post_init must be called
   *  before the object is used as a solver converter.
   *
   *  @param _ns Namespace for looking up the type of certain symbols.
   *  @param _options Provide all the needed parameters to configure the solver. */
  smt_convt(const namespacet &_ns, const optionst &_options);

  virtual ~smt_convt() = default;

  /** Post-constructor setup method. We must create various pieces of memory
   *  model data for tracking, however can't do it from the constructor because
   *  the solver converter itself won't have been initialized itself at that
   *  point. So, once it's ready, the solver converter should call this from
   *  it's constructor. */
  void smt_post_init();

  // The API that we provide to the rest of the world:
  /** @{
   *  @name External API to smt_convt. */

  /** Result of a call to dec_solve. Either sat, unsat, or error. SMTLIB is
   *  historic case that needs to go. */
  typedef enum
  {
    P_UNSATISFIABLE,
    P_SATISFIABLE,
    P_ERROR,
    P_SMTLIB
  } resultt;

  /** Push one context on the SMT assertion stack. */
  virtual void push_ctx();
  /** Pop one context on the SMT assertion stack. */
  virtual void pop_ctx();

  /** Main interface to SMT conversion.
   *  Takes one expression, and converts it into the underlying SMT solver,
   *  returning a single smt_ast that represents the converted expressions
   *  value. The lifetime of the returned pointer is currently undefined.
   *
   *  @param expr The expression to convert into the SMT solver
   *  @return The resulting handle to the SMT value. */
  smt_astt convert_ast(const expr2tc &expr);

  /** Interface to specifig SMT conversion.
   *  Takes one expression, and converts it into the underlying SMT solver,
   *  depending on the type of the expression.
   *
   *  @param expr The expression to convert into the SMT solver
   *  @param type The expression's type
   *  @param args The expression's args
   *  @param ops The operations for each sort type
   *  @return The resulting handle to the SMT value. */
  smt_astt convert_ast(
    const expr2tc &expr,
    const type2tc &type,
    smt_astt const *args,
    struct expr_op_convert ops);

  smt_astt convert_assign(const expr2tc &expr);

  smt_astt make_n_ary_and(const ast_vec &v);
  smt_astt make_n_ary_or(const ast_vec &v);

  /** Create the inverse of an smt_ast. Equivalent to a 'not' operation.
   *  @param a The ast to invert. Must be boolean sorted.
   *  @return The inverted piece of AST. */
  smt_astt invert_ast(smt_astt a);

  /** Create an ipmlication between two smt_ast's.
   *  @param a The ast that implies the truth of the other. Boolean.
   *  @param b The ast whos truth is implied. Boolean.
   *  @return The resulting piece of AST. */
  smt_astt imply_ast(smt_astt a, smt_astt b);

  /** Assert the truth of an ast. Equivalent to the 'assert' directive in the
   *  SMTLIB language, this informs the solver that in the satisfying
   *  assignment it attempts to produce, the formula corresponding to the
   *  smt_ast argument must evaluate to true.
   *  @param a A handle to the formula that must be true. */
  virtual void assert_ast(smt_astt a) = 0;

  /** Solve the formula given to the solver. The solver will attempt to produce
   *  a satisfying assignment for all of the variables / symbols used in the
   *  formula, where all the asserted sub-formula are true. Results are either
   *  unsat (the formula is inconsistent), sat (an assignment exists), or that
   *  an error occurred.
   *  @return Result code of the call to the solver. */
  virtual resultt dec_solve() = 0;

  void pre_solve();

  /** Get the satisfying assignment using the type.
   *  @param expr Variable to get the value of. Must be a symbol expression.
   *  @return Explicit assigned value of expr in the solver. May be nil, in
   *          which case the solver did not assign a value to it for some
   *          reason. */
  virtual expr2tc get_by_type(const expr2tc &expr);

  /** Get the satisfying assignment using the ast.
   *  @param a Variable to get the value of.
   *  @param type The variable type.
   *  @return Explicit assigned value of expr in the solver. May be nil, in
   *          which case the solver did not assign a value to it for some
   *          reason. */
  virtual expr2tc get_by_ast(const type2tc &type, smt_astt a);

  /** Builds the bitvector based on the value retrieved from the solver.
   *  @param type the type (fixedbv or (un)signedbv),
   *  @param value the value retrieved from the solver.
   *  @return Expression representation of a's value */
  expr2tc get_by_value(const type2tc &type, BigInt value);

  /** Fetch a satisfying assignment from the solver. If a previous call to
   *  dec_solve returned satisfiable, then the solver has a set of assignments
   *  to symbols / variables used in the formula. This method retrieves the
   *  value of a symbol, and formats it into an ESBMC expression.
   *  @param expr Variable to get the value of. Must be a symbol expression.
   *  @return Explicit assigned value of expr in the solver. May be nil, in
   *          which case the solver did not assign a value to it for some
   *          reason. */
  virtual expr2tc get(const expr2tc &expr);

  /** Solver name fetcher. Returns a string naming the solver being used, and
   *  potentially it's version, if available.
   *  @return The name of the solver this smt_convt uses. */
  virtual const std::string solver_text() = 0;

  /** Fetch the value of a boolean sorted smt_ast. (The 'l' is for literal, and
   *  is historic). Returns a three valued result, of true, false, or
   *  unassigned.
   *  @param a The boolean sorted ast to fetch the value of.
   *  @return A three-valued return val, of the assignment to a. */
  virtual tvt l_get(smt_astt a);

  /** @} */

  /** @{
   *  @name Internal conversion API between smt_convt and solver converter */

  virtual smt_astt mk_add(smt_astt a, smt_astt b);
  virtual smt_astt mk_bvadd(smt_astt a, smt_astt b);
  virtual smt_astt mk_sub(smt_astt a, smt_astt b);
  virtual smt_astt mk_bvsub(smt_astt a, smt_astt b);
  virtual smt_astt mk_mul(smt_astt a, smt_astt b);
  virtual smt_astt mk_bvmul(smt_astt a, smt_astt b);
  virtual smt_astt mk_mod(smt_astt a, smt_astt b);
  virtual smt_astt mk_bvsmod(smt_astt a, smt_astt b);
  virtual smt_astt mk_bvumod(smt_astt a, smt_astt b);
  virtual smt_astt mk_div(smt_astt a, smt_astt b);
  virtual smt_astt mk_bvsdiv(smt_astt a, smt_astt b);
  virtual smt_astt mk_bvudiv(smt_astt a, smt_astt b);
  virtual smt_astt mk_shl(smt_astt a, smt_astt b);
  virtual smt_astt mk_bvshl(smt_astt a, smt_astt b);
  virtual smt_astt mk_bvashr(smt_astt a, smt_astt b);
  virtual smt_astt mk_bvlshr(smt_astt a, smt_astt b);
  virtual smt_astt mk_neg(smt_astt a);
  virtual smt_astt mk_bvneg(smt_astt a);
  virtual smt_astt mk_bvnot(smt_astt a);
  virtual smt_astt mk_bvnxor(smt_astt a, smt_astt b);
  virtual smt_astt mk_bvnor(smt_astt a, smt_astt b);
  virtual smt_astt mk_bvnand(smt_astt a, smt_astt b);
  virtual smt_astt mk_bvxor(smt_astt a, smt_astt b);
  virtual smt_astt mk_bvor(smt_astt a, smt_astt b);
  virtual smt_astt mk_bvand(smt_astt a, smt_astt b);
  virtual smt_astt mk_implies(smt_astt a, smt_astt b);
  virtual smt_astt mk_xor(smt_astt a, smt_astt b);
  virtual smt_astt mk_or(smt_astt a, smt_astt b);
  virtual smt_astt mk_and(smt_astt a, smt_astt b);
  virtual smt_astt mk_not(smt_astt a);
  virtual smt_astt mk_lt(smt_astt a, smt_astt b);
  virtual smt_astt mk_bvult(smt_astt a, smt_astt b);
  virtual smt_astt mk_bvslt(smt_astt a, smt_astt b);
  virtual smt_astt mk_gt(smt_astt a, smt_astt b);
  virtual smt_astt mk_bvugt(smt_astt a, smt_astt b);
  virtual smt_astt mk_bvsgt(smt_astt a, smt_astt b);
  virtual smt_astt mk_le(smt_astt a, smt_astt b);
  virtual smt_astt mk_bvule(smt_astt a, smt_astt b);
  virtual smt_astt mk_bvsle(smt_astt a, smt_astt b);
  virtual smt_astt mk_ge(smt_astt a, smt_astt b);
  virtual smt_astt mk_bvuge(smt_astt a, smt_astt b);
  virtual smt_astt mk_bvsge(smt_astt a, smt_astt b);
  virtual smt_astt mk_eq(smt_astt a, smt_astt b);
  virtual smt_astt mk_neq(smt_astt a, smt_astt b);
  virtual smt_astt mk_store(smt_astt a, smt_astt b, smt_astt c);
  virtual smt_astt mk_select(smt_astt a, smt_astt b);
  virtual smt_astt mk_real2int(smt_astt a);
  virtual smt_astt mk_int2real(smt_astt a);
  virtual smt_astt mk_isint(smt_astt a);

  /** Create an integer or SBV/UBV sort */
  smt_sortt mk_int_bv_sort(std::size_t width)
  {
    if (int_encoding)
      return mk_int_sort();

    if (width == 0)
      width = 1;

    return mk_bv_sort(width);
  }

  /** Create an real or floating-point/fixed-point sort */
  smt_sortt mk_real_fp_sort(std::size_t ew, std::size_t sw)
  {
    if (int_encoding)
      return mk_real_sort();

    if (config.ansi_c.use_fixed_for_float)
      return mk_fbv_sort(ew + sw);

    return fp_api->mk_fpbv_sort(ew, sw);
  }

  /** Create a bool sort */
  virtual smt_sortt mk_bool_sort();

  /** Create a real sort */
  virtual smt_sortt mk_real_sort();

  /** Create a int sort */
  virtual smt_sortt mk_int_sort();

  /** Create a bv sort */
  virtual smt_sortt mk_bv_sort(std::size_t width);

  /** Create a fixed-point sort */
  virtual smt_sortt mk_fbv_sort(std::size_t width);

  /** Create a floating-point sort, using bitvectors */
  virtual smt_sortt mk_bvfp_sort(std::size_t ew, std::size_t sw);

  /** Create a floating-point rouding mode sort, using bitvectors */
  virtual smt_sortt mk_bvfp_rm_sort();

  /** Create an array sort */
  virtual smt_sortt mk_array_sort(smt_sortt domain, smt_sortt range);

  /** Create an integer smt_ast. That is, an integer in QF_AUFLIRA, rather than
   *  a bitvector.
   *  @param theint BigInt representation of the number to create.
   *  @return The newly created terminal smt_ast of this integer. */
  virtual smt_astt mk_smt_int(const BigInt &theint) = 0;

  /** Create a real in a smt_ast.
   *  @param str String representation of the real, to be parsed by the solver.
   *         Tends to be one integer divided ('/') by another. After inspecting
   *         all other options, there are none that are good, this is a
   *         legitimate use of strings.
   *  @return The newly created terminal smt_ast of this real. */
  virtual smt_astt mk_smt_real(const std::string &str) = 0;

  /** Create a bitvector.
   *  @param theint Integer representation of the bitvector. Any excess bits
   *         in the stored integer should be ignored.
   *  @param w Width, in bits, of the bitvector to create.
   *  @return The newly created terminal smt_ast of this bitvector. */
  virtual smt_astt mk_smt_bv(const BigInt &theint, std::size_t w)
  {
    return mk_smt_bv(theint, mk_int_bv_sort(w));
  }

  /** Create a bitvector.
   *  @param s the sort.
   *  @param theint Integer representation of the bitvector. Any excess bits
   *         in the stored integer should be ignored.
   *  @param w Width, in bits, of the bitvector to create.
   *  @return The newly created terminal smt_ast of this bitvector. */
  virtual smt_astt mk_smt_bv(const BigInt &theint, smt_sortt s) = 0;

  /** Create a boolean.
   *  @param val Whether to create a true or false boolean.
   *  @return The newly created terminal smt_ast of this boolean. */
  virtual smt_astt mk_smt_bool(bool val) = 0;

  /** Create a symbol / variable. These correspond to renamed SSA variables in
   *  the SSA program, although any other names can be used too, so long as they
   *  don't conflict with anything else.
   *  @param name Textual name of the symbol to create.
   *  @param s The sort of the symbol we're creating.
   *  @param The newly created terminal smt_ast of this symbol. */
  virtual smt_astt mk_smt_symbol(const std::string &name, smt_sortt s) = 0;

  /** Create an 'extract' func app. Due to the fact that we can't currently
   *  encode integer constants as function arguments without serious faff,
   *  this can't be performed via the medium of mk_func_app. Hence, this api
   *  call.
   *  @param a The source piece of ast to extract a value from.
   *  @param high The topmost bit to select from the source, down to low.
   *  @param low The lowest bit to select from the source. */
  virtual smt_astt
  mk_extract(smt_astt a, unsigned int high, unsigned int low) = 0;

  /** Given a signed, upwards cast, extends the sign of the given AST to the
   *  desired length.
   *  @param a The bitvector to upcast.
   *  @param topwidth The number of bits to extend the input by
   *  @return A bitvector with topwidth more bits, of the appropriate sign. */
  virtual smt_astt mk_sign_ext(smt_astt a, unsigned int topwidth) = 0;

  /** Identical to mk_sign_ext, but extends AST with zeros */
  virtual smt_astt mk_zero_ext(smt_astt a, unsigned int topwidth) = 0;

  /** Concatenate two smt expressions
   * @param a the first part of the concatenation
   * @param b the second part of the concatenation
   * @return the concatenation of a and b */
  virtual smt_astt mk_concat(smt_astt a, smt_astt b) = 0;

  /** Create an ite operation
   * @param cond the ite condition
   * @param t the true case
   * @param f the false case
   * @return an ite expression */
  virtual smt_astt mk_ite(smt_astt cond, smt_astt t, smt_astt f) = 0;

  /** Extract the assignment to a boolean variable from the SMT solvers model.
   *  @param a The AST whos value we wish to know.
   *  @return Expression representation of a's value, as a constant_bool2tc */
  virtual bool get_bool(smt_astt a) = 0;

  /** Extract the assignment to a bitvector from the SMT solvers model.
   *  @param a The AST whos value we wish to know.
   *  @param is_signed whether the bitvector is signed
   *  @return Expression representation of a's value */
  virtual BigInt get_bv(smt_astt a, bool is_signed) = 0;

  /** Reduction or: equals bit0 iff all bits are 0
   * @param op the expr to be reduced
   * @return reduced op
   */
  smt_astt mk_bvredor(smt_astt op);

  /** Reduction and: equals bit1 iff all bits are 1
   * @param op the expr to be reduced
   * @return reduced op
   */
  smt_astt mk_bvredand(smt_astt op);

  /** @} */

  /** @{
   *  @name Integer overflow solver-converter API. */

  /** Detect integer arithmetic overflows. Takes an expression, that is one of
   *  add / sub / mul, and evaluates whether its operation applied to its
   *  operands will result in an integer overflow or underflow.
   *  @param expr Expression to test for arithmetic overflows in.
   *  @return Boolean valued AST representing whether an overflow occurs. */
  virtual smt_astt overflow_arith(const expr2tc &expr);

  /** Detect integer overflows in a cast. Takes a typecast2tc as an argument,
   *  and if it causes a decrease in integer width, then encodes a test that
   *  the dropped bits are never significant / used.
   *  @param expr Cast to test for dropped / overflowed data in.
   *  @return Boolean valued AST representing whether an overflow occurs. */
  virtual smt_astt overflow_cast(const expr2tc &expr);

  /** Detects integer overflows in negation. This only tests for the case where
   *  MIN_INT is being negated, in which case there is no positive
   *  representation of that number, and an overflow occurs. Evaluates to true
   *  if that can occur in the operand.
   *  @param expr A neg2tc to test for overflows in.
   *  @return Boolean valued AST representing whether an overflow occurs. */
  virtual smt_astt overflow_neg(const expr2tc &expr);

  /** Method to dump the SMT formula */
  virtual void dump_smt();

  /** Method to print the SMT model */
  virtual void print_model();

  /** @} */

  /** @{
   *  @name Array operations solver-converter API. */

  /** High level index expression conversion. Deals with several annoying
   *  corner cases that must be addressed, such as flattening multidimensional
   *  arrays into one domain sort, or turning bool arrays into bit arrays.
   *  XXX, why is this virtual?
   *  @param expr An index2tc expression to convert to an SMT AST.
   *  @return An AST representing the index operation in the expression. */
  virtual smt_astt convert_array_index(const expr2tc &expr);

  /** Partner method to convert_array_index, for stores.
   *  XXX, why is this virtual?
   *  @param expr with2tc operation to convert to SMT.
   *  @return AST representing the result of evaluating expr. */
  virtual smt_astt convert_array_store(const expr2tc &expr);

  /** @} */

  /** @{
   *  @name Internal foo. */

  /** Create a free variable with the given sort, and a unique name, with the
   *  prefix given in 'tag' */
  virtual smt_astt
  mk_fresh(smt_sortt s, const std::string &tag, smt_sortt st = nullptr);
  /** Create a previously un-used variable name with the prefix given in tag */
  std::string mk_fresh_name(const std::string &tag);

  void renumber_symbol_address(
    const expr2tc &guard,
    const expr2tc &addr_symbol,
    const expr2tc &new_size);

  /** Convert a type2tc into an smt_sort. This despatches control to the
   *  appropriate method in the subclassing solver converter for type
   *  conversion */
  smt_sortt convert_sort(const type2tc &type);
  /** Convert a terminal expression into an SMT AST. This despatches control to
   *  the appropriate method in the subclassing solver converter for terminal
   *  conversion */
  smt_astt convert_terminal(const expr2tc &expr);

  /** Flatten pointer arithmetic. When faced with an addition or subtraction
   *  between a pointer and some integer or other pointer, perform whatever
   *  multiplications or casting is requried to honour the C semantics of
   *  pointer arith. */
  smt_astt convert_pointer_arith(const expr2tc &expr, const type2tc &t);
  /** Compare two pointers. This attempts to optimise cases where we can avoid
   *  comparing the integer representation of a pointer, as that's hugely
   *  inefficient sometimes (and gets bitblasted).
   *  @param expr First pointer to compare
   *  @param expr2 Second pointer to compare
   *  @param templ_expr The comparision expression -- this method will look at
   *         the kind of comparison being performed, and make an appropriate
   *         decision.
   *  @return Boolean valued AST as appropriate to the requested comparision */
  smt_astt convert_ptr_cmp(
    const expr2tc &expr,
    const expr2tc &expr2,
    const expr2tc &templ_expr);
  /** Take the address of some kind of expression. This will abort if the given
   *  expression isn't based on some symbol in some way. (i.e., you can't take
   *  the address of an addition, but you can take the address of a member of
   *  a struct, for example). */
  smt_astt convert_addr_of(const expr2tc &expr);
  /** Handle union/struct based corner cases for member2tc expressions */
  smt_astt convert_member(const expr2tc &expr);
  /** Convert an identifier to a pointer. When given the name of a variable
   *  that we want to take the address of, this inspects our current tracking
   *  of addresses / variables, and returns a pointer for the given symbol.
   *  If it hasn't had its address taken before, performs any computations or
   *  address space juggling required to make a new pointer.
   *  @param expr The symbol2tc expression of this symbol.
   *  @param sym The textual representation of this symbol.
   *  @param type Optionally a pointer to the type of the symbol in the context.
   *  @return A pointer-typed AST representing the address of this symbol. */
  smt_astt convert_identifier_pointer(
    const expr2tc &expr,
    const std::string &sym,
    const typet *type);

  smt_astt init_pointer_obj(
    unsigned int obj_num,
    const expr2tc &size,
    const typet *type);

  /** Checks for equality with NaN representation. */
  smt_astt convert_is_nan(const expr2tc &expr);
  /** Checks for equality with inf representation. */
  smt_astt convert_is_inf(const expr2tc &expr);
  /** Checks for equality with normal representation. */
  smt_astt convert_is_normal(const expr2tc &expr);
  /** Checks for equality with finite representation. */
  smt_astt convert_is_finite(const expr2tc &expr);
  /** Converts signbit representation. */
  smt_astt convert_signbit(const expr2tc &expr);
  /** Converts popcount representation. */
  smt_astt convert_popcount(const expr2tc &expr);
  /** Converts bswap representation. */
  smt_astt convert_bswap(const expr2tc &expr);
  /** Converts rounding mode for ieee fp operations. */
  smt_astt convert_rounding_mode(const expr2tc &expr);
  /** Convert a byte_extract2tc, pulling a byte from the byte representation
   *  of some piece of data. */
  smt_astt convert_byte_extract(const expr2tc &expr);
  /** Convert a byte_update2tc, inserting a byte into the byte representation
   *  of some piece of data. */
  smt_astt convert_byte_update(const expr2tc &expr);
  /** Convert a bitcast2tc, converting an expr to its bit representation. */
  smt_astt convert_bitcast(const expr2tc &expr);
  /** Convert the given expr to AST, then assert that AST */
  void assert_expr(const expr2tc &e);
  /** Convert constant_array2tc's and constant_array_of2tc's */
  smt_astt array_create(const expr2tc &expr);

  /** Initialize tracking data for the address space records. This also sets
   *  up the symbols / addresses of 'NULL', '0', and the invalid pointer */
  void init_addr_space_array();
  /** Stores handle for the tuple interface. */
  void set_tuple_iface(tuple_iface *iface);
  /** Stores handle for the array interface. */
  void set_array_iface(array_iface *iface);
  /** Stores handle for the floating-point interface. */
  void set_fp_conv(fp_convt *iface);
  /** Store a new address-allocation record into the address space accounting.
   *  idx indicates the object number of this record. */
  void bump_addrspace_array(unsigned int idx, const expr2tc &val);
  /** Get the symbol name for the current address-allocation record array. */
  std::string get_cur_addrspace_ident();
  /** Create and assert address space constraints on the given object ID
   *  number. Essentially, this asserts that all the objects to date don't
   *  overlap with /this/ one. */
  void finalize_pointer_chain(unsigned int obj_num);

  /** Typecast data to bools */
  smt_astt convert_typecast_to_bool(const typecast2t &cast);
  /** Typecast to a fixedbv in bitvector mode */
  smt_astt convert_typecast_to_fixedbv_nonint(const expr2tc &cast);
  smt_astt convert_typecast_to_fixedbv_nonint_from_bv(const expr2tc &cast);
  smt_astt convert_typecast_to_fixedbv_nonint_from_bool(const expr2tc &cast);
  smt_astt convert_typecast_to_fixedbv_nonint_from_fixedbv(const expr2tc &cast);
  /** Typecast anything to an integer (but not pointers) */
  smt_astt convert_typecast_to_ints(const typecast2t &cast);
  smt_astt convert_typecast_to_ints_intmode(const typecast2t &cast);
  smt_astt convert_typecast_to_ints_from_fbv_sint(const typecast2t &cast);
  smt_astt convert_typecast_to_ints_from_unsigned(const typecast2t &cast);
  smt_astt convert_typecast_to_ints_from_bool(const typecast2t &cast);
  /** Typecast something (i.e. an integer) to a pointer */
  smt_astt convert_typecast_to_ptr(const typecast2t &cast);
  /** Typecast a pointer to an integer */
  smt_astt convert_typecast_from_ptr(const typecast2t &cast);
  /** Typecast structs to other structs */
  smt_astt convert_typecast_to_struct(const typecast2t &cast);
  /** Despatch a typecast expression to a more specific typecast method */
  smt_astt convert_typecast(const expr2tc &expr);
  /** Typecast to a floatbv*/
  smt_astt convert_typecast_to_fpbv(const typecast2t &cast);
  /** Typecast from a floatbv */
  smt_astt convert_typecast_from_fpbv(const typecast2t &cast);
  /** Round a real to an integer; not straightforwards at all. */
  smt_astt round_real_to_int(smt_astt a);
  /** Round a fixedbv to an integer. */
  smt_astt
  round_fixedbv_to_int(smt_astt a, unsigned int width, unsigned int towidth);

  /** Extract a type definition (i.e. a struct_union_data object) from a type.
   *  This method abstracts the fact that a pointer type is in fact a tuple. */
  const struct_union_data &get_type_def(const type2tc &type) const;
  /** Prep call for creating a tuple array */
  smt_astt tuple_array_create_despatch(const expr2tc &expr, smt_sortt domain);

  /** Convert a boolean to a bitvector with one bit. */
  smt_astt make_bool_bit(smt_astt a);
  /** Convert a bitvector with one bit to boolean. */
  smt_astt make_bit_bool(smt_astt a);

  /** Given an array index, extract the lower n bits of it, where n is the
   *  bitwidth of the array domain. */
  expr2tc fix_array_idx(const expr2tc &idx, const type2tc &array_type);
  /** For a multi-dimensional array, convert the type into a single dimension
   *  array. This works by concatenating the domain widths together into one
   *  large domain. */
  type2tc flatten_array_type(const type2tc &type);
  /** For a multi-dimensional constant array, flatten the actual definition of
   *  it down to a single dimension */
  expr2tc flatten_array_body(const expr2tc &expr);
  /** Get the base subtype of an array, delving through any intermediate
   *  multidimensional arrays. */
  type2tc get_flattened_array_subtype(const type2tc &type);
  /** Fetch the number of elements in an array (the domain). */
  expr2tc array_domain_to_width(const type2tc &type);

  /** For the given type, replace all instances of a pointer type with the
   *  struct representation of it. */
  void rewrite_ptrs_to_structs(type2tc &type);

  /** When dealing with multi-dimensional arrays, and selecting one element
   *  out of several dimensions, reduce it to an expression on a single
   *  dimensional array, by concatenating the indexes. Works in conjunction
   *  with flatten_array_type. */
  expr2tc decompose_select_chain(const expr2tc &expr, expr2tc &base);
  /** Like decompose_select_chain, but for multidimensional stores. */
  expr2tc decompose_store_chain(const expr2tc &expr, expr2tc &base);

  /** Prepare an array_of expression by flattening its dimensions, if it
   *  has more than one. */
  smt_astt convert_array_of_prep(const expr2tc &expr);
  /** Create an array of pointers; expects the init_val to be null, because
   *  there's no other way to initialize a pointer array in C, AFAIK. */
  smt_astt pointer_array_of(const expr2tc &init_val, unsigned long array_width);

  unsigned int
  get_member_name_field(const type2tc &t, const irep_idt &name) const;
  unsigned int
  get_member_name_field(const type2tc &t, const expr2tc &name) const;

  // Ours:
  /** Given an array expression, attempt to extract its valuation from the
   *  solver model, computing a constant_array2tc by calling get_array_elem. */
  expr2tc get_array(const type2tc &type, smt_astt array);
  expr2tc get_array(const expr2tc &expr);

  void delete_all_asts();

  /** @} */

  // Types

  // Type for (optional) AST cache

  struct smt_cache_entryt
  {
    const expr2tc val;
    smt_astt ast;
    unsigned int level;
  };

  typedef boost::multi_index_container<
    smt_cache_entryt,
    boost::multi_index::indexed_by<
      boost::multi_index::hashed_unique<
        BOOST_MULTI_INDEX_MEMBER(smt_cache_entryt, const expr2tc, val)>,
      boost::multi_index::ordered_non_unique<
        BOOST_MULTI_INDEX_MEMBER(smt_cache_entryt, unsigned int, level),
        std::greater<unsigned int>>>>
    smt_cachet;

  typedef std::unordered_map<type2tc, smt_sortt, type2_hash> smt_sort_cachet;

  // Members
  /** Number of un-popped context pushes encountered so far. */
  unsigned int ctx_level;

  /** A cache mapping expressions to converted SMT ASTs. */
  smt_cachet smt_cache;
  /** A cache of converted type2tc's to smt sorts */
  smt_sort_cachet sort_cache;
  /** Pointer_logict object, which contains some code for formatting how
   *  pointers are displayed in counter-examples. This is a list so that we
   *  can push and pop data when context push/pop operations occur. */
  std::list<pointer_logict> pointer_logic;
  /** Constant struct representing the implementation of the pointer type --
   *  i.e., the struct type that pointers get translated to. */
  type2tc pointer_struct;
  /** The type of the machine integer that can store a pointer. */
  type2tc machine_ptr;
  /** Sort for booleans. For fast access. */
  smt_sortt boolean_sort;
  /** Whether we are encoding expressions in integer mode or not. */
  bool int_encoding;
  /** A namespace containing all the types in the program. Used to resolve the
   *  rare case where we're doing some pointer arithmetic and need to have the
   *  concrete type of a pointer. */
  const namespacet &ns;
  /* Options contain all the parameters set by the user to run ESBMC */
  const optionst &options;

  bool ptr_foo_inited;

  smt_astt null_ptr_ast;
  smt_astt invalid_ptr_ast;

  /** Mapping of name prefixes to use counts: when we want a fresh new name
   *  with a particular prefix, this map stores how many times that prefix has
   *  been used, and thus what number should be appended to make the name
   *  unique. */
  std::map<std::string, unsigned int> fresh_map;

  /** Integer recording how many times the address space allocation record
   *  array has been modified. Essentially, this is like the SSA variable
   *  number, for an array we build / modify at conversion time. In a list so
   *  that we can support push/pop operations. */
  std::list<unsigned int> addr_space_sym_num;
  /** Type of the address space allocation records. Currently a start address
   *  integer and an end address integer. */
  type2tc addr_space_type;
  /** Type of the array of address space allocation records. */
  type2tc addr_space_arr_type;
  /** List of address space allocation sizes. A map from the object number to
   *  the nubmer of bytes allocated. In a list to support pushing and
   *  popping. */
  std::list<std::map<unsigned, unsigned>> addr_space_data;

  /** Holds the `__ESBMC_alloc` symbol convert_terminal() was last invoked with.
   */
  expr2tc current_valid_objects_sym;

  // XXX - push-pop will break here.
  typedef std::map<std::string, smt_astt> renumber_mapt;
  std::vector<renumber_mapt> renumber_map;

  /** Lifetime tracking of smt ast's. When a context is pop'd, all the ASTs
   *  created in that context are freed. */
  std::vector<smt_astt> live_asts;
  /** Accounting of live_asts for push/pop. Records the number of pointers
   *  contained when a push occurred. On pop, the live_asts vector is reset
   *  back to that point. */
  std::vector<unsigned int> live_asts_sizes;

  tuple_iface *tuple_api;
  array_iface *array_api;
  fp_convt *fp_api;

  // Workaround for integer shifts. This is an array of the powers of two,
  // up to 2^64.
  smt_astt int_shift_op_array;
};

/** Given an array type, create a type2tc representing its domain. */
type2tc make_array_domain_type(const array_type2t &arr);

/** Given an array type, calculate the domain bitwidth it should have. For
 *  nondeterministically or infinite sized arrays, this defaults to the
 *  machine integer width. */
unsigned long calculate_array_domain_width(const array_type2t &arr);

// Define here to enable inlining
inline smt_ast::smt_ast(smt_convt *ctx, smt_sortt s) : sort(s), context(ctx)
{
  assert(sort != nullptr);
  ctx->live_asts.push_back(this);
}

/* Type for push/pop-aware symbol table cache, required by some solvers */

struct symtab_entryt
{
  std::string val;
  smt_astt ast;
  unsigned int level;

  symtab_entryt(std::string val, smt_astt ast, unsigned int level)
    : val(std::move(val)), ast(ast), level(level)
  {
  }
};

typedef boost::multi_index_container<
  symtab_entryt,
  boost::multi_index::indexed_by<
    boost::multi_index::hashed_unique<
      BOOST_MULTI_INDEX_MEMBER(symtab_entryt, std::string, val)>,
    boost::multi_index::ordered_non_unique<
      BOOST_MULTI_INDEX_MEMBER(symtab_entryt, unsigned int, level),
      std::greater<unsigned int>>>>
  symtabt;

#endif /* _ESBMC_PROP_SMT_SMT_CONV_H_ */
