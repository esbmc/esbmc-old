#include "irep2/irep2_expr.h"
#include <goto-programs/goto_cfg.h>

goto_cfg::goto_cfg(goto_functionst &goto_functions)
{
  Forall_goto_functions (f_it, goto_functions)
  {
    if (!f_it->second.body_available)
      continue;

    goto_programt &body = f_it->second.body;

    // First pass - identify all the leaders
    std::set<goto_programt::instructionst::iterator> leaders;
    leaders.insert(
      body.instructions.begin()); // First instruction is always a leader

    Forall_goto_program_instructions (i_it, body)
    {
      if (i_it->is_target())
      {
        leaders.insert(i_it);
      }

      if (i_it->is_goto() || i_it->is_backwards_goto())
      {
        for (const auto &target : i_it->targets)
          leaders.insert(target);

        auto next = i_it;
        next++;
        leaders.insert(next);
      }

      if (i_it->is_return())
      {
        auto next = i_it;
        next++;
        leaders.insert(next);
      }

      if (i_it->is_throw() || i_it->is_catch())
      {
        log_error("[CFG], Throw and catch instructions are not supported yet");
        abort();
      }

      // TODO: there are some special C functions that should be handled: exit, longjmp, etc.
    }

    // Second pass - identify all the basic blocks
    auto start = body.instructions.begin();
    const auto &end = body.instructions.end();
    std::vector<std::shared_ptr<basic_block>> bbs;

    while (start != end)
    {
      std::shared_ptr<basic_block> bb = std::make_shared<basic_block>();
      bb->begin = start;
      start++;
      bb->end = start;

      while (start != end && leaders.find(start) == leaders.end())
      {
        start++;
        bb->end = start;
      }

      if (bbs.size() > 0)
      {
        bbs.back()->successors.insert(bb);
        bb->predecessors.insert(bbs.back());
      }
      bbs.push_back(bb);
    }

    // Third pass - identify all the successors/predecessors
    for (auto &bb : bbs)
    {
      assert(bb->begin != bb->end);
      auto last = bb->end;
      last--;

      for (const auto &bb2 : bbs)
      {
        if (bb2->begin == bb->end)
        {
          bb->successors.insert(bb2);
          bb2->predecessors.insert(bb);
        }

        if (last->is_goto() || last->is_backwards_goto())
        {
          bb->terminator = basic_block::terminator_type::IF_GOTO;
          for (const auto &target : last->targets)
          {
            if (target == bb2->begin)
            {
              bb->successors.insert(bb2);
              bb2->predecessors.insert(bb);
            }
          }
        }
      }
    }

    basic_blocks[f_it->first.as_string()] = bbs;
  }

  log_progress("Finished CFG construction");
}

#include <fstream>

void goto_cfg::dump_graph() const
{
  log_status("Dumping CFG");
  for (const auto &[function, bbs] : basic_blocks)
  {
    std::ofstream file("cfg_" + function + ".dot");
    file << "digraph G {\n";
    for (size_t t = 0; t < bbs.size(); t++)
    {
      file << "BB" << t << " [shape=record, label=\"{" << t << ":\\l|";
      for (auto i = bbs[t]->begin; i != bbs[t]->end; i++)
      {
        std::ostringstream oss;
        i->output_instruction(*migrate_namespace_lookup, "", oss);
        // TODO: escape special characters
        file << oss.str() << "\\l";
      }

      switch (bbs[t]->terminator)
      {
      case basic_block::terminator_type::IF_GOTO:
      {
        file << "|{<s0>T|<s1>F}}\"];\n";
        auto suc = bbs[t]->successors.begin();
        file << "BB" << t << ":s0"
             << " -> "
             << "BB"
             << std::distance(
                  bbs.begin(), std::find(bbs.begin(), bbs.end(), *suc))
             << ";\n";
        suc++;
        file << "BB" << t << ":s1"
             << " -> "
             << "BB"
             << std::distance(
                  bbs.begin(), std::find(bbs.begin(), bbs.end(), *suc))
             << ";\n";
      }
      break;

      default:
        file << "}\"];\n";
        for (const auto &suc : bbs[t]->successors)
          file << "BB" << t << " -> "
               << "BB"
               << std::distance(
                    bbs.begin(), std::find(bbs.begin(), bbs.end(), suc))
               << ";\n";
        break;
      }
    }
    file << "}\n";
  }
}

