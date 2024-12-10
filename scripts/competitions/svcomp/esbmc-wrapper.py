#!/usr/bin/env python3

import os
import argparse
import shlex
import subprocess
import time
import sys
import resource
from hashlib import sha256
import datetime

# Start time for this script
start_time = time.time()
SVCOMP_EXTRA_VERSION = " svcomp 0"

class Result:
  success = 1
  fail_deref = 2
  fail_memtrack = 3
  fail_free = 4
  fail_reach = 5
  fail_overflow = 6
  err_timeout = 7
  err_memout = 8
  err_unwinding_assertion = 9
  force_fp_mode = 10
  unknown = 11
  fail_memcleanup = 12
  fail_termination = 13
  fail_race = 14

  @staticmethod
  def is_fail(res):
    if res == Result.fail_deref:
      return True
    if res == Result.fail_free:
      return True
    if res == Result.fail_memtrack:
      return True
    if res == Result.fail_overflow:
      return True
    if res == Result.fail_reach:
      return True
    if res == Result.fail_memcleanup:
      return True
    if res == result.fail_termination: # XXX fbrausse: shouldn't "result" be capitalized?
      return True
    if res == Result.fail_race:
      return True
    return False

  @staticmethod
  def is_out(res):
    if res == Result.err_memout:
      return True
    if res == Result.err_timeout:
      return True
    if res == Result.unknown:
      return True
    return False

class Property:
  reach = 1
  memory = 2
  overflow = 3
  termination = 4
  memcleanup = 5
  datarace = 6

