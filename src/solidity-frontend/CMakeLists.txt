add_library(solidityfrontend_stuff solidity_language.cpp typecast.cpp solidity_convert.cpp solidity_grammar.cpp
  solidity_convert_literals.cpp pattern_check.cpp ../c2goto/library/solidity.c)
target_include_directories(solidityfrontend_stuff
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../c2goto/headers/__esbmc
    PRIVATE ${CLANG_INCLUDE_DIRS}
    PRIVATE ${Boost_INCLUDE_DIRS}
)

target_link_libraries(solidityfrontend_stuff gotoprograms nlohmann_json::nlohmann_json clangcfrontend_stuff)

add_library(solidityfrontend INTERFACE)
target_link_libraries(solidityfrontend INTERFACE solidityfrontend_stuff)
