/*******************************************************************\

Module:

Author: Lucas Cordeiro, lcc08r@ecs.soton.ac.uk

\*******************************************************************/

#ifndef REACHABILITY_TREE_H_
#define REACHABILITY_TREE_H_

#include <deque>
#include <goto-programs/goto_program.h>
#include <goto-symex/execution_state.h>
#include <goto-symex/goto_symex.h>
#include <goto-symex/renaming.h>
#include <goto-symex/symex_target_equation.h>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <util/crypto_hash.h>
#include <util/message.h>
#include <util/options.h>

/**
 *  Class to explore states reachable through threading.
 *  Runs an execution_statet that explores code containing threading functions,
 *  and when notified of context-switch generating operations, attempts to
 *  interleave threads in all possible ways.
 *
 *  To do this, the primary piece of information stored is a stack of
 *  execution_statets, with each ex_state representing a context switch point
 *  reached. A vector<bool> in each ex_state represents which context switches
 *  from this path have been explored already.
 *
 *  The algorithm is to run until the program completes, then feed the trace
 *  to the caller. From then on, when asked to generate a new trace we:
 *
 *    -# Remove the final ex_state from the stack
 *    -# Move to the new final ex_state on the stack
 *    -# Inspect whether we've explored all switches from this state.
 *      If yes, goto 1
 *    -# Pick a context switch to take from the current state, mark as explored
 *    -# Duplicate the final ex_state onto the end of the stack
 *    -# Move to the new end of stack; make the ex_state take the context
 *      switch we picked.
 *    -# Continue symbolic execution from here. Fin.
 *
 *  There are various scheduling possiblities. The default is depth-first
 *  search, where we just follow the algorithm above and return all the traces
 *  to the caller. The "schedule" way combines all paths into one trace, which
 *  is then solved once. Round-robin switches to the next thread in the set of
 *  threads when a context switch point occurs.
 *
 *  Some kind of scheduling interface/api would be good for the future.
 */

class reachability_treet
{
public:
  /**
   *  Default constructor.
   *  Requires a list of functions, and the namespace/context that we'll be
   *  working it, as well as the list of options to work with.
   *  The symex_targett pointer exists to allow the creator of this RT to
   *  feed a subclass of symex_targett into the RT, performing some additional
   *  actions than just collecting assignments/etc.
   *  @param goto_functions GOTO functions to operate over. Must contain main.
   *  @param ns Namespace to operate in
   *  @param target Target to listen in on assigns/asserts/assumes. Is cloned.
   *  @param context Context to operate in.
   *  @param message_handler Message object for symex errors/warnings/info
   */
  reachability_treet(
    goto_functionst &goto_functions,
    const namespacet &ns,
    optionst &opts,
    std::shared_ptr<symex_targett> target,
    contextt &context,
    message_handlert &message_handler);

  /**
   *  Default destructor.
   */
  virtual ~reachability_treet() = default;

  /** Reinitialize for making new exploration of given functions.
   *  Sets up the flags and fields of the object to start a new exploration of
   *  the goto functions we're operating over. To be called when the previous
   *  exploration using this object has been completed. */
  void setup_for_new_explore();

  /**
   *  Return current execution_statet being explored / symex'd.
   *  @return Current execution_statet being explored.
   */
  execution_statet &get_cur_state();
  const execution_statet &get_cur_state() const;

  /**
   *  Walks back to an unexplored context switch.
   *  Follows the algorithm described in reachability_treet, and walk back up
   *  the stack of current execution_states to find a context-switch that
   *  hasn't yet been explored.
   *  @return True if there are more states to be explored
   */
  bool reset_to_unexplored_state();

  /**
   *  Are there more execution_statet s to explore.
   *  @return True if there are more execution_statet s to explore
   */
  bool has_more_states();

  /**
   *  Permitted number of context switches to take.
   *  Set with --context-bound <integer> on the command line. Paths where more
   *  than this many context switches occur will not be explored.
   */
  int get_CS_bound() const;

  /**
   *  Ask user for context switch to take.
   *  Enabled with --interactive-ileaves. Prints out a list of current thread
   *  states, their stack traces and the current instruction being executed.
   *  Then ask the user what thread to switch to; giving feedback and asking
   *  again if that switch is blocked somehow.
   *  @return Thread ID user desires us to switch to
   */
  int get_ileave_direction_from_user() const;

