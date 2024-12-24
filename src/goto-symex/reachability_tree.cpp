/* Byte order includes, for context switch checkpoint files */
#ifndef _WIN32
#  include <arpa/inet.h>
#  include <netinet/in.h>
#else
#  include <winsock2.h>
#  undef small // mingw workaround
#endif

#include <goto-symex/goto_symex.h>
#include <goto-symex/reachability_tree.h>
#include <util/config.h>
#include <util/crypto_hash.h>
#include <util/expr_util.h>
#include <util/i2string.h>
#include <util/message.h>
#include <util/std_expr.h>

reachability_treet::reachability_treet(
  goto_functionst &goto_functions,
  const namespacet &ns,
  optionst &opts,
  std::shared_ptr<symex_targett> target,
  contextt &context)
  : goto_functions(goto_functions),
    permanent_context(context),
    ns(ns),
    options(opts)
{
  // Put a few useful symbols in the symbol table.
  symbolt sym;
  sym.type = bool_typet();
  sym.id = "execution_statet::\\guard_exec";
  sym.name = "execution_statet::\\guard_exec";
  context.move(sym);

  CS_bound = atoi(options.get_option("context-bound").c_str());
  TS_slice = atoi(options.get_option("time-slice").c_str());
  state_hashing = options.get_bool_option("state-hashing");
  directed_interleavings = options.get_bool_option("direct-interleavings");
  interactive_ileaves = options.get_bool_option("interactive-ileaves");
  schedule = options.get_bool_option("schedule");
  smt_during_symex = options.get_bool_option("smt-during-symex");
  por = !options.get_bool_option("no-por");
  main_thread_ended = false;
  target_template = std::move(target);
}

void reachability_treet::setup_for_new_explore()
{
  std::shared_ptr<symex_targett> targ;

  execution_states.clear();

  has_complete_formula = false;

  execution_statet *s;
  if (schedule)
  {
    schedule_target = target_template->clone();
    targ = schedule_target;
    s = reinterpret_cast<execution_statet *>(new schedule_execution_statet(
      goto_functions,
      ns,
      this,
      schedule_target,
      permanent_context,
      options,
      &schedule_total_claims,
      &schedule_remaining_claims));
  }
  else
  {
    targ = target_template->clone();
    s = reinterpret_cast<execution_statet *>(new dfs_execution_statet(
      goto_functions, ns, this, targ, permanent_context, options));
    schedule_target = nullptr;
  }

  execution_states.emplace_back(s);
  cur_state_it = execution_states.begin();
  targ->push_ctx(); // Start with a depth of 1.
}

execution_statet &reachability_treet::get_cur_state()
{
  return **cur_state_it;
}

const execution_statet &reachability_treet::get_cur_state() const
{
  return **cur_state_it;
}

bool reachability_treet::has_more_states()
{
  return execution_states.size() > 0;
}

int reachability_treet::get_CS_bound() const
{
  return CS_bound;
}

bool reachability_treet::check_for_hash_collision() const
{
  const execution_statet &ex_state = get_cur_state();

  crypto_hash hash;
  hash = ex_state.generate_hash();
  if (hit_hashes.find(hash) != hit_hashes.end())
    return true;

  return false;
}

void reachability_treet::post_hash_collision_cleanup()
{
  for (auto &&it : get_cur_state().DFS_traversed)
    it = true;
}

void reachability_treet::update_hash_collision_set()
{
  execution_statet &ex_state = get_cur_state();

  crypto_hash hash;
  hash = ex_state.generate_hash();
  hit_hashes.insert(hash);
}

void reachability_treet::create_next_state()
{
  execution_statet &ex_state = get_cur_state();

  if (next_thread_id != ex_state.threads_state.size())
  {
    auto new_state = ex_state.clone();
    execution_states.push_back(new_state);

    /* Make it active, make it follow on from previous state... */
    if (new_state->get_active_state_number() != next_thread_id)
      new_state->increment_context_switch();

    new_state->switch_to_thread(next_thread_id);
    new_state->update_after_switch_point();
  }
}

bool reachability_treet::step_next_state()
{
  next_thread_id = decide_ileave_direction(get_cur_state());
  if (next_thread_id != get_cur_state().threads_state.size())
  {
    create_next_state();
    return true;
  }

  return false;
}

