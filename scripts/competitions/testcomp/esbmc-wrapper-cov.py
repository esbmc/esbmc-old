#!/usr/bin/env python3

import os
import argparse
import shlex
import subprocess
import time
import sys
import resource
import re

# Start time for this script
start_time = time.time()


def createTestDir():
    """
    1. Check if "test-suite/" folder exists.
        - If it exists, clear its contents.
        - If it doesn't, create it.
    2. Move all *.xml files into this folder using os.rename
    """
    # Define the folder name
    test_dir = 'test-suite'
    
    # Check if the folder already exists
    if os.path.exists(test_dir):
        # If it exists, clear its contents (delete all files inside)
        for file_name in os.listdir(test_dir):
            file_path = os.path.join(test_dir, file_name)
            if os.path.isfile(file_path):
                os.remove(file_path)
    else:
        # If it doesn't exist, create the folder
        os.makedirs(test_dir)
    
    # Get the current working directory
    current_dir = os.getcwd()
    
    # Loop through all files in the current directory
    for file_name in os.listdir(current_dir):
        # Check if the file has an .xml extension
        if file_name.endswith('.xml'):
            # Create full file path
            full_file_name = os.path.join(current_dir, file_name)
            new_location = os.path.join(test_dir, file_name)
            
            # Move the file by renaming its path
            os.rename(full_file_name, new_location)

class Result:
    success = 1
    fail_reach = 2
    err_timeout = 3
    err_memout = 4
    force_fp_mode = 5
    unknown = 6

    @staticmethod
    def is_fail(res):
        if res == Result.fail_reach:
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
    coverage = 1
    reach = 2

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


    if "VERIFICATION FAILED" in the_output:
        if "unwinding assertion loop" in the_output:
            return Result.err_unwinding_assertion

        if prop == Property.coverage:
            return Result.fail_reach

        if prop == Property.reach:
            return Result.fail_reach

    if "VERIFICATION SUCCESSFUL" in the_output:
        return Result.success

    return Result.unknown


def get_result_string(the_result):
  """
    ??Do we need a special output for coverage?
  """
  if the_result == Result.fail_reach:
    return "FALSE_REACH"

  if the_result == Result.success:
    return "TRUE"

  if the_result == Result.err_timeout:
    return "Timed out"

  if the_result == Result.err_memout:
    return "Unknown"

  if the_result == Result.unknown:
    return "Unknown"

  exit(0)


# strings
esbmc_path = "./esbmc "

# ESBMC default commands: this is the same for every submission
esbmc_dargs = "--no-div-by-zero-check --force-malloc-success --state-hashing --add-symex-value-sets "
esbmc_dargs += "--no-align-check --k-step 3 --floatbv --unlimited-k-steps "

# <https://github.com/esbmc/esbmc/pull/1190#issuecomment-1637047028>
esbmc_dargs += "--no-vla-size-check "

import re
def check_if_benchmark_contains_pthread(benchmark):
  with open(benchmark, "r") as f:
    for line in f:
      if re.search("pthread_create", line.strip()):
        return True
  return False