  /**
   *  Decide context switch from --round-robin.
   *  Called when --round-robin scheduling is picked. Decides which context
   *  switch to take on that basis.
   *  @return Thread ID to switch to according to scheduling
   */
  int get_ileave_direction_from_scheduling() const;

  /**
   *  Determine if a thread can be run.
   *  Checks that the thread hasn't ended, has an empty stack, is blocked by
   *  POR and so forth etc. Potentially prints a comment as to why the thread
   *  is blocked, for user feedback from get_ileave_direction_from_user
   *  @param tid Thread ID to switch to
   *  @param quiet If false, will print to stdout why this thread is blocked
   *  @return True if thread is viable; false otherwise.
   */
  bool check_thread_viable(unsigned int tid, bool quiet) const;

  /**
   *  Check whether current ex_state is a state hash collision.
   *  @return True if this state has already been visited
   */
  bool check_for_hash_collision() const;

  /**
   *  Perform various pieces of accounting after a hash collision - primarily,
   *  ensuring that no further paths from this cswitch are explored.
   */
  void post_hash_collision_cleanup();

  /**
   *  Update seen state hashes to contain current state.
   */
  void update_hash_collision_set();

  /**
   *  Perform context switch operation triggered elsewhere.
   *  The analyse_* functions make a decision on whether or not to take a
   *  context switch, but defer the actual taking of this switch until later,
   *  to prevent switching with inconsistent state. This method causes that
   *  context switch, which has been decided upon, to actually be taken.
   *  As referred to in the reachability_treet algorithm, this makes up steps
   *  four and five.
   */
  void create_next_state();

  /**
   *  Force a context switch, and take it.
   *  This causes an analyse_* routine to be called, followed by
   *  create_new_state. The upshot of this is that if there is a context switch
   *  that could be taken, we find and take it. This implements steps 3-7 of
   *  the reachability_treet algorithm.
   *  @return True if context switch was generated and taken
   */
  bool step_next_state();

  /**
   *  Pick a context switch to take.
   *  Determines which thread to switch to now, according to whatever
   *  scheduling method/option is enabled. Called internally by various
   *  analysis routines.
   *  @param ex_state Execution state to analyse for switch direction
   *  @return Thread ID of what thread to switch to next.
   */
  unsigned int decide_ileave_direction(execution_statet &ex_state);

  /**
   *  Prints state of execution_statet stack.
   *  Primarily for debugging; takes the current stack of execution_statet s
   *  and prints a stack trace from the thread executing where the context
   *  switch was caused in each state. Gives you a good idea of how the current
   *  interleaving of ex_state shas been reached.
   */
  void print_ileave_trace() const;

  /**
   *  Have we generated a full program trace.
   *  @return True if all threads have run to completion
   */
  bool is_has_complete_formula();

  /**
   *  Duplicate of step_next_state.
   *  Essentially does the same thing as step_next_state, but is specific to
   *  the --schedule option. This can probably be removed in the future.
   */
  void go_next_state();

  /**
   *  Switch into just-generated execution state.
   *  Run after a context switch has just been generated, switches current
   *  state to the newest one. Optionally generates more states if we were
   *  already on the last one (this may be un-needed).
   */
  void switch_to_next_execution_state();

  // Interface for bmc operation goes here

  /**
   *  Run threads to generate new trace.
   *  Explores a new thread interleaving and returns its trace.
   *  @return A symex_resultt recording the trace that we just generated.
   */
  std::shared_ptr<goto_symext::symex_resultt> get_next_formula();

  /**
   *  Run threads in --schedule manner.
   *  Run all threads to explore all interleavings, and encode it into a single
   *  trace.
   *  @return Symex result representing all interleavings
   */
  std::shared_ptr<goto_symext::symex_resultt> generate_schedule_formula();

  /**
   *  Reset ex_state stack to unexplored state.
   *  This is just a wrapper around reset_to_unexplored_state
   *  @return True if there is another state to be explored
   */
  bool setup_next_formula();

  /**
   *  Class recording a reachability checkpoint.
   *  Currently likely broken; but this originally redorced a particular trace
   *  of the reachability that could written to file, then restored, then
   *  re-reached through symbolic execution. Not going to document it until I
   *  know that it works.
   */
  class dfs_position
  {
  public:
    dfs_position(const reachability_treet &rt);
    dfs_position(const std::string &&filename);
    bool write_to_file(const std::string &&filename) const;

