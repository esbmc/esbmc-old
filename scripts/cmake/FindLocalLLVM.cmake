# Module to find LLVM and checks it's version

if(DOWNLOAD_DEPENDENCIES AND ("${LLVM_DIR}" STREQUAL ""))
    download_zip_and_extract(LLVM ${ESBMC_LLVM_URL})
    set(LLVM_DIR ${CMAKE_BINARY_DIR}/LLVM/${ESBMC_LLVM_NAME})
    set(Clang_DIR ${CMAKE_BINARY_DIR}/LLVM/${ESBMC_LLVM_NAME})    
endif()

if(NOT (("${LLVM_DIR}" STREQUAL "LLVM_DIR-NOTFOUND") OR ("${LLVM_DIR}" STREQUAL "")))
  message("Looking for LLVM in: ${LLVM_DIR}")
  find_package(LLVM REQUIRED CONFIG
    PATHS ${LLVM_DIR}
    NO_DEFAULT_PATH
  )

  find_package(Clang REQUIRED CONFIG
    PATHS ${Clang_DIR}
    NO_DEFAULT_PATH
  )
else()  
  find_package(LLVM REQUIRED CONFIG)
  find_package(Clang REQUIRED CONFIG)
endif()

if (LLVM_PACKAGE_BUGREPORT STREQUAL https://github.com/CTSRD-CHERI/llvm-project/issues)
  set(ESBMC_CHERI_CLANG ON)
  message(STATUS "Clang is CHERI-enabled, enabling CHERI support.")
elseif(EXISTS "${CLANG_INSTALL_PREFIX}/include/llvm/Support/Morello.h")
  set(ESBMC_CHERI_CLANG ON)
  set(ESBMC_CHERI_CLANG_MORELLO ON)
  message(STATUS "Clang is CHERI-enabled, enabling CHERI support for Morello.")
else()
  unset(ESBMC_CHERI_CLANG)
  message(STATUS "Clang is not CHERI-enabled, disabling CHERI support.")
endif()

if (ESBMC_CHERI AND NOT ESBMC_CHERI_CLANG)
  message(FATAL_ERROR "ESBMC_CHERI enabled, but Clang is does not support CHERI")
elseif (DEFINED ESBMC_CHERI AND NOT ESBMC_CHERI AND ESBMC_CHERI_CLANG)
  message(WARNING "ESBMC_CHERI disabled, but Clang may generate CHERI-related ASTs. This build configuration is not supported! You're on your own.")
  unset(ESBMC_CHERI_CLANG)
  unset(ESBMC_CHERI_CLANG_MORELLO)
endif()

if (${LLVM_VERSION_MAJOR} LESS 11)
  message(FATAL_ERROR "Could not find LLVM/Clang >= 11.0 at all: please specify with -DLLVM_DIR/-DClang_DIR")
else()
  message(STATUS "LLVM version: ${LLVM_VERSION}")
endif()

message(STATUS "Clang found in: ${CLANG_INSTALL_PREFIX}")
if (CLANG_LINK_CLANG_DYLIB AND NOT BUILD_STATIC)
  set(ESBMC_CLANG_LIBS clang-cpp LLVM)
  set(CLANG_HEADERS_SHOULD_BUNDLE FALSE)
  message(STATUS "Linking libclang: shared")
else()
  set(ESBMC_CLANG_LIBS clangTooling clangAST clangIndex)
  set(CLANG_HEADERS_SHOULD_BUNDLE TRUE)
  message(STATUS "Linking libclang: static")
endif()

# LLVM upstream decided not to export the variable CLANG_RESOURCE_DIR for the
# installed tree, since "running 'clang -print-resource-dir' is good enough":
# <https://reviews.llvm.org/D49486>
# Here, we are not running clang, since we only require access to the
# resources used by the library and do not necessarily want to depend on the
# clang executable, which in, e.g., Ubuntu, lives in a package separate from
# libclang-cpp.so.

try_run(TRY_CLANG_RUNS TRY_CLANG_COMPILES ${CMAKE_CURRENT_BINARY_DIR}
        ${PROJECT_SOURCE_DIR}/scripts/cmake/try_clang.cc
        CMAKE_FLAGS -DINCLUDE_DIRECTORIES=${CLANG_INCLUDE_DIRS}
        LINK_LIBRARIES ${ESBMC_CLANG_LIBS}
        COMPILE_OUTPUT_VARIABLE TRY_CLANG_COMPILE_OUTPUT
        RUN_OUTPUT_VARIABLE CLANG_RESOURCE_DIR)

if (NOT TRY_CLANG_COMPILES)
  message(FATAL_ERROR "Cannot compile against Clang: ${TRY_CLANG_COMPILE_OUTPUT}")
endif()

if (TRY_CLANG_RUNS EQUAL 0)
  string(STRIP "${CLANG_RESOURCE_DIR}" CLANG_RESOURCE_DIR)
  # see clang-toolings's injectResourceDir():
  # <https://clang.llvm.org/doxygen/Tooling_8cpp_source.html#l00462>
  if (NOT ("${CLANG_RESOURCE_DIR}" STREQUAL ""))
    set(CLANG_RESOURCE_DIR "${CLANG_INSTALL_PREFIX}/bin/${CLANG_RESOURCE_DIR}")
  elseif (${LLVM_VERSION_MAJOR} LESS 16)
    set(CLANG_RESOURCE_DIR "${CLANG_INSTALL_PREFIX}/lib${LLVM_LIBDIR_SUFFIX}/clang/${LLVM_VERSION_MAJOR}.${LLVM_VERSION_MINOR}.${LLVM_VERSION_PATCH}")
  else()
    set(CLANG_RESOURCE_DIR "${CLANG_INSTALL_PREFIX}/lib${LLVM_LIBDIR_SUFFIX}/clang/${LLVM_VERSION_MAJOR}")
  endif()
  message(STATUS "Clang resource dir: ${CLANG_RESOURCE_DIR}")
  set(ESBMC_CLANG_HEADER_DIR "${CLANG_RESOURCE_DIR}/include")
  if (NOT IS_DIRECTORY "${ESBMC_CLANG_HEADER_DIR}")
    message(STATUS "Failed to determine path to Clang headers: not a directory: ${ESBMC_CLANG_HEADER_DIR}")
    unset(ESBMC_CLANG_HEADER_DIR)
  endif()
endif()

if (NOT ${OVERRIDE_CLANG_HEADER_DIR} STREQUAL "")
  set(ESBMC_CLANG_HEADER_DIR ${OVERRIDE_CLANG_HEADER_DIR})
endif()

if (${ESBMC_CLANG_HEADERS_BUNDLED} STREQUAL "detect")
  set(ESBMC_CLANG_HEADERS_BUNDLED ${CLANG_HEADERS_SHOULD_BUNDLE})
endif()

if (ESBMC_CLANG_HEADERS_BUNDLED AND NOT CLANG_HEADERS_SHOULD_BUNDLE)
  message(WARNING "Bundling headers can lead to inconsistencies when libclang is updated independently from ESBMC")
elseif(NOT ESBMC_CLANG_HEADERS_BUNDLED AND CLANG_HEADERS_SHOULD_BUNDLE)
  message(WARNING "Not bundling headers can lead to inconsistencies when they are updated independently from ESBMC")
endif()

if (NOT ESBMC_CLANG_HEADER_DIR)
  message(FATAL_ERROR "Cannot find path to Clang headers, please specify it using -DOVERRIDE_CLANG_HEADER_DIR=<path>")
endif()

if (ESBMC_CLANG_HEADERS_BUNDLED)
  message(STATUS "Using bundled header files from: ${ESBMC_CLANG_HEADER_DIR}")
else()
  message(STATUS "Hard-coding path to clang's header files: ${ESBMC_CLANG_HEADER_DIR}")
endif()