def get_command_line(strat, prop, arch, benchmark, concurrency, dargs, coverage):
  command_line = esbmc_path + dargs

  # Add benchmark
  command_line += benchmark + " "

  # Add arch
  if arch == 32:
    command_line += "--32 "
  else:
    command_line += "--64 "

  concurrency = ((prop in (Property.reach, Property.coverage)) and
                 check_if_benchmark_contains_pthread(benchmark))

  if concurrency:
    command_line += " --no-por --context-bound 2 "
    #command_line += "--no-slice " # TODO: Witness validation is only working without slicing

  # Special case for termination, it runs regardless of the strategy
  if prop == Property.coverage:
    command_line += "--no-bounds-check --no-pointer-check --quiet --no-standard-checks "
    command_line += "-Wno-incompatible-pointer-types -Wno-int-conversion "
    command_line += "--generate-testcase "
    if coverage == "branch":
      command_line += "--base-k-step 2 --branch-coverage "
    elif coverage == "condition":
      command_line += "--base-k-step 3 --branch-coverage " # "--condition-coverage-rm --no-cov-asserts "
  elif prop == Property.reach:
    command_line += "--base-k-step 5 --enable-unreachability-intrinsic "
    command_line += "--generate-testcase "
    if concurrency:
      command_line += "--no-pointer-check --no-bounds-check "
    else:
      command_line += "--no-pointer-check --interval-analysis --no-bounds-check --error-label ERROR --goto-unwind --unlimited-goto-unwind "
  else:
    print("Unknown property")
    exit(1)

  # Add strategy
  if concurrency: # Concurrency only works with incremental
    command_line += "--incremental-bmc "
  elif strat == "fixed":
    command_line += "--k-induction --max-inductive-step 8 "
  elif strat == "kinduction":
    command_line += "--k-induction --max-inductive-step 8 "
  elif strat == "falsi":
    command_line += "--falsification "
  elif strat == "incr":
    command_line += "--incremental-bmc "
  else:
    print("Unknown strategy")
    exit(1)  

  return command_line


def verify(strat, prop, concurrency, dargs, coverage):
  # Get command line
  esbmc_command_line = get_command_line(strat, prop, arch, benchmark, concurrency, dargs, coverage)

  # Call ESBMC
  output = run(esbmc_command_line)

  res = parse_result(output.decode(), category_property)
  # Parse output
  return res

# Options
parser = argparse.ArgumentParser()
parser.add_argument("-a", "--arch", help="Either 32 or 64 bits", type=int, choices=[32, 64], default=32)
parser.add_argument("-v", "--version", help="Prints ESBMC's version", action='store_true')
parser.add_argument("-p", "--propertyfile", help="Path to the property file")
parser.add_argument("benchmark", nargs='?', help="Path to the benchmark")
parser.add_argument("-s", "--strategy", help="ESBMC's strategy", choices=["kinduction", "falsi", "incr", "fixed"], default="fixed")
parser.add_argument("-c", "--concurrency", help="Set concurrency flags", action='store_true')
parser.add_argument("-n", "--dry-run", help="do not actually run ESBMC, just print the command", action='store_true')
parser.add_argument("-o","--coverage", help="run in coverage mode",  choices=["branch", "condition"])

args = parser.parse_args()

arch = args.arch
version = args.version
property_file = args.propertyfile
benchmark = args.benchmark
strategy = args.strategy
concurrency = args.concurrency
coverage = args.coverage

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


'''
In test-comp, it seems to have properties for 
- branch coverage
- condition coverage
- statement coverage
However, it only measures branch coverage
'''
category_property = 0
if "COVER( init(main()), FQL(COVER EDGES(@DECISIONEDGE)) )" in property_file_content:
    category_property = Property.coverage
elif "COVER( init(main()), FQL(COVER EDGES(@CONDITIONEDGE)) )" in property_file_content:
    category_property = Property.coverage
elif "COVER( init(main()), FQL(COVER EDGES(@BASICBLOCKENTRY)) )" in property_file_content:
    category_property = Property.coverage
elif "COVER( init(main()), FQL(COVER EDGES(@CALL(__VERIFIER_error))) )" in property_file_content:
    category_property = Property.reach
elif "CHECK( init(main()), LTL(G ! call(__VERIFIER_error())) )" in property_file_content:
    category_property = Property.reach
elif "COVER( init(main()), FQL(COVER EDGES(@CALL(reach_error))) )" in property_file_content:
    category_property = Property.reach
else:
    print ("Unsupported Property")
    exit(1)

result = verify(strategy, category_property, concurrency, esbmc_dargs, coverage)
print (get_result_string(result))

createTestDir()

"""
Usage:
  # run
  ./esbmc-wrapper-cov.py -s kinduction -p ./properties/coverage-branches.prp test.c

  # output
  test-suite/
  
  # validate
  testcov --no-isolation --test-suite ./test-suite.zip test.c
"""