add_library(symex symex_target.cpp symex_target_equation.cpp symex_assign.cpp
  symex_main.cpp  symex_stack.cpp goto_trace.cpp build_goto_trace.cpp
  symex_function.cpp goto_symex_state.cpp symex_dereference.cpp symex_goto.cpp
  builtin_functions.cpp slice.cpp symex_other.cpp xml_goto_trace.cpp
  symex_valid_object.cpp dynamic_allocation.cpp symex_catch.cpp renaming.cpp
  execution_state.cpp reachability_tree.cpp reachability_tree_cin.cpp
  witnesses.cpp printf_formatter.cpp features.cpp)
target_include_directories(symex
    PRIVATE ${CMAKE_BINARY_DIR}/src
    PRIVATE ${Boost_INCLUDE_DIRS}
)
target_link_libraries(symex fmt::fmt)

if(WIN32)
  target_link_libraries(symex fmt::fmt wsock32 ws2_32)
endif()