template <class F>
void goto_cfg::foreach_bb(
  const std::shared_ptr<goto_cfg::basic_block> &start,
  F foo) 
{
  std::unordered_set<std::shared_ptr<goto_cfg::basic_block>> visited;
  std::vector<std::shared_ptr<goto_cfg::basic_block>> to_visit({start});

  while (!to_visit.empty())
  {
    const auto &item = to_visit.back();
    visited.insert(item);
    to_visit.pop_back();
    foo(item);
    for (const auto &next : item->successors)
      if (!visited.count(next))
        to_visit.push_back(next);
  }
}


void goto_cfg::Dominator::compute_dominators()
{
  // Computes dominator of a node
  std::unordered_map<
    std::shared_ptr<basic_block>,
    std::unordered_set<std::shared_ptr<basic_block>>> dt;


  // 1. Sets all nodes dominators to TOP
  
  std::unordered_set<std::shared_ptr<basic_block>> top;
  foreach_bb(
    start, [&top](const std::shared_ptr<basic_block> &n) { top.insert(n); });

  foreach_bb(
    start, [&dt, &top](const std::shared_ptr<basic_block> &n) { dt[n] = top; });


  /*
    Dom(n) = {n}, if n = start
             {n} `union` (intersec_pred Dom(p))
   */
  dt[start] = std::unordered_set<std::shared_ptr<basic_block>>({start});

  std::vector<std::shared_ptr<goto_cfg::basic_block>> worklist{start};
  for (const auto &suc : start->successors)
     worklist.push_back(suc);

  while (!worklist.empty())
  {    
    const auto &dominator_node = worklist.back();
    worklist.pop_back();

    if (dominator_node == start)
      continue;

    // assert(!dominator_node->predecessors.empty());

    // Get the intersection of all the predecessors
    std::unordered_set<std::shared_ptr<basic_block>> intersection = top;
    for (const auto &pred : dominator_node->predecessors)
    {
      assert(dt.count(pred));
      for (const auto &item : top)
        if (!dt[pred].count(item))
          intersection.erase(item);
    }

    // Union of node with its predecessor intersection
    std::unordered_set<std::shared_ptr<basic_block>> result({dominator_node});
    for (const auto &item : intersection)
      result.insert(item);

    // Fix-point?
    if (dt[dominator_node] != result)
    {
      dt[dominator_node] = result;
      for (const auto &suc : dominator_node->successors)
          worklist.push_back(suc);
    }
  }

  dominators = dt;
}


void goto_cfg::Dominator::dump_dominators() const
{
  for (const auto &[node, edges] : dominators)
  {
    log_status("Node");
    node->begin->dump();

    log_status("Dominated");
    for (const auto &edge : edges)
      edge->begin->dump();
  }
}

goto_cfg::Dominator::Node goto_cfg::Dominator::idom(const Node &n) const
{
  std::vector<Node> sdoms;
  const auto &n_dominators = dom(n);
  std::copy_if(
    n_dominators.begin(),
    n_dominators.end(),
    std::back_inserter(sdoms),
    [&n,this](const auto &item) {return sdom(item,n); });

  for (const Node &n0 : sdoms)
  {
    bool valid_result = true;
    for (const Node &n1 : sdoms)
    {
      if (sdom(n0, n1))
      {
        valid_result = false;
        break;
      }
    }
    if (valid_result)
      return n0;
  }
  log_error("[cfg] Unable to compute `idom`");
  abort();
}

goto_cfg::Dominator::DomTree goto_cfg::Dominator::dom_tree() const
{
  DomTree dt;
  dt.first = start;

  foreach_bb(
    start,
    [this, &dt](const Node &n)
    {
      if (n == start)
        return;

      Node root = idom(n);
      auto [val, ins] = dt.second.insert({root, std::unordered_set<Node>()});
      val->second.insert(n);
    });

  return dt;
}

void goto_cfg::Dominator::dump_idoms() const
{
  DomTree dt = dom_tree();

  log_status("Root: ");
  dt.first->begin->dump();

  for (const auto &[key, value] : dt.second)
  {
    log_status("Node: ");
    key->begin->dump();
    log_status("Edges");
    for (const auto &n : value)
      n->begin->dump();
  }
  
}
