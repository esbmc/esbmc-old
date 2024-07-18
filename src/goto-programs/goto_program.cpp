#include <goto-programs/goto_program.h>
#include <iomanip>
#include <langapi/language_util.h>

void goto_programt::instructiont::dump() const
{
  std::ostringstream oss;
  output_instruction(*migrate_namespace_lookup, "", oss);
  log_status("{}", oss.str());
}

void goto_programt::instructiont::output_instruction(
  const class namespacet &ns,
  const irep_idt &identifier,
  std::ostream &out,
  bool show_location) const
{
  if (show_location)
  {
    out << "        // " << location_number << " ";

    if (!location.is_nil())
      out << location.as_string();
    else
      out << "no location";

    out << "\n";
  }

  if (!labels.empty())
  {
    out << "        // Labels:";
    for (const auto &label : labels)
    {
      out << " " << label;
    }

    out << "\n";
  }

  if (is_target())
    out << std::setw(6) << target_number << ": ";
  else
    out << "        ";

  switch (type)
  {
  case NO_INSTRUCTION_TYPE:
    out << "NO INSTRUCTION TYPE SET"
        << "\n";
    break;

  case GOTO:
    if (!is_true(guard))
    {
      out << "IF " << from_expr(ns, identifier, guard) << " THEN ";
    }

    out << "GOTO ";

    for (instructiont::targetst::const_iterator gt_it = targets.begin();
         gt_it != targets.end();
         gt_it++)
    {
      if (gt_it != targets.begin())
        out << ", ";
      out << (*gt_it)->target_number;
    }

    out << "\n";
    break;

  case FUNCTION_CALL:
    out << "FUNCTION_CALL:  " << from_expr(ns, "", migrate_expr_back(code))
        << "\n";
    break;

  case RETURN:
  {
    std::string arg;
    const code_return2t &ref = to_code_return2t(code);
    if (!is_nil_expr(ref.operand))
      arg = from_expr(ns, "", ref.operand);
    out << "RETURN: " << arg << "\n";
  }
  break;

  case DECL:
    out << "DECL " << from_expr(ns, identifier, code) << "\n";
    break;
  case DEAD:
    out << "DEAD " << to_code_dead2t(code).value << "\n";
    break;
  case OTHER:
    out << "OTHER " << from_expr(ns, identifier, code) << "\n";
    break;
  case ASSIGN:
    out << "ASSIGN " << from_expr(ns, identifier, code) << "\n";
    break;

  case ASSUME:
  case ASSERT:
    if (is_assume())
      out << "ASSUME ";
    else
      out << "ASSERT ";

    {
      out << from_expr(ns, identifier, guard);

      const irep_idt &comment = location.comment();
      if (comment != "")
        out << " // " << comment;
    }

    out << "\n";
    break;

  case SKIP:
    out << "SKIP"
        << "\n";
    break;

  case END_FUNCTION:
    out << "END_FUNCTION";
    {
      const irep_idt &function = location.function();
      if (function != "")
        out << " // " << function;
    }
    out << "\n";
    break;

  case LOCATION:
    out << "LOCATION"
        << "\n";
    break;

  case THROW:
    out << "THROW";

    {
      const code_cpp_throw2t &throw_ref = to_code_cpp_throw2t(code);
      for (auto const &it : throw_ref.exception_list)
      {
        if (it != *throw_ref.exception_list.begin())
          out << ",";
        out << " " << it;
      }

      if (!is_nil_expr(throw_ref.operand))
        out << ": " << from_expr(ns, identifier, throw_ref.operand);
    }

    out << "\n";
    break;

  case CATCH:
    out << "CATCH ";

    {
      unsigned i = 0;
      const code_cpp_catch2t &catch_ref = to_code_cpp_catch2t(code);
      assert(targets.size() == catch_ref.exception_list.size());

      for (instructiont::targetst::const_iterator gt_it = targets.begin();
           gt_it != targets.end();
           gt_it++, i++)
      {
        if (gt_it != targets.begin())
          out << ", ";
        out << catch_ref.exception_list[i] << "->" << (*gt_it)->target_number;
      }
    }

    out << "\n";
    break;

  case ATOMIC_BEGIN:
    out << "ATOMIC_BEGIN"
        << "\n";
    break;

  case ATOMIC_END:
    out << "ATOMIC_END"
        << "\n";
    break;

  case THROW_DECL:
    out << "THROW_DECL (";

    {
      const code_cpp_throw_decl2t &ref = to_code_cpp_throw_decl2t(code);

      for (unsigned int i = 0; i < ref.exception_list.size(); ++i)
      {
        if (i)
          out << ", ";
        out << ref.exception_list[i];
      }
      out << ")";
    }

    out << "\n";
    break;

  case THROW_DECL_END:
    out << "THROW_DECL_END";
    out << "\n";
    break;

  default:
    throw "unknown statement";
  }
}

bool operator<(
  const goto_programt::const_targett i1,
  const goto_programt::const_targett i2)
{
  const goto_programt::instructiont &_i1 = *i1;
  const goto_programt::instructiont &_i2 = *i2;
  return &_i1 < &_i2;
}

void goto_programt::compute_loop_numbers(unsigned int &num)
{
  for (auto &instruction : instructions)
    if (instruction.is_backwards_goto())
    {
      (*instruction.targets.begin())->loop_number = num;
      instruction.loop_number = num++;
    }
}

