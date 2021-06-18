/*******************************************************************\

Module: Symbolic Execution of ANSI-C

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include <esbmc/bmc.h>
#include <fstream>
#include <iostream>
#include <langapi/language_util.h>
#include <langapi/languages.h>
#include <langapi/mode.h>
#include <util/migrate.h>

void bmct::show_vcc(
  std::ostream &out,
  std::shared_ptr<symex_target_equationt> &eq)
{
  switch(ui)
  {
  case ui_message_handlert::OLD_GUI:
  case ui_message_handlert::XML_UI:
    error("not supported");
    return;

  case ui_message_handlert::PLAIN:
    break;

  default:
    assert(false);
  }

  out << "\n"
      << "VERIFICATION CONDITIONS:"
      << "\n"
      << "\n";

  languagest languages(ns, MODE_C);

  for(symex_target_equationt::SSA_stepst::iterator it = eq->SSA_steps.begin();
      it != eq->SSA_steps.end();
      it++)
  {
    if(!it->is_assert())
      continue;

    if(it->source.pc->location.is_not_nil())
      out << it->source.pc->location << "\n";

    if(it->comment != "")
      out << it->comment << "\n";

    symex_target_equationt::SSA_stepst::const_iterator p_it =
      eq->SSA_steps.begin();

    for(unsigned count = 1; p_it != it; p_it++)
      if(p_it->is_assume() || p_it->is_assignment())
        if(!p_it->ignore)
        {
          std::string string_value;
          languages.from_expr(migrate_expr_back(p_it->cond), string_value);
          out << "{-" << count << "} " << string_value << "\n";
          count++;
        }

    out << "|--------------------------"
        << "\n";

    std::string string_value;
    languages.from_expr(migrate_expr_back(it->cond), string_value);
    out << "{" << 1 << "} " << string_value << "\n";

    out << "\n";
  }
}

void bmct::show_vcc(std::shared_ptr<symex_target_equationt> &eq)
{
  const std::string &filename = options.get_option("output");

  if(filename.empty() || filename == "-")
  {
    std::stringstream output;
    show_vcc(output, eq);
    PRINT(output.str());
  }
  else
  {
    std::ofstream out(filename.c_str());
    if(!out)
      ERROR("failed to open " << filename << "\n")
    else
      show_vcc(out, eq);
  }
}
