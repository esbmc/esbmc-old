# Adds tests as regression
include(CTest)
enable_testing()

find_package(Python)
# TODO: Use add dependency

if(ENABLE_PYTHON_FRONTEND)
# Install Jira package with pip before running tests (needed by regression/python/try-fail)
execute_process(
    COMMAND ${Python_EXECUTABLE} -m pip install jira
)
endif()

set(ESBMC_REGRESSION_TOOL "${CMAKE_CURRENT_SOURCE_DIR}/testing_tool.py")

if(BENCHBRINGUP)
  # To run a specific benchmark in Github workflow and download the log archive when completed
  #   - BENCHMARK_TO_RUN: user-specified benchmark to run
  #   - LOG_DIR: log directory to be archived
  #   - TEST_TIMEOUT: timeout for each test case in a benchmark
  if(NOT DEFINED ENV{BENCHMARK_TO_RUN} OR
     NOT DEFINED ENV{LOG_DIR} OR
     NOT DEFINED ENV{TEST_TIMEOUT})
   message( FATAL_ERROR "Please make sure ENV vars `BENCHMARK_TO_RUN`, `LOG_DIR` and `TEST_TIMEOUT` are defined" )
  endif()

  message(STATUS "Benchmark to run: $ENV{BENCHMARK_TO_RUN} with timeout $ENV{TEST_TIMEOUT}")
  set(BENCHBRINGUP_ARGS --timeout=$ENV{TEST_TIMEOUT} --benchbringup)
endif()

# Let's do CMake "maps"
set(map_extensions_.c "C")
set(map_extensions_.i "C")
set(map_extensions_.cpp "C++")
set(map_extensions_.cu "CUDA")
set(map_extensions_.solast "Solidity")
set(map_extensions_.jimple "Jimple")
set(map_extensions_.goto "GOTO")
set(map_extensions_.py "Python")

set(map_flags_--z3 "Z3")
set(map_flags_--mathsat "MathSAT")
set(map_flags_--cvc "CVC")
set(map_flags_--boolector "Boolector")
set(map_flags_--bitwuzla "Bitwuzla")
set(map_flags_--incremental-bmc "Incremental-BMC")
set(map_flags_--k-induction "k-Induction")
set(map_flags_--k-induction-parallel "k-Induction")
set(map_flags_--gcse "Optimization")
set(map_flags_--interval-analysis "Optimization")

function(add_test_tags test_desc_path test_name folder)
  # Note: I tried using python for this. It made the configuration take minutes.
  # Not sure if this slowness is due to my python skills or from using stdout as a communication
  # layer. With CMake it, takes 5s.

  set(TAGS "regression;${folder}")  
  file(STRINGS ${test_desc_path} TEST_DESC)
  list(GET TEST_DESC 0 MODE)
  list(GET TEST_DESC 1 FILE_NAME)
  list(GET TEST_DESC 2 ARGUMENTS)

  list(APPEND TAGS ${MODE})

  # BUG: For some reason, CMake considers whitespace and multiple "." as part of the extension
  # this means that ".c " and ".magic.c" might be returned. This /might/ be solved in the latest
  # versions (starting with 3.20, CMake has the cmake_path command)
  get_filename_component(FILE_EXT ${FILE_NAME} EXT)
  list(APPEND TAGS ${map_extensions_${FILE_EXT}})
  foreach(arg ${ARGUMENTS})
    list(APPEND TAGS ${map_flags_${arg}})
  endforeach()
  set_tests_properties(${test_name}
    PROPERTIES LABELS "${TAGS}")
endfunction()

function(add_esbmc_regression_test folder modes test)
    set(test_name "regression/${folder}/${test}")
    add_test(NAME ${test_name}
             COMMAND ${Python_EXECUTABLE} ${ESBMC_REGRESSION_TOOL}
                     --tool=${ESBMC_BIN}
                     --regression=${CMAKE_CURRENT_SOURCE_DIR}/${folder}
                     --modes ${modes}
                     --file=${test}
                     ${BENCHBRINGUP_ARGS}) # additional args for BENCHBRINGUP workflow

    add_test_tags(${CMAKE_CURRENT_SOURCE_DIR}/${folder}/${test}/test.desc ${test_name} ${folder})
  
    set_tests_properties(${test_name}
      PROPERTIES SKIP_RETURN_CODE 10)
endfunction()

function(add_esbmc_regression folder modes)
    SUBDIRLIST(SUBDIRS ${CMAKE_CURRENT_SOURCE_DIR}/${regression})
    foreach(test ${SUBDIRS})
        add_esbmc_regression_test(${folder} "${modes}" ${test})
    endforeach()
endfunction()

# conditionally enable regression suites based on configured support
if(ENABLE_BITWUZLA)
    set(REGRESSIONS_BITWUZLA bitwuzla)
endif()
if(ENABLE_CVC4)
    set(REGRESSIONS_CVC cvc)
endif()
if(ENABLE_MATHSAT)
    set(REGRESSIONS_MATHSAT mathsat)
endif()
if(ENABLE_SMTLIB)
    set(REGRESSIONS_SMTLIB smtlib)
endif()
if(ENABLE_Z3)
    set(REGRESSIONS_Z3 z3)
endif()
if(ENABLE_SOLIDITY_FRONTEND)
    set(REGRESSIONS_SOLIDITY esbmc-solidity)
endif()
if(ENABLE_GOTO_CONTRACTOR)
    set(REGRESSIONS_GOTO_CONTRACTOR Interval-analysis-ibex-contractor)
endif()
if(ENABLE_OLD_FRONTEND)
    set(REGRESSIONS_OLD_FRONTEND esbmc-old)