def do_exec(cmd_line):

  if args.dry_run:
    exit(0)

  the_args = shlex.split(cmd_line)

  p = subprocess.Popen(the_args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
  (stdout, stderr) = p.communicate()

  return stdout + stderr

# Function to run esbmc
def run(cmd_line):
  print("Verifying with ESBMC")
  print("Command: " + cmd_line)
  out = do_exec(cmd_line)
  print(out.decode())
  return out

def parse_result(the_output, prop):

  # Parse output
  if "Timed out" in the_output:
    return Result.err_timeout

  if "Out of memory" in the_output:
    return Result.err_memout

  if "Chosen solver doesn\'t support floating-point numbers" in the_output:
    return Result.force_fp_mode

  # Error messages:
  memory_leak = "dereference failure: forgotten memory"
  invalid_pointer = "dereference failure: invalid pointer"
  memset_access_oob = "dereference failure: memset of memory segment of size"
  access_out = "dereference failure: Access to object out of bounds"
  dereference_null = "dereference failure: NULL pointer"
  expired_variable = "dereference failure: accessed expired variable pointer"
  invalid_object = "dereference failure: invalidated dynamic object"
  invalid_object_free = "dereference failure: invalidated dynamic object freed"
  invalid_pointer_free = "dereference failure: invalid pointer freed"
  free_error = "dereference failure: free() of non-dynamic memory"
  bounds_violated = "array bounds violated"
  free_offset = "Operand of free must have zero pointer offset"
  data_race = "/W data race on"
  unreachability_intrinsic = "reachability: unreachable code reached"

  if "VERIFICATION FAILED" in the_output:
    if "unwinding assertion loop" in the_output:
      return Result.err_unwinding_assertion

    if prop == Property.memcleanup:
      if memory_leak in the_output:
        return Result.fail_memcleanup

    if prop == Property.termination:
      return Result.fail_termination

    if prop == Property.memory:
      if memory_leak in the_output:
        return Result.fail_memtrack

      if invalid_pointer_free in the_output:
        return Result.fail_free

      if invalid_object_free in the_output:
        return Result.fail_free

      if expired_variable in the_output:
        return Result.fail_deref

      if invalid_pointer in the_output:
        return Result.fail_deref

      if dereference_null in the_output:
        return Result.fail_deref

      if free_error in the_output:
        return Result.fail_free

      if access_out in the_output or memset_access_oob in the_output:
        return Result.fail_deref

      if invalid_object in the_output:
        return Result.fail_deref

      if bounds_violated in the_output:
        return Result.fail_deref

      if free_offset in the_output:
        return Result.fail_free

      if " Verifier error called" in the_output:
        return Result.success

    if prop == Property.overflow:
      return Result.fail_overflow

    if prop == Property.reach:
      if unreachability_intrinsic not in the_output:
        return Result.fail_reach

    if prop == Property.datarace:
      return Result.fail_race

  if "VERIFICATION SUCCESSFUL" in the_output:
    return Result.success

  return Result.unknown

def get_result_string(the_result):
  if the_result == Result.fail_memcleanup:
    return "FALSE_MEMCLEANUP"

  if the_result == Result.fail_memtrack:
    return "FALSE_MEMTRACK"

  if the_result == Result.fail_free:
    return "FALSE_FREE"

  if the_result == Result.fail_deref:
    return "FALSE_DEREF"

  if the_result == Result.fail_overflow:
    return "FALSE_OVERFLOW"

  if the_result == Result.fail_reach:
    return "FALSE_REACH"

  if the_result == Result.fail_termination:
    return "FALSE_TERMINATION"

  if the_result == Result.fail_race:
    return "FALSE_DATARACE"

  if the_result == Result.success:
    return "TRUE"

  if the_result == Result.err_timeout:
    return "Timed out"

  if the_result == Result.err_unwinding_assertion:
    return "Unknown"

  if the_result == Result.err_memout:
    return "Unknown"

  if the_result == Result.unknown:
    return "Unknown"

  exit(0)

# strings
esbmc_path = "./esbmc "

# ESBMC default commands: this is the same for every submission
esbmc_dargs = "--no-div-by-zero-check --force-malloc-success --state-hashing --add-symex-value-sets "
esbmc_dargs += "--no-align-check --k-step 2 --floatbv --unlimited-k-steps "

# <https://github.com/esbmc/esbmc/pull/1190#issuecomment-1637047028>
esbmc_dargs += "--no-vla-size-check "


import re
def check_if_benchmark_contains_pthread(benchmark):
  with open(benchmark, "r") as f:
    for line in f:
      if re.search("pthread_create", line.strip()):
        return True
  return False

def get_command_line(strat, prop, arch, benchmark, concurrency, dargs, esbmc_ci):
  command_line = esbmc_path + dargs

  # Add benchmark
  command_line += benchmark + " "

  # Add arch
  if arch == 32:
    command_line += "--32 "
  else:
    command_line += "--64 "

  concurrency = ((prop in (Property.reach, Property.datarace)) and
                 check_if_benchmark_contains_pthread(benchmark))

  if concurrency:
    command_line += " --context-bound 2 "
    #command_line += "--no-slice " # TODO: Witness validation is only working without slicing

  # Add witness arg
  witness_name = os.path.basename(benchmark) if esbmc_ci else "witness"
  command_line += "--witness-output " + witness_name + ".graphml "

  # Special case for termination, it runs regardless of the strategy
  if prop == Property.termination:
    command_line += "--no-pointer-check --no-bounds-check --no-assertions "
    command_line += "--termination --max-inductive-step 3 "
    return command_line

  if prop == Property.overflow:
    command_line += "--no-pointer-check --no-bounds-check --overflow-check --no-assertions "
  elif prop == Property.memory:
    command_line += "--memory-leak-check --no-reachable-memory-leak --no-assertions "
    # It seems SV-COMP doesn't want to check for memleaks on abort()
    # see also <https://github.com/esbmc/esbmc/issues/1259>
    command_line += "--no-abnormal-memory-leak "
    # many benchmarks assume malloc(0) == NULL and alloca(0) == NULL
    command_line += "--malloc-zero-is-null "
    strat = "incr"
  elif prop == Property.memcleanup:
    command_line += "--no-pointer-check --no-bounds-check --memory-leak-check --no-assertions "
    strat = "incr"
  elif prop == Property.reach:
    command_line += "--enable-unreachability-intrinsic "
    if concurrency:
      command_line += "--no-pointer-check --no-bounds-check "
    else:
      command_line += "--no-pointer-check --interval-analysis --no-bounds-check --error-label ERROR --goto-unwind --unlimited-goto-unwind "
  elif prop == Property.datarace:
    # TODO: can we do better in case 'concurrency == False'?
    command_line += "--no-pointer-check --no-bounds-check --data-races-check --no-assertions "
  else:
    print("Unknown property")
    exit(1)

  # Add strategy
  if concurrency: # Concurrency only works with incremental
    command_line += "--incremental-bmc "
  elif strat == "fixed":
    command_line += "--k-induction --max-inductive-step 3 "
  elif strat == "kinduction":
    command_line += "--k-induction --max-inductive-step 3 "
  elif strat == "falsi":
    command_line += "--falsification "
  elif strat == "incr":
    command_line += "--incremental-bmc "
  else:
    print("Unknown strategy")
    exit(1)

  return command_line

def verify(strat, prop, concurrency, dargs, esbmc_ci):
  # Get command line
  esbmc_command_line = get_command_line(strat, prop, arch, benchmark, concurrency, dargs, esbmc_ci)

  # Call ESBMC
  output = run(esbmc_command_line)

  res = parse_result(output.decode(), category_property)
  # Parse output
  return res

def witness_to_sha256(benchmark, esbmc_ci):
  sha256hash = ''
  with open(benchmark, 'r') as f:
    data = f.read().encode('utf-8')
    sha256hash = sha256(data).hexdigest()
  witness = os.path.basename(benchmark) + ".graphml" if esbmc_ci else "witness.graphml"
  fin = open(witness, "rt")
  data = fin.readlines()
  fin.close()

  fin = open(witness, "wt")
  for line in data:
    if '<data key="programhash">' in line:
      line = line.replace(line[line.index('>')+1:line.index('</data>')], sha256hash)

    if '<data key="creationtime">' in line:
      time = datetime.datetime.now().strftime("%Y-%m-%dT%H:%M:%SZ")
      line = line.replace(line[line.index('>')+1:line.index('</data>')], time)
    fin.write(line)
  fin.close()
  return

# Options
parser = argparse.ArgumentParser()
parser.add_argument("-a", "--arch", help="Either 32 or 64 bits", type=int, choices=[32, 64], default=32)
parser.add_argument("-v", "--version", help="Prints ESBMC's version", action='store_true')
parser.add_argument("-p", "--propertyfile", help="Path to the property file")
parser.add_argument("benchmark", nargs='?', help="Path to the benchmark")
parser.add_argument("-s", "--strategy", help="ESBMC's strategy", choices=["kinduction", "falsi", "incr", "fixed"], default="fixed")
parser.add_argument("-c", "--concurrency", help="Set concurrency flags", action='store_true')
parser.add_argument("-n", "--dry-run", help="do not actually run ESBMC, just print the command", action='store_true')
parser.add_argument("--ci", help="run this wrapper with special options for the CI (internal use)", action='store_true')

args = parser.parse_args()

arch = args.arch
version = args.version
property_file = args.propertyfile
benchmark = args.benchmark
strategy = args.strategy
concurrency = args.concurrency
esbmc_ci = args.ci

if version:
  print(do_exec(esbmc_path + "--version").decode()[6:].strip()),
  exit(0)

if property_file is None:
  print("Please, specify a property file")
  exit(1)

if benchmark is None:
  print("Please, specify a benchmark to verify")
  exit(1)

# Parse property files
f = open(property_file, 'r')
property_file_content = f.read()

category_property = 0
if "CHECK( init(main()), LTL(G valid-free) )" in property_file_content:
  category_property = Property.memory
elif "CHECK( init(main()), LTL(G ! overflow) )" in property_file_content:
  category_property = Property.overflow
elif "CHECK( init(main()), LTL(G ! call(reach_error())) )" in property_file_content:
  category_property = Property.reach
elif "CHECK( init(main()), LTL(F end) )" in property_file_content:
  category_property = Property.termination
elif "CHECK( init(main()), LTL(G valid-memcleanup) )" in property_file_content:
  category_property = Property.memcleanup
elif "CHECK( init(main()), LTL(G ! data-race) )" in property_file_content:
  category_property = Property.datarace
else:
  print("Unsupported Property")
  exit(1)

result = verify(strategy, category_property, concurrency, esbmc_dargs, esbmc_ci)
try:
  witness_to_sha256(benchmark, esbmc_ci)
except:
  pass
print(get_result_string(result))
