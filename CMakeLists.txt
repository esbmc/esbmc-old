#################################
# Project Information           #
#################################
cmake_minimum_required (VERSION 3.18)

# HACK: This msvc policy needs to be set before project()
# This can be removed when we move to CMake >= 3.15
cmake_policy(SET CMP0091 NEW)

project (ESBMC)

set (ESBMC_VERSION_MAJOR 7)
set (ESBMC_VERSION_MINOR 8)
set (ESBMC_VERSION_PATCH 0)
set (ESBMC_VERSION "${ESBMC_VERSION_MAJOR}.${ESBMC_VERSION_MINOR}.${ESBMC_VERSION_PATCH}")

# The only default solver available is smtlib
set (ESBMC_AVAILABLE_SOLVERS "smtlib")

set(MAX_SUPPORTED_LLVM_VERSION_MAJOR 19)

#################################
# Configuration                 #
#################################

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Adds custom modules from ESBMC and default Options
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/scripts/cmake/")

# Set a default build type if none was specified
set(default_build_type "RelWithDebInfo")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to '${default_build_type}' as none was specified.")
  set(CMAKE_BUILD_TYPE "${default_build_type}" CACHE
      STRING "Choose the type of build." FORCE)
endif()

# Set the possible values of build type for cmake-gui
set(CMAKE_CXX_FLAGS_DEBUGOPT "-O2 -g" CACHE STRING "C++ compiler flags for DebugOpt build" FORCE)
set(CMAKE_C_FLAGS_DEBUGOPT "-O2 -g" CACHE STRING "C compiler flags for DebugOpt build" FORCE)
#set(CMAKE_EXE_LINKER_FLAGS_DEBUGOPT "")
#set(CMAKE_SHARED_LINKER_FLAGS_DEBUGOPT "")
mark_as_advanced(
  CMAKE_CXX_FLAGS_DEBUGOPT
  CMAKE_C_FLAGS_DEBUGOPT
  #CMAKE_EXE_LINKER_FLAGS_DEBUGOPT
  #CMAKE_SHARED_LINKER_FLAGS_DEBUGOPT
)
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
             "Debug" "Release" "MinSizeRel" "RelWithDebInfo" "Sanitizer" "DebugOpt")

set(ESBMC_CXX_Clang_DEFAULTS -Wall -Wextra -pipe)
set(ESBMC_CXX_GNU_DEFAULTS -Wall -Wextra -pipe)
add_compile_options(${ESBMC_CXX_${CMAKE_CXX_COMPILER_ID}_DEFAULTS})

include(CheckIncludeFile)
check_include_file(unistd.h HAVE_UNISTD)

include(Utils)
include(OSConfiguration)
include(Options)
include(SendFileHack)
include(InstallFiles)
include(Sanitizers)
include(ClangTidy)
include(Coverage)

if(BUILD_STATIC)
  include(BuildStatic)
endif()

# Copied from https://stackoverflow.com/questions/53877344/cannot-configure-cmake-to-look-for-homebrew-installed-version-of-bison
# On macOS, search Homebrew for keg-only versions of Bison and Flex. Xcode does
# not provide new enough versions for us to use.
if (CMAKE_HOST_SYSTEM_NAME MATCHES "Darwin")
    execute_process(
        COMMAND brew --prefix bison
        RESULT_VARIABLE BREW_BISON
        OUTPUT_VARIABLE BREW_BISON_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if (BREW_BISON EQUAL 0 AND EXISTS "${BREW_BISON_PREFIX}")
        message(STATUS "Found Bison keg installed by Homebrew at ${BREW_BISON_PREFIX}")
        set(BISON_EXECUTABLE "${BREW_BISON_PREFIX}/bin/bison")
    endif()

    execute_process(
        COMMAND brew --prefix flex
        RESULT_VARIABLE BREW_FLEX
        OUTPUT_VARIABLE BREW_FLEX_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if (BREW_FLEX EQUAL 0 AND EXISTS "${BREW_FLEX_PREFIX}")
        message(STATUS "Found Flex keg installed by Homebrew at ${BREW_FLEX_PREFIX}")
        set(FLEX_EXECUTABLE "${BREW_FLEX_PREFIX}/bin/flex")
    endif()
endif()



# This MUST be executed after BuildStatic since it sets Boost Static flags
find_package(Boost REQUIRED COMPONENTS filesystem system date_time program_options iostreams)
include(FindLocalLLVM)

include(ExternalDependencies)

# Optimization
include(OptimizationCCache)
include(WError)

if(ENABLE_OLD_FRONTEND)
  find_package(BISON 2.6.1 REQUIRED)
  find_package(FLEX 2.6.1 REQUIRED)
  add_definitions(-DENABLE_OLD_FRONTEND)
endif()

if(ENABLE_SOLIDITY_FRONTEND)
  add_definitions(-DENABLE_SOLIDITY_FRONTEND)
endif()
if(ENABLE_JIMPLE_FRONTEND)
    add_definitions(-DENABLE_JIMPLE_FRONTEND)
endif()

if(ENABLE_PYTHON_FRONTEND)
    add_definitions(-DENABLE_PYTHON_FRONTEND)
endif()

if(ENABLE_GOTO_CONTRACTOR)
  add_compile_options(-DENABLE_GOTO_CONTRACTOR)
endif()

add_subdirectory(src)

include(Irep2Optimization)

# Generate ac_config.h. This must be generated after solvers
configure_file (
 "${PROJECT_SOURCE_DIR}/scripts/cmake/cmake_config.in"
 "${PROJECT_BINARY_DIR}/src/ac_config.h"
)

# This should be added after all source files
include(Docs)

include(CTest)
if(BUILD_TESTING)
    enable_testing()
    include_directories(src)
    add_subdirectory(unit)
endif()
if(ENABLE_REGRESSION)
    add_subdirectory(regression)
endif()
include(FindCsmith)