endif()
if(ENABLE_JIMPLE_FRONTEND)
    set(REGRESSIONS_JIMPLE jimple)
endif()
if(ENABLE_PYTHON_FRONTEND)
    set(REGRESSIONS_PYTHON python)
endif()

if(ESBMC_CHERI_CLANG)
    set(REGRESSIONS_CHERI cheri-c
                          cheri-128
       )
endif()

# list of C++98/03 test suites
set(REGRESSIONS_CPP03 esbmc-cpp/cpp
                      esbmc-cpp/cbmc
                      esbmc-cpp/destructors
                      esbmc-cpp/polymorphism_bringup
                      esbmc-cpp/polymorphism_bringup_overload
                      esbmc-cpp/inheritance_bringup
                      esbmc-cpp/bug_fixes
                      esbmc-cpp/OM_sanity_checks
                      esbmc-cpp/string
                      esbmc-cpp/stream
                      esbmc-cpp/gcc-template-tests
                      esbmc-cpp/template
                      esbmc-cpp/algorithm
                      esbmc-cpp/deque
                      esbmc-cpp/list
                      esbmc-cpp/map
                      esbmc-cpp/multimap
                      esbmc-cpp/multiset
                      esbmc-cpp/priority_queue
                      esbmc-cpp/queue
                      esbmc-cpp/set
                      esbmc-cpp/stack
                      esbmc-cpp/unix
                      esbmc-cpp/vector
                      esbmc-cpp/try_catch
                      )
# list of C++11 test suites
set(REGRESSIONS_CPP11 esbmc-cpp11/cpp
                      esbmc-cpp11/constructors
                      esbmc-cpp11/new-delete
                      esbmc-cpp11/reference
                      esbmc-cpp11/template
                      esbmc-cpp11/tuple
                      esbmc-cpp11/array
                      esbmc-cpp11/lambda
                      )

# list of C++14 test suites
set(REGRESSIONS_CPP14 esbmc-cpp14/cpp
                      esbmc-cpp14/deduced-return-type
                      esbmc-cpp14/lambda
                      esbmc-cpp14/template
                      )

# list of C++17 test suites
set(REGRESSIONS_CPP17 esbmc-cpp17/lambda)

# list of C++20 test suites
set(REGRESSIONS_CPP20 esbmc-cpp20/cpp)

# NOTE: In order to make the best of the concurrency set sort the tests from the slowest to fastest.
if(APPLE)
    set(REGRESSIONS esbmc-unix
                    esbmc-unix2
                    esbmc
                    ${REGRESSIONS_SOLIDITY}
                    cbmc
                    cstd
                    llvm
                    floats
                    floats-regression
                    ${REGRESSIONS_JIMPLE}
                    k-induction
                    k-induction-parallel
                    nonz3
                    ${REGRESSIONS_BITWUZLA}
                    ${REGRESSIONS_CVC}
                    ${REGRESSIONS_MATHSAT}
                    ${REGRESSIONS_Z3}
                    incremental-smt
                    esbmc-cpp11/cpp
                    esbmc-cpp11/cbmc-constructors
                    extensions
                    ${REGRESSIONS_CHERI}
                    ${REGRESSIONS_PYTHON}
                    ltl
       )
elseif(WIN32)
    # FUTURE: Add floats-regression esbmc-cpp/cpp
    set(REGRESSIONS esbmc
                    cbmc
                    cstd
                    llvm
                    floats
                    k-induction
                    windows
                    ${REGRESSIONS_JIMPLE}
                    ${REGRESSIONS_BITWUZLA}
                    ${REGRESSIONS_CVC}
                    ${REGRESSIONS_MATHSAT}
                    ${REGRESSIONS_SMTLIB}
                    ${REGRESSIONS_Z3}
                    ltl
       )
else()
  if(NOT BENCHBRINGUP)
    set(REGRESSIONS esbmc-unix
                    esbmc-unix2
                    esbmc
                    ${REGRESSIONS_SOLIDITY}
                    ${REGRESSIONS_OLD_FRONTEND}
                    cbmc
                    cstd
                    llvm
                    floats
                    floats-regression
                    k-induction
                    csmith
                    k-induction-parallel
                    cuda/benchmarks
                    cuda/Supported_long_time
                    nonz3
                    ${REGRESSIONS_BITWUZLA}
                    ${REGRESSIONS_CVC}
                    ${REGRESSIONS_MATHSAT}
                    ${REGRESSIONS_SMTLIB}
                    ${REGRESSIONS_Z3}
                    incremental-smt
                    ${REGRESSIONS_CPP20}
                    ${REGRESSIONS_CPP17}
                    ${REGRESSIONS_CPP14}
                    ${REGRESSIONS_CPP11}
                    ${REGRESSIONS_CPP03}
                    ${REGRESSIONS_GOTO_CONTRACTOR}
                    ${REGRESSIONS_JIMPLE}
                    extensions
                    cuda/COM_sanity_checks
                    linux
                    ${REGRESSIONS_CHERI}
                    ${REGRESSIONS_PYTHON}
                    ltl
                    goto-coverage
       )
  else()
    set(REGRESSIONS $ENV{BENCHMARK_TO_RUN}) # run a single user-specified benchmark
    # create a directory at config time. Test logs will be saved in this directory.
    set(OUT_DIR $ENV{LOG_DIR})
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${OUT_DIR})
  endif()
endif()

foreach(regression IN LISTS REGRESSIONS)
    if(WIN32 OR APPLE) # FUTURE: configure suites using an option
        set(MODES CORE KNOWNBUG FUTURE)
    else()
        set(MODES CORE KNOWNBUG FUTURE THOROUGH)
    endif()
    add_esbmc_regression("${regression}" "${MODES}")
endforeach()


