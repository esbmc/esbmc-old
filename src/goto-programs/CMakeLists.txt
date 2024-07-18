if(ENABLE_PYTHON_FRONTEND)
  add_definitions(-DPYTHON_FRONTEND)
endif()

add_library(gotoprograms goto_convert.cpp goto_function.cpp goto_main.cpp
  goto_sideeffects.cpp goto_program.cpp goto_check.cpp goto_inline.cpp
  remove_no_op.cpp goto_convert_functions.cpp remove_unreachable.cpp
  builtin_functions.cpp show_claims.cpp destructor.cpp set_claims.cpp
  add_race_assertions.cpp rw_set.cpp goto_binary_reader.cpp static_analysis.cpp
  goto_program_serialization.cpp goto_function_serialization.cpp
  read_bin_goto_object.cpp goto_program_irep.cpp format_strings.cpp
  loop_numbers.cpp goto_loops.cpp write_goto_binary.cpp
  goto_k_induction.cpp loopst.cpp goto_coverage.cpp goto_cfg.cpp)
add_library(gotoalgorithms loop_unroll.cpp mark_decl_as_non_det.cpp)

if(ENABLE_GOTO_CONTRACTOR)
    include(FindIbex)
    add_library(gotocontractor goto_contractor.cpp)
    target_include_directories(gotocontractor PUBLIC ${IBEX_INCLUDE_DIRS} ${IBEX_INCLUDE_DIRS}/ibex ${IBEX_INCLUDE_DIRS}/ibex/3rd)
    target_link_libraries(gotocontractor PUBLIC algorithms gotoprograms PkgConfig::IBEX)
endif()

add_subdirectory(abstract-interpretation)
target_link_libraries(gotoalgorithms algorithms gotoprograms abstract-interpretation)
target_include_directories(gotoprograms
    PRIVATE ${Boost_INCLUDE_DIRS}
)
target_include_directories(gotoalgorithms
    PRIVATE ${Boost_INCLUDE_DIRS}
)

target_link_libraries(gotoprograms pointeranalysis bigint)