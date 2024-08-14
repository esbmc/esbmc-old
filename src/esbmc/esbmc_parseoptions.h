#ifndef CPROVER_ESBMC_PARSEOPTIONS_H
#define CPROVER_ESBMC_PARSEOPTIONS_H

#include <esbmc/bmc.h>
#include <goto-programs/goto_convert_functions.h>
#include <langapi/language_ui.h>
#include <util/cmdline.h>
#include <util/options.h>
#include <util/parseoptions.h>
#include <util/algorithms.h>
#include <util/threeval.h>

extern const struct group_opt_templ all_cmd_options[];

class esbmc_parseoptionst : public parseoptions_baset, public language_uit
{
public:
  int doit() override;
  void help() override;

  esbmc_parseoptionst(int argc, const char **argv)
    : parseoptions_baset(all_cmd_options, argc, argv)
  {
  }

  ~esbmc_parseoptionst()
  {
    close_file(out);
  }

protected:
  virtual void get_command_line_options(optionst &options);
  virtual int do_bmc(bmct &bmc);

  virtual bool
  get_goto_program(optionst &options, goto_functionst &goto_functions);

  virtual bool
  create_goto_program(optionst &options, goto_functionst &goto_functions);

  virtual bool
  parse_goto_program(optionst &options, goto_functionst &goto_functions);

  virtual bool
  process_goto_program(optionst &options, goto_functionst &goto_functions);

  virtual bool
  output_goto_program(optionst &options, goto_functionst &goto_functions);

  int do_bmc_strategy(optionst &options, goto_functionst &goto_functions);

  int doit_k_induction_parallel();

  tvt is_base_case_violated(
    optionst &options,
    goto_functionst &goto_functions,
    const BigInt &k_step);

  tvt does_forward_condition_hold(
    optionst &options,
    goto_functionst &goto_functions,
    const BigInt &k_step);

  tvt is_inductive_step_violated(
    optionst &options,
    goto_functionst &goto_functions,
    const BigInt &k_step);

  bool read_goto_binary(goto_functionst &goto_functions);

  bool set_claims(goto_functionst &goto_functions);

  uint64_t read_time_spec(const char *str);
  uint64_t read_mem_spec(const char *str);

  void preprocessing();

  void add_property_monitors(goto_functionst &goto_functions, namespacet &ns);
  expr2tc calculate_a_property_monitor(
    const std::string &prefix,
    std::set<std::string> &used_syms) const;
  void add_monitor_exprs(
    goto_programt::targett insn,
    goto_programt::instructionst &insn_list,
    const std::map<std::string, std::pair<std::set<std::string>, expr2tc>>
      &monitors);

  void print_ileave_points(namespacet &ns, goto_functionst &goto_functions);

  FILE *out = stderr;

  std::vector<std::unique_ptr<goto_functions_algorithm>>
    goto_preprocess_algorithms;

private:
  void close_file(FILE *f)
  {
    if (f != stdout && f != stderr)
    {
      fclose(f);
    }
  }

public:
  goto_functionst goto_functions;

  // for multi-kind/incr
  std::set<std::pair<std::string, std::string>> to_remove_claims;
};

#endif
