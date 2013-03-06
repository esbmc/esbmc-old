#ifndef _ESBMC_SOLVERS_SMTLIB_SMTLIB_CONV_H
#define _ESBMC_SOLVERS_SMTLIB_SMTLIB_CONV_H

#include <unistd.h>

#include <string>
#include <list>

#include <irep2.h>
#include <solvers/prop/prop_conv.h>
#include <solvers/smt/smt_conv.h>

class sexpr {
public:
  sexpr() : token(0), sexpr_list(), data() { sexpr_list.clear(); }
  unsigned int token; // If zero, then an sexpr list
  std::list<sexpr> sexpr_list;
  std::string data; // Text rep of parsed token.
};

class smtlib_smt_sort : public smt_sort {
};

class smtlib_smt_ast : public smt_ast {
public:
  smtlib_smt_ast(const smt_sort *s, smt_func_kind k) : smt_ast(s), kind(k) { }
  ~smtlib_smt_ast() { }
  smt_func_kind kind;
  std::string symname;
  BigInt intval;
  bool boolval;
  int extract_high;
  int extract_low;
  const smt_ast *args[4];
};

class smtlib_convt : public smt_convt {
public:
  smtlib_convt(bool int_encoding, const namespacet &_ns, bool is_cpp,
               const optionst &_options);
  ~smtlib_convt();

  virtual resultt dec_solve();
  virtual expr2tc get(const expr2tc &expr);
  virtual tvt l_get(literalt a);
  virtual const std::string solver_text();

  virtual void assert_lit(const literalt &l);
  virtual smt_ast *mk_func_app(const smt_sort *s, smt_func_kind k,
                               const smt_ast **args, unsigned int numargs);
  virtual smt_sort *mk_sort(const smt_sort_kind k, ...);
  virtual literalt mk_lit(const smt_ast *s);
  virtual smt_ast *mk_smt_int(const mp_integer &theint, bool sign);
  virtual smt_ast *mk_smt_real(const mp_integer &thereal);
  virtual smt_ast *mk_smt_bvint(const mp_integer &theint, bool sign,
                                unsigned int w);
  virtual smt_ast *mk_smt_bool(bool val);
  virtual smt_ast *mk_smt_symbol(const std::string &name, const smt_sort *s);
  virtual smt_sort *mk_struct_sort(const type2tc &type);
  virtual smt_sort *mk_union_sort(const type2tc &type);
  virtual smt_ast *mk_extract(const smt_ast *a, unsigned int high,
                              unsigned int low, const smt_sort *s);

  // Members
  const optionst &options;
  pid_t solver_proc_pid;
  FILE *out_stream;
  FILE *in_stream;
  std::string solver_name;
  std::string solver_version;

  // Actual solving data
  // The set of symbols and their sorts.
  std::map<std::string, const smt_sort *> symbol_table;
};

#endif /* _ESBMC_SOLVERS_SMTLIB_SMTLIB_CONV_H */