unsigned int
reachability_treet::decide_ileave_direction(execution_statet &ex_state)
{
  unsigned int tid = 0, user_tid = 0;

  if (interactive_ileaves)
  {
    tid = get_ileave_direction_from_user();
    user_tid = tid;
  }

  for (; tid < ex_state.threads_state.size(); tid++)
  {
    /* For all threads: */
    if (!check_thread_viable(tid, true))
      continue;

    if (!ex_state.dfs_explore_thread(tid))
      continue;

#if 0
    //apply static partial-order reduction
    if (por && !ex_state.is_thread_mpor_schedulable(tid))
      continue;
#endif

    break;
  }

  if (interactive_ileaves && tid != user_tid)
  {
    log_error("Ileave code selected different thread from user choice");
    abort();
  }

  return tid;
}

bool reachability_treet::is_has_complete_formula()
{
  return has_complete_formula;
}

void reachability_treet::switch_to_next_execution_state()
{
  std::list<std::shared_ptr<execution_statet>>::iterator it = cur_state_it;
  it++;

  if (it != execution_states.end())
  {
    cur_state_it++;
  }
  else
  {
    if (step_next_state())
    {
      cur_state_it++;
    }
    else
    {
      if (config.options.get_bool_option("print-stack-traces"))
        print_ileave_trace();
      has_complete_formula = true;
    }
  }
}

bool reachability_treet::reset_to_unexplored_state()
{
  // After executing up to a point where all threads have ended and returning
  // that equation to the caller, free and remove fully explored execution
  // states back to the point where there's an unexplored one.

  // Eliminate final execution state, then attempt to generate another one from
  // the last on the list. If we can, it's an unexplored state, if we can't,
  // all depths from the current execution state are explored, so delete it.

  auto it = cur_state_it--;
  execution_states.erase(it);

  while (execution_states.size() > 0 && !step_next_state())
  {
    it = cur_state_it--;
    execution_states.erase(it);
  }

  if (execution_states.size() > 0)
    cur_state_it++;

  if (execution_states.size() && !smt_during_symex)
  {
    // When backtracking, erase all the assertions from the equation before
    // continuing forwards. They've all already been checked, in the trace we
    // just backtracked from. Thus there's no point in checking them again.
    symex_target_equationt *eq =
      static_cast<symex_target_equationt *>((*cur_state_it)->target.get());
    unsigned int num_asserts = eq->clear_assertions();

    // Remove them from the count of remaining assertions to check. This allows
    // for more traces to be discarded because they do not contain any
    // unchecked assertions.
    (*cur_state_it)->total_claims -= num_asserts;
    (*cur_state_it)->remaining_claims -= num_asserts;
  }

  return execution_states.size();
}

void reachability_treet::go_next_state()
{
  std::list<std::shared_ptr<execution_statet>>::iterator it = cur_state_it;
  it++;
  if (it != execution_states.end())
    cur_state_it++;
  else
  {
    while (execution_states.size() > 0 && !step_next_state())
    {
      it = cur_state_it;
      cur_state_it--;

      // For the last one:
      if (execution_states.size() == 1)
        (*it)->add_memory_leak_checks();

      execution_states.erase(it);
    }

    if (execution_states.size() > 0)
      cur_state_it++;
  }
}

reachability_treet::dfs_position::dfs_position(const reachability_treet &rt)
{
  std::list<std::shared_ptr<execution_statet>>::const_iterator it;

  // Iterate through each position in the DFS tree recording data into this
  // object.
  for (it = rt.execution_states.begin(); it != rt.execution_states.end(); it++)
  {
    reachability_treet::dfs_position::dfs_state state;
    auto ex = *it;
    state.location_number = ex->get_active_state().source.pc->location_number;
    state.num_threads = ex->threads_state.size();
    state.explored = ex->DFS_traversed;

    // The thread taken in this DFS path isn't decided at this execution state,
    // instead it's whatever thread is active in the /next/ state. So, take the
    // currently active thread no and assign it to the previous dfs state
    // we recorded.
    if (states.size() > 0)
      states.back().cur_thread = ex->get_active_state_number();

    states.push_back(state);
  }

  // The final execution state in a DFS is a dummy, there are no paths from it,
  // so assign a dummy cur_thread value.
  states.back().cur_thread = 0;

  checksum = 0; // Use this in the future.
  ileaves = 0;  // Can use this depending on a future refactor.
}

reachability_treet::dfs_position::dfs_position(const std::string &&filename)
{
  read_from_file(std::move(filename));
}

const uint32_t reachability_treet::dfs_position::file_magic =
  0x4543484B; //'ECHK'

