#include <goto-programs/loopst.h>
#include <util/message.h>

const loopst::loop_varst &loopst::get_modified_loop_vars() const
{
  return modified_loop_vars;
}

const loopst::loop_varst &loopst::get_unmodified_loop_vars() const
{
  return unmodified_loop_vars;
}

const goto_programt::targett loopst::get_original_loop_exit() const
{
  return original_loop_exit;
}

void loopst::set_original_loop_exit(goto_programt::targett _loop_exit)
{
  original_loop_exit = _loop_exit;
}

const goto_programt::targett loopst::get_original_loop_head() const
{
  return original_loop_head;
}

void loopst::set_original_loop_head(goto_programt::targett _loop_head)
{
  original_loop_head = _loop_head;
}

void loopst::add_modified_var_to_loop(const expr2tc &expr)
{
  modified_loop_vars.insert(expr);
}

void loopst::add_unmodified_var_to_loop(const expr2tc &expr)
{
  unmodified_loop_vars.insert(expr);
}

void loopst::dump() const
{
  unsigned n = get_original_loop_head()->location_number;

  PRINT(n << " is head of (size: ");
  PRINT(size);
  PRINT(") { ");

  for(goto_programt::instructionst::iterator l_it = get_original_loop_head();
      l_it != get_original_loop_exit();
      ++l_it)
    PRINT((*l_it).location_number << ", ");
  PRINT(get_original_loop_exit()->location_number);

  PRINT(
    " }"
    << "\n");

  dump_loop_vars();
}

void loopst::dump_loop_vars() const
{
  PRINT("Loop variables:\n");
  unsigned int i = 0;
  for(auto var : modified_loop_vars)
    PRINT(++i << ". \t" << to_symbol2t(var).thename << '\n');
  PRINT('\n');
}
