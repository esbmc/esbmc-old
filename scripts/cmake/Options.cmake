# Module to list ESBMC Options

#############################
# ABOUT
#############################

#[[
This module sets all default options and variables with default values
to overwrite use the cmake cli, e.g -DENABLE_WERROR=On

Also, you can set some variables which are not defined directly here:
-DCMAKE_BUILD_TYPE which can be Release, Debug, RelWithDebInfo, etc (https://cmake.org/cmake/help/latest/variable/CMAKE_BUILD_TYPE.html)
-G which can be Ninja, Unix Makefile, Visual studio, etc...
]]

#############################
# GENERAL
#############################
option(ENABLE_WERROR "All warnings are treated as errors during compilation (default: OFF)" OFF)
option(BUILD_STATIC "Build ESBMC in static mode (default: OFF)" OFF)
option(BUILD_DOC "Build ESBMC documentation" OFF)
option(ENABLE_REGRESSION "Add Regressions Tests (default: OFF)" OFF)
option(ENABLE_COVERAGE "Generate Coverage Report (default: OFF)" OFF)
option(ENABLE_OLD_FRONTEND "Enable flex/bison language frontend (default: OFF)" OFF)
option(ENABLE_SOLIDITY_FRONTEND "Enable Solidity language frontend (default: OFF)" OFF)
option(ENABLE_GOTO_CONTRACTOR "Enable IBEX in the build (default: OFF)" OFF)
option(ENABLE_JIMPLE_FRONTEND "Enable Jimple language frontend (default: OFF)" OFF)
option(ENABLE_PYTHON_FRONTEND "Enable Python language frontend (default: OFF)" OFF)

#############################
# SOLVERS
#############################
option(ENABLE_SMTLIB "Use SMTLIB interface solver (default: ON)" ON)
option(ENABLE_BOOLECTOR "Use Boolector solver (default: OFF)" OFF)
option(ENABLE_Z3 "Use Z3 solver (default: OFF)" OFF)
option(ENABLE_MATHSAT "Use MathSAT solver (default: OFF)" OFF)
option(ENABLE_YICES "Use Yices solver (default: OFF)" OFF)
option(ENABLE_CVC4 "Use CVC4 solver (default: OFF)" OFF)
option(ENABLE_CVC5 "Use CVC5 solver (default: OFF)" OFF)
option(ENABLE_BITWUZLA "Use Bitwuzla solver (default: OFF)" OFF)

#############################
# OTHERS
#############################
option(ESBMC_BUNDLE_LIBC "Use libc from c2goto (default: ON)" ON)
option(ENABLE_LIBM "Use libm from c2goto (default: ON)" ON)
option(ENABLE_FUZZER "Add fuzzing targets (default: OFF)" OFF)
option(ENABLE_CLANG_TIDY "Activate clang tidy analysis (default: OFF)" OFF)
option(ENABLE_CSMITH "Add csmith Tests (default: OFF) (depends: ENABLE_REGRESSION)" OFF)
option(BENCHBRINGUP "Run a user-specified benchmark in Github workflow" OFF)
option(DOWNLOAD_DEPENDENCIES "Download and build dependencies if needed (default: OFF)" OFF)
option(ACADEMIC_BUILD "Check and Enable libs that available only in Academic builds (default: OFF)" OFF)
option(ESBMC_SVCOMP "Enable an SV-COMP build of ESBMC (default: OFF)" OFF)

#############################
# PRE-BUILT DEPENDENCIES
#############################

# these URLs are all for an x86_64 target
if(WIN32)
  set(DEFAULT_LLVM_URL "https://zenodo.org/records/14704256/files/llvm+clang+lld-16.0.0-x86_64-windows-msvc-release-mt.zip?download=1")
  set(DEFAULT_LLVM_NAME "llvm+clang+lld-16.0.0-x86_64-windows-msvc-release-mt")

  set(DEFAULT_Z3_URL "https://github.com/Z3Prover/z3/releases/download/z3-4.13.3/z3-4.13.3-x64-win.zip")
  set(DEFAULT_Z3_NAME z3-4.13.3-x64-win)

  set(MATHSAT_URL "https://mathsat.fbk.eu/download.php?file=mathsat-5.6.10-win64-msvc.zip")
  set(MATHSAT_NAME "mathsat-5.6.10-win64-msvc")
else()
  set(DEFAULT_LLVM_URL "https://github.com/llvm/llvm-project/releases/download/llvmorg-16.0.0/clang+llvm-16.0.0-x86_64-linux-gnu-ubuntu-18.04.tar.xz")
  set(DEFAULT_LLVM_NAME "clang+llvm-16.0.0-x86_64-linux-gnu-ubuntu-18.04")

  set(DEFAULT_Z3_URL "https://github.com/Z3Prover/z3/releases/download/z3-4.13.3/z3-4.13.3-x64-glibc-2.35.zip")
  set(DEFAULT_Z3_NAME z3-4.13.3-x64-glibc-2.35)

  set(MATHSAT_URL "https://mathsat.fbk.eu/download.php?file=mathsat-5.6.10-linux-x86_64.tar.gz")
  set(MATHSAT_NAME "mathsat-5.6.10-linux-x86_64")

  set(DEFAULT_CVC5_URL "https://github.com/cvc5/cvc5/releases/download/cvc5-1.1.2/cvc5-Linux-static.zip")
  set(DEFAULT_CVC5_NAME cvc5-Linux-static)
endif()

set(ESBMC_LLVM_URL ${DEFAULT_LLVM_URL} CACHE STRING "URL to download prebuilt LLVM")
set(ESBMC_LLVM_NAME ${DEFAULT_LLVM_NAME} CACHE STRING "Name of the extracted directory of LLVM")

set(ESBMC_Z3_URL ${DEFAULT_Z3_URL} CACHE STRING "URL to download prebuilt Z3")
set(ESBMC_Z3_NAME ${DEFAULT_Z3_NAME} CACHE STRING "Name of the extracted directory of Z3")

set(ESBMC_CVC5_URL ${DEFAULT_CVC5_URL} CACHE STRING "URL to download prebuilt CVC")
set(ESBMC_CVC5_NAME ${DEFAULT_CVC5_NAME} CACHE STRING "Name of the extracted directory of CVC")

#############################
# CMake extra Vars
#############################
# ESBMC_CLANG_HEADERS_BUNDLED: 'detect', On, Off
set(ESBMC_CLANG_HEADERS_BUNDLED "detect" CACHE STRING "Bundle the Clang resource-dir headers (default: detect)")
set(OVERRIDE_CLANG_HEADER_DIR "")
set(Clang_DIR "${LLVM_DIR}" CACHE STRING "Clang Directory (if not set, this will be set to the LLVM_DIR")

set(ESBMC_CHERI "" CACHE STRING "override CHERI-support enabled through detection of Clang")
set(ESBMC_CHERI_HYBRID_SYSROOT "" CACHE STRING "Path containing the mips64-unknown-freebsd sysroot typically generated by 'cheribuild.py cheribsd-mips64-hybrid'")
set(ESBMC_CHERI_PURECAP_SYSROOT "" CACHE STRING "Path containing the mips64-unknown-freebsd sysroot typically generated by 'cheribuild.py cheribsd-mips64-purecap'")

# Demand C++17
set (CMAKE_CXX_STANDARD 17)

# Used by try_compile
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