bool reachability_treet::dfs_position::write_to_file(
  const std::string &&filename) const
{
  uint8_t buffer[8192];
  reachability_treet::dfs_position::file_hdr hdr;
  reachability_treet::dfs_position::file_entry entry;
  std::vector<bool>::const_iterator ex_it;
  std::vector<reachability_treet::dfs_position::dfs_state>::const_iterator it;
  FILE *f;
  unsigned int i;

  f = fopen(filename.c_str(), "wb");
  if (f == nullptr)
  {
    log_error("Couldn't open checkpoint output file");
    return true;
  }

  hdr.magic = htonl(file_magic);
  hdr.checksum = 0;
  hdr.num_states = htonl(states.size());
  hdr.num_ileaves = 0;

  if (fwrite(&hdr, sizeof(hdr), 1, f) != 1)
    goto fail;

  for (it = states.begin(); it != states.end(); it++)
  {
    entry.location_number = htonl(it->location_number);
    entry.num_threads = htons(it->num_threads);
    entry.cur_thread = htons(it->cur_thread);

    if (fwrite(&entry, sizeof(entry), 1, f) != 1)
      goto fail;

    assert(it->explored.size() < 65536);
    assert(it->explored.size() == ntohs(entry.num_threads));

    i = 0;
    memset(buffer, 0, sizeof(buffer));
    for (ex_it = it->explored.begin(); ex_it != it->explored.end(); ex_it++)
    {
      if (*ex_it)
      {
        buffer[i >> 3] |= (1 << i & 7);
      }
      i++;
    }

    // Round up
    i += 7;
    i >>= 3;

    assert(i != 0); // Always at least one thread in _existence_.
    if (fwrite(buffer, i, 1, f) != 1)
      goto fail;
  }

  fclose(f);
  return false;

fail:
  log_error("Write error writing checkpoint file");
  fclose(f);
  return true;
}

bool reachability_treet::dfs_position::read_from_file(
  const std::string &&filename)
{
  reachability_treet::dfs_position::file_hdr hdr;
  reachability_treet::dfs_position::file_entry entry;
  FILE *f;
  unsigned int i, j;
  char c;

  f = fopen(filename.c_str(), "rb");
  if (f == nullptr)
  {
    log_error("Couldn't open checkpoint input file");
    return true;
  }

  if (fread(&hdr, sizeof(hdr), 1, f) != 1)
    goto fail;

  if (hdr.magic != htonl(file_magic))
  {
    log_error("Magic number indicates that this isn't a checkpoint file");
    fclose(f);
    return true;
  }

  for (i = 0; i < ntohl(hdr.num_states); i++)
  {
    reachability_treet::dfs_position::dfs_state state;
    if (fread(&entry, sizeof(entry), 1, f) != 1)
      goto fail;

    state.location_number = ntohl(entry.location_number);
    state.num_threads = ntohs(entry.num_threads);
    state.cur_thread = ntohs(entry.cur_thread);

    assert(state.num_threads < 65536);
    if (state.cur_thread >= state.num_threads)
    {
      log_error("Inconsistent checkpoint data");
      fclose(f);
      return true;
    }

    for (j = 0; j < state.num_threads; j++)
    {
      if (j % 8 == 0)
      {
        if (fread(&c, sizeof(c), 1, f) != 1)
          goto fail;
      }

      state.explored.push_back(c & (1 << (j & 7)));
    }

    states.push_back(state);
  }

  fclose(f);
  return false;

fail:
  log_error("Read error on checkpoint file");
  fclose(f);
  return true;
}

void reachability_treet::print_ileave_trace() const
{
  std::list<std::shared_ptr<execution_statet>>::const_iterator it;
  int i = 0;

  log_status("Context switch trace for interleaving:");
  for (it = execution_states.begin(); it != execution_states.end(); it++, i++)
  {
    log_status("Context switch point {}", i);
    (*it)->print_stack_traces(4);
  }
}

bool reachability_treet::check_thread_viable(unsigned int tid, bool quiet) const
{
  const execution_statet &ex = get_cur_state();

  if (ex.DFS_traversed.at(tid) == true)
  {
    if (!quiet)
      log_status("Thread unschedulable as it's already been explored");
    return false;
  }

  if (ex.threads_state.at(tid).call_stack.empty())
  {
    if (!quiet)
      log_status("Thread unschedulable due to empty call stack");
    return false;
  }

  if (ex.threads_state.at(tid).thread_ended)
  {
    if (!quiet)
      log_status("That thread has ended");
    return false;
  }

#if 0
  if (por && !ex.is_thread_mpor_schedulable(tid)) {
    if (!quiet)
      log_status("Thread unschedulable due to POR");
    return false;
  }
#endif

  if (ex.tid_is_set && ex.monitor_tid == tid)
  {
    if (!quiet)
      log_status("Can't context switch to a monitor thread");
    return false;
  }

  return true;
}

