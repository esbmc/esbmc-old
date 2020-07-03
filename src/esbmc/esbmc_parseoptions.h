/*******************************************************************\

Module: Command Line Parsing

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#ifndef CPROVER_ESBMC_PARSEOPTIONS_H
#define CPROVER_ESBMC_PARSEOPTIONS_H

#include <esbmc/bmc.h>
#include <goto-programs/goto_convert_functions.h>
#include <langapi/language_ui.h>
#include <util/cmdline.h>
#include <util/options.h>
#include <util/parseoptions.h>
#include <util/ui_message.h>

extern const struct opt_templ esbmc_options[];

class esbmc_parseoptionst : public parseoptions_baset, public language_uit
{
public:
  int doit() override;
  void help() override;

  esbmc_parseoptionst(int argc, const char **argv)
    : parseoptions_baset(esbmc_options, argc, argv), language_uit(cmdline)
  {
  }

protected:
  virtual void get_command_line_options(optionst &options);
  virtual int do_bmc(bmct &bmc);

  virtual bool
  get_goto_program(optionst &options, goto_functionst &goto_functions);

  virtual bool
  process_goto_program(optionst &options, goto_functionst &goto_functions);

  int doit_k_induction();
  int doit_k_induction_parallel();

  int doit_falsification();
  int doit_incremental();
  int doit_incremental_cb();
  int doit_termination();

  int do_base_case(
    optionst &opts,
    goto_functionst &goto_functions,
    const BigInt &k_step);

  int do_forward_condition(
    optionst &opts,
    goto_functionst &goto_functions,
    const BigInt &k_step);

  int do_inductive_step(
    optionst &opts,
    goto_functionst &goto_functions,
    const BigInt &k_step);

  bool read_goto_binary(goto_functionst &goto_functions);

  bool set_claims(goto_functionst &goto_functions);

  void set_verbosity_msg(messaget &message);

  uint64_t read_time_spec(const char *str);
  uint64_t read_mem_spec(const char *str);

  void preprocessing();

  void print_ileave_points(namespacet &ns, goto_functionst &goto_functions);

public:
  goto_functionst goto_functions;
};

#endif
