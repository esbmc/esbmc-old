set(Bitwuzla_MIN_VERSION "0.1")

if(DEFINED Bitwuzla_DIR)
    set(ENABLE_BITWUZLA ON)
else()
    if(ENABLE_BITWUZLA AND DOWNLOAD_DEPENDENCIES)
        # See Boolector configurations for more details about this

        CPMAddPackage(
            NAME bitwuzla
            DOWNLOAD_ONLY YES
            GITHUB_REPOSITORY bitwuzla/bitwuzla
            GIT_TAG 0.5.0)
	  
	  message("[bitwuzla] Source-dir: ${bitwuzla_SOURCE_DIR} ")
        message("[bitwuzla] Configuring project:  ./configure.py --prefix ${bitwuzla_BINARY_DIR}")
        execute_process(COMMAND python3 ./configure.py --prefix ${bitwuzla_BINARY_DIR}
          WORKING_DIRECTORY "${bitwuzla_SOURCE_DIR}")

	
        message("[bitwuzla] Building...")
        execute_process(COMMAND meson install
          WORKING_DIRECTORY ${bitwuzla_SOURCE_DIR}/build
	  COMMAND_ECHO STDOUT)

        set(Bitwuzla_DIR ${bitwuzla_BINARY_DIR})
    endif()

endif()

if(EXISTS $ENV{HOME}/bitwuzla)
    set(ENABLE_BITWUZLA ON)
endif()

if(ENABLE_BITWUZLA)
  
  if(Bitwuzla_DIR)
    # We need to tell cmake where to look for the .pc file exported by bitwuzla
    list(APPEND CMAKE_PREFIX_PATH "${Bitwuzla_DIR}")
  endif()

  include(FindPkgConfig)
  pkg_check_modules(Bitwuzla REQUIRED IMPORTED_TARGET bitwuzla)

  if(NOT Bitwuzla_FOUND)
    message(ERROR "Could not find Bitwuzla in '${Bitwuzla_DIR}'")
  endif()

    message(STATUS "Found Bitwuzla at: ${Bitwuzla_PREFIX}")
    message(STATUS "Bitwuzla version: ${Bitwuzla_VERSION}")
    if(Bitwuzla_VERSION VERSION_LESS Bitwuzla_MIN_VERSION)
        message(FATAL_ERROR "Expected version ${Bitwuzla_MIN_VERSION} or greater")
    endif()

    add_library(solverbitw bitwuzla_conv.cpp)
    target_include_directories(solverbitw
            PRIVATE ${Boost_INCLUDE_DIRS}
            PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(solverbitw fmt::fmt PkgConfig::Bitwuzla)

    target_link_libraries(solvers INTERFACE solverbitw)

    set(ESBMC_ENABLE_bitwuzla 1 PARENT_SCOPE)
    set(ESBMC_AVAILABLE_SOLVERS "${ESBMC_AVAILABLE_SOLVERS} bitwuzla" PARENT_SCOPE)
endif()
