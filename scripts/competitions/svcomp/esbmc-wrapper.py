#!/usr/bin/env python3

import os
import argparse
import shlex
import subprocess
import time
import sys
import resource

# Start time for this script
start_time = time.time()

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
    if res == result.fail_termination:
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

# Function to run esbmc
def run(cmd_line):
  print("Verifying with ESBMC")
  print("Command: " + cmd_line)

  the_args = shlex.split(cmd_line)

  p = subprocess.Popen(the_args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True)

  stdout = []

  while True:
    output = p.stdout.readline()
    if output == '' and p.poll() is not None:
      break
    if output:
      print(output.strip())
      stdout.append(output.strip())
    rc = p.poll()

  return "\n".join(stdout)

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
  access_out = "dereference failure: Access to object out of bounds"
  dereference_null = "dereference failure: NULL pointer"
  expired_variable = "dereference failure: accessed expired variable pointer"
  invalid_object = "dereference failure: invalidated dynamic object"
  invalid_object_free = "dereference failure: invalidated dynamic object freed"
  invalid_pointer_free = "dereference failure: invalid pointer freed"
  free_error = "dereference failure: free() of non-dynamic memory"
  bounds_violated = "array bounds violated"
  free_offset = "Operand of free must have zero pointer offset"

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

      if access_out in the_output:
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
      return Result.fail_reach

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
esbmc_dargs = "--no-div-by-zero-check --force-malloc-success --state-hashing "
esbmc_dargs += "--no-align-check --k-step 2 --floatbv --unlimited-k-steps "
esbmc_dargs += "--context-bound 2 "

def get_command_line(strat, prop, arch, benchmark, fp_mode):
  command_line = esbmc_path + esbmc_dargs

  # Add benchmark
  command_line += benchmark + " "

  # Add arch
  if arch == 32:
    command_line += "--32 "
  else:
    command_line += "--64 "

  # Add witness arg
  command_line += "--witness-output " + os.path.basename(benchmark) + ".graphml "

  # Special case for termination, it runs regardless of the strategy
  if prop == Property.termination:
    command_line += "--no-pointer-check --no-bounds-check --no-assertions "
    command_line += "--termination --max-inductive-step 3 "
    return command_line

  # Add strategy
  if strat == "fixed":
    command_line += "--k-induction-parallel --max-inductive-step 3 "
  elif strat == "kinduction":
    command_line += "--k-induction-parallel --max-inductive-step 3 "
  elif strat == "falsi":
    command_line += "--falsification "
  elif strat == "incr":
    command_line += "--incremental-bmc "
  else:
    print("Unknown strategy")
    exit(1)

  if prop == Property.overflow:
    command_line += "--no-pointer-check --no-bounds-check --overflow-check --no-assertions "
  elif prop == Property.memory:
    command_line += "--memory-leak-check --no-assertions "
  elif prop == Property.memcleanup:
    command_line += "--memory-leak-check --no-assertions "
  elif prop == Property.reach:
    command_line += "--no-pointer-check --no-bounds-check --interval-analysis "
  else:
    print("Unknown property")
    exit(1)

  # if we're running in FP mode, use MathSAT
  if fp_mode:
    command_line += "--mathsat "

  return command_line

def verify(strat, prop, fp_mode):
  # Get command line
  esbmc_command_line = get_command_line(strat, prop, arch, benchmark, fp_mode)

  # Call ESBMC
  output = run(esbmc_command_line)

  res = parse_result(output, category_property)
  if(res == Result.force_fp_mode):
    return verify(strat, prop, True)

  # Parse output
  return res

# Options
parser = argparse.ArgumentParser()
parser.add_argument("-a", "--arch", help="Either 32 or 64 bits", type=int, choices=[32, 64], default=32)
parser.add_argument("-v", "--version", help="Prints ESBMC's version", action='store_true')
parser.add_argument("-p", "--propertyfile", help="Path to the property file")
parser.add_argument("benchmark", nargs='?', help="Path to the benchmark")
parser.add_argument("-s", "--strategy", help="ESBMC's strategy", choices=["kinduction", "falsi", "incr", "fixed"], default="fixed")

args = parser.parse_args()

arch = args.arch
version = args.version
property_file = args.propertyfile
benchmark = args.benchmark
strategy = args.strategy

if version:
  print(os.popen(esbmc_path + "--version").read()[6:],)
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
elif "CHECK( init(main()), LTL(G ! call(__VERIFIER_error())) )" in property_file_content:
  category_property = Property.reach
elif "CHECK( init(main()), LTL(F end) )" in property_file_content:
  category_property = Property.termination
else:
  print("Unsupported Property")
  exit(1)

result = verify(strategy, category_property, False)
print(get_result_string(result))