  protected:
    bool read_from_file(const std::string &&filename);

  public:
    struct dfs_state
    {
      unsigned int location_number;
      unsigned int num_threads;
      unsigned int cur_thread;
      std::vector<bool> explored;
    };

    static const uint32_t file_magic;

    struct file_hdr
    {
      uint32_t magic;
      uint32_t checksum;
      uint32_t num_states;
      uint32_t num_ileaves;
    };

    struct file_entry
    {
      uint32_t location_number;
      uint16_t num_threads;
      uint16_t cur_thread;
      // Followed by bitfield for threads explored state.
    };

    std::vector<struct dfs_state> states;

    // Number of interleavings explored to date.
    unsigned int ileaves;

    // We need to be able to detect when the source files have changed somehow,
    // leading to the checkpoint being invalid. So add a checksum field. Exactly
    // how it's going to be calculated, I don't know yet.
    uint64_t checksum;
  };

  /**
   *  Restore RT state to a reachability point.
   *  Currently likely broken.
   *  @param dfs State to restore
   *  @return Dummy
   */
  bool restore_from_dfs_state(void *dfs);

  /**
   *  Save RT reachability state to file.
   *  @param fname Name of file to save to.
   */
  void save_checkpoint(const std::string &&fname) const;

  /** GOTO functions we're operating over. */
  goto_functionst &goto_functions;
  /** Context we're operating upon */
  contextt &permanent_context;
  /** Flag indicating we've executed all threads to exhaustion.
   *  That is; for this particular interleaving. There may still be other
   *  interleavings to explore */
  bool has_complete_formula;
  /** State hashing is enabled */
  bool state_hashing;
  /** Functions dictate interleavings; perform no exploration.
   *  Used by --directed-interleavings */
  bool directed_interleavings;
  /** Namespace we're operating in */
  const namespacet &ns;
  /** Options that are enabled */
  optionst &options;
  /* Have we enabled the deadlock-check?*/
  bool deadlock_check;

protected:
  /** Stack of execution states representing current interleaving.
   *  See reachability_treet algorithm for how this is used. Is initialized
   *  with a single execution_statet in it, with a function call to "main" set
   *  up to be explored. During exploration has various numbers of ex_states
   *  contained in the list. At end of exploration, contains zero.
   *  @see print_ileave_trace
   */
  std::list<std::shared_ptr<execution_statet>> execution_states;
  /** Iterator recording the execution_statet in stack we're operating on */
  std::list<std::shared_ptr<execution_statet>>::iterator cur_state_it;
  /** "Global" symex target for output from --schedule exploration */
  std::shared_ptr<symex_targett> schedule_target;
  /** Target template; from which all targets are cloned.
   *  This allows for the use of a non-concrete target class throughout
   *  exploration */
  std::shared_ptr<symex_targett> target_template;
  /** Limit on context switches; -1 for no limit */
  int CS_bound;
  /** Limit on timeslices (--round-robin) */
  int TS_slice;
  /** Number of claims in current --schedule exploration */
  unsigned int schedule_total_claims;
  /** Number of remaining claims in current --schedule exploration */
  unsigned int schedule_remaining_claims;
  /** Next thread ID to switch to, decided by analyse_* routines */
  unsigned int next_thread_id;
  /** Whether partial-order-reduction is enabled */
  bool por;
  /** Set of state hashes we've discovered */
  std::set<crypto_hash> hit_hashes;
  /** Message handler reference. */
  message_handlert &message_handler;
  /** Flag as to whether we're picking interleaving directions explicitly.
   *  Corresponds to the --interactive-ileaves option. */
  bool interactive_ileaves;
  /** Flag as to whether we're scheduling using round-robin. */
  bool round_robin;
  /** Are we using the --schedule scheduling method? */
  bool schedule;

  /* Map to store the expression and thread ID,
   * which that expression belongs to. */
  std::unordered_map<expr2tc, std::list<unsigned int>, irep2_hash> vars_map;
  /* associative container that contains global writes in */
  std::unordered_set<expr2tc, irep2_hash> is_global;

  friend class execution_statet;
  friend void build_goto_symex_classes();
};

#endif /* REACHABILITY_TREE_H_ */