goto_symext::symex_resultt reachability_treet::get_next_formula()
{
  assert(execution_states.size() > 0 && "Must setup RT before exploring");

  while (!is_has_complete_formula())
  {
    while ((!get_cur_state().has_cswitch_point_occured() ||
            get_cur_state().check_if_ileaves_blocked()) &&
           get_cur_state().can_execution_continue())
      get_cur_state().symex_step(*this);

    if (state_hashing)
    {
      if (check_for_hash_collision())
      {
        post_hash_collision_cleanup();
        break;
      }

      update_hash_collision_set();
    }

    if (por)
    {
      get_cur_state().calculate_mpor_constraints();
      if (get_cur_state().is_transition_blocked_by_mpor())
        break;
    }

    next_thread_id = decide_ileave_direction(get_cur_state());

    if (
      get_cur_state().interleaving_unviable &&
      next_thread_id != get_cur_state().active_thread)
      break;
    create_next_state();

    switch_to_next_execution_state();
  }

  (*cur_state_it)->add_memory_leak_checks();

  has_complete_formula = false;

  return get_cur_state().get_symex_result();
}

bool reachability_treet::setup_next_formula()
{
  return reset_to_unexplored_state();
}

goto_symext::symex_resultt reachability_treet::generate_schedule_formula()
{
  int total_states = 0;
  while (has_more_states())
  {
    total_states++;
    while ((!get_cur_state().has_cswitch_point_occured() ||
            get_cur_state().check_if_ileaves_blocked()) &&
           get_cur_state().can_execution_continue())
    {
      get_cur_state().symex_step(*this);
    }

    if (state_hashing)
    {
      if (check_for_hash_collision())
      {
        post_hash_collision_cleanup();
        go_next_state();
        continue;
      }

      update_hash_collision_set();
    }

    next_thread_id = decide_ileave_direction(get_cur_state());

    create_next_state();

    go_next_state();
  }

  return goto_symext::symex_resultt(
    schedule_target, schedule_total_claims, schedule_remaining_claims);
}

bool reachability_treet::restore_from_dfs_state(void *)
{
  abort();
#if 0
  std::vector<reachability_treet::dfs_position::dfs_state>::const_iterator it;
  unsigned int i;

  const reachability_treet::dfs_position *foo = (const reachability_treet::dfs_position*)_dfs;
  const reachability_treet::dfs_position &dfs = *foo;
  // Symex repeatedly until context switch points. At each point, verify that it
  // happened where we expected it to, and then switch to the correct thread for
  // the history we've been provided with.
  for (it = dfs.states.begin(), i = 0; it != dfs.states.end(); it++, i++) {

    at_end_of_run = false;

    while (!is_at_end_of_run()) {
      // Restore the DFS exploration space so that when an interleaving occurs
      // we take the option leading to the thread we desire to run. This
      // assumes that the DFS exploration path algorithm never changes.
      // Has to occur here; between generating new threads, ESBMC messes with
      // the dfs state.
      for (unsigned int dfspos = 0; dfspos < get_cur_state().DFS_traversed.size();
           dfspos++)
        get_cur_state().DFS_traversed[dfspos] = true;
      get_cur_state().DFS_traversed[it->cur_thread] = false;

      get_cur_state().symex_step(*this);
    }

    create_next_state();

    get_cur_state().DFS_traversed = it->explored;

    if (get_cur_state().threads_state.size() != it->num_threads)
{
log_error("Unexpected number of threads when reexploring checkpoint");
abort();
}

    switch_to_next_execution_state();

    // check we're on the right thread; except on the last run, where there are
    // no more threads to be run.
    if (i + 1 < dfs.states.size())
      assert(get_cur_state().get_active_state_number() == it->cur_thread);

#  if 0
    if (get_cur_state().get_active_state().source.pc->location_number !=
        it->location_number) {
log_error("Interleave at unexpected location when restoring checkpoint").
abort();
}
#  endif
  }
#endif
  return false;
}

void reachability_treet::save_checkpoint(const std::string &&) const
{
#if 0
  reachability_treet::dfs_position pos(*this);
  if (pos.write_to_file(fname))
    log_error("Couldn't save checkpoint; continuing");
#endif

  abort();
}
