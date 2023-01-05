#################################
# Project Information           #
#################################
cmake_minimum_required (VERSION 3.9)
project (ESBMC)

set (ESBMC_VERSION_MAJOR 7)
set (ESBMC_VERSION_MINOR 0)
set (ESBMC_VERSION_PATCH 0)
set (ESBMC_VERSION "${ESBMC_VERSION_MAJOR}.${ESBMC_VERSION_MINOR}.${ESBMC_VERSION_PATCH}")

# The only default solver available is smtlib
set (ESBMC_AVAILABLE_SOLVERS "smtlib")

#################################
# Configuration                 #
#################################

# Adds custom modules from ESBMC and default Options
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/scripts/cmake/")

# Set a default build type if none was specified
set(default_build_type "RelWithDebInfo")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to '${default_build_type}' as none was specified.")
  set(CMAKE_BUILD_TYPE "${default_build_type}" CACHE
      STRING "Choose the type of build." FORCE)
  # Set the possible values of build type for cmake-gui
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    "Debug" "Release" "MinSizeRel" "RelWithDebInfo" "Sanitizer")
endif()

if(CMAKE_COMPILER_IS_GNUCXX)
  add_compile_options(-Wall -Wextra)
endif()

include(CheckIncludeFile)
check_include_file(unistd.h HAVE_UNISTD)

include(Utils)
include(OSConfiguration)
include(Options)
include(SendFileHack)
include(AppleConfiguration)
include(InstallFiles)
include(Sanitizers)
include(ClangTidy)
include(Coverage)

if(BUILD_STATIC)
  include(BuildStatic)
endif()

include(ExternalDependencies)

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

find_package(BISON 2.6.1 REQUIRED)
find_package(FLEX 2.6.1 REQUIRED)

# This MUST be executed after BuildStatic since it sets Boost Static flags
find_package(Boost REQUIRED COMPONENTS filesystem system date_time program_options iostreams)
include(FindLocalLLVM)

# Optimization
include(OptimizationCCache)
# Enable LTO
include(LtoOptimization)
include(WError)

if(ENABLE_OLD_FRONTEND)
  add_definitions(-DENABLE_OLD_FRONTEND)
endif()

if(ENABLE_SOLIDITY_FRONTEND)
  add_definitions(-DENABLE_SOLIDITY_FRONTEND)
endif()
if(ENABLE_JIMPLE_FRONTEND)
    add_definitions(-DENABLE_JIMPLE_FRONTEND)
endif()

if(ENABLE_GOTO_CONTRACTOR)
  add_compile_options(-DENABLE_GOTO_CONTRACTOR)
endif()

add_subdirectory(src)

# Precompile irep2 headers
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