// This method takes an iterator "target" into the list of instructions
// and returns a list of iterators "successors" to the successor
// instructions of "target".
//
// In this context, a successor is any instruction that may be
// executed after the given instruction (i.e., specified by "target").
// In some cases (see below) we can trivially reject
// some instructions as successors.
void goto_programt::get_successors(
  const_targett target,
  const_targetst &successors) const
{
  successors.clear();

  // The last instruction does not have any successors
  if (target == instructions.end())
    return;

  const auto next = std::next(target);
  const instructiont &i = *target;

  if (i.is_goto())
  {
    // GOTO instructions may have multiple successors:
    // the next instruction in the list + all the instructions
    // this jump may lead to.

    // If the guard is definitely FALSE, then the corresponding
    // jump can never occur. Hence, we can ignore all jump targets
    // as successors. Otherwise, we consider these targets as successors.
    if (!is_false(i.guard))
    {
      for (auto target : i.targets)
        successors.emplace_back(target);
    }

    // If the guard is definitely TRUE, then this GOTO
    // jump always happens. Hence, only one kind of successors
    // is possible -- the target instructions of the GOTO jump.
    if (!is_true(i.guard) && next != instructions.end())
      successors.push_back(next);
  }
  else if (i.is_return())
  {
    // A RETURN instruction is basically an "unguarded" jump to
    // the corresponding END_FUNCTION instruction.
    successors.push_back(--instructions.end());
  }
  else if (i.is_assume() || i.is_assert())
  {
    // This is an ASSERT or ASSUME with a guard that
    // might hold (i.e., definitely not FALSE), so the next target
    // might be reached.
    if (!is_false(i.guard))
      successors.push_back(next);
  }
  else if (i.is_catch())
  {
    // CATCH signed_int->1, bool->2, float->3
    // we consider these targets as successors
    for (auto target : i.targets)
      successors.emplace_back(target);

    successors.push_back(next);
  }
  else
    successors.push_back(next);
}

void goto_programt::update()
{
  compute_target_numbers();
  compute_location_numbers();
}

std::ostream &goto_programt::output(
  const namespacet &ns,
  const irep_idt &identifier,
  std::ostream &out) const
{
  // output program

  for (const auto &instruction : instructions)
    instruction.output_instruction(ns, identifier, out);

  return out;
}

void goto_programt::compute_target_numbers()
{
  // reset marking

  for (auto &instruction : instructions)
    instruction.target_number = -1;

  // mark the goto targets

  for (instructionst::const_iterator it = instructions.begin();
       it != instructions.end();
       it++)
  {
    for (auto t : it->targets)
    {
      if (t != instructions.end())
        t->target_number = 0;
    }
  }

  // number the targets properly
  unsigned cnt = 0;

  for (instructionst::iterator it = instructions.begin();
       it != instructions.end();
       it++)
  {
    if (it->is_target())
    {
      it->target_number = ++cnt;
      assert(it->target_number != 0);
    }
  }

  // check the targets!
  // (this is a consistency check only)

  for (instructionst::const_iterator it = instructions.begin();
       it != instructions.end();
       it++)
  {
    for (auto t : it->targets)
    {
      if (t != instructions.end())
      {
        assert(t->target_number != 0);
        assert(t->target_number != unsigned(-1));
      }
    }
  }
}

void goto_programt::copy_from(const goto_programt &src)
{
  // Definitions for mapping between the two programs
  typedef std::map<const_targett, targett> targets_mappingt;
  targets_mappingt targets_mapping;

  clear();

  // Copy hide flag
  hide = src.hide;

  // Loop over program - 1st time collects targets and copy

  for (instructionst::const_iterator it = src.instructions.begin();
       it != src.instructions.end();
       it++)
  {
    targett new_instruction = add_instruction();
    targets_mapping[it] = new_instruction;
    *new_instruction = *it;
  }

  // Loop over program - 2nd time updates targets

  for (auto &instruction : instructions)
  {
    for (auto &target : instruction.targets)
    {
      targets_mappingt::iterator m_target_it = targets_mapping.find(target);

      if (m_target_it == targets_mapping.end())
        throw "copy_from: target not found";

      target = m_target_it->second;
    }
  }

  compute_target_numbers();
}

std::ostream &operator<<(std::ostream &out, goto_program_instruction_typet t)
{
  switch (t)
  {
  case NO_INSTRUCTION_TYPE:
    out << "NO_INSTRUCTION_TYPE";
    break;
  case GOTO:
    out << "GOTO";
    break;
  case ASSUME:
    out << "ASSUME";
    break;
  case ASSERT:
    out << "ASSERT";
    break;
  case OTHER:
    out << "OTHER";
    break;
  case SKIP:
    out << "SKIP";
    break;
  case LOCATION:
    out << "LOCATION";
    break;
  case END_FUNCTION:
    out << "END_FUNCTION";
    break;
  case ATOMIC_BEGIN:
    out << "ATOMIC_BEGIN";
    break;
  case ATOMIC_END:
    out << "ATOMIC_END";
    break;
  case RETURN:
    out << "RETURN";
    break;
  case ASSIGN:
    out << "ASSIGN";
    break;
  case DECL:
    out << "DECL";
    break;
  case DEAD:
    out << "DEAD";
    break;
  case FUNCTION_CALL:
    out << "FUNCTION_CALL";
    break;
  case THROW:
    out << "THROW";
    break;
  case CATCH:
    out << "CATCH";
    break;
  case THROW_DECL:
    out << "THROW_DECL";
    break;
  case THROW_DECL_END:
    out << "THROW_DECL_END";
    break;
  default:
    assert(!"Unknown instruction type");
    out << "unknown instruction";
  }

  return out;
}

void goto_programt::dump() const
{
  std::ostringstream oss;
  output(*migrate_namespace_lookup, "", oss);
  log_status("{}", oss.str());
}

void goto_programt::get_decl_identifiers(
  decl_identifierst &decl_identifiers) const
{
  forall_goto_program_instructions (it, (*this))
  {
    if (it->is_decl())
      decl_identifiers.insert(to_code_decl2t(it->code).value);
  }
}
