add_library(algorithms algorithms.cpp)
target_include_directories(algorithms
    PRIVATE ${Boost_INCLUDE_DIRS}
    )

add_library(cache cache.cpp)
target_link_libraries(cache algorithms)

add_library(filesystem filesystem.cpp)
target_include_directories(filesystem
    PRIVATE ${Boost_INCLUDE_DIRS}
    )
target_link_libraries(filesystem PUBLIC ${Boost_LIBRARIES})

add_library(crypto_hash crypto_hash.cpp)
target_include_directories(crypto_hash PUBLIC ${Boost_INCLUDE_DIRS})
target_link_libraries(crypto_hash PUBLIC ${Boost_LIBRARIES})

add_library(util_esbmc xml_irep.cpp xml.cpp
        arith_tools.cpp base_type.cpp cmdline.cpp config.cpp context.cpp
        expr_util.cpp i2string.cpp location.cpp
        mp_arith.cpp namespace.cpp parseoptions.cpp rename.cpp
        threeval.cpp typecheck.cpp bitvector.cpp parser.cpp replace_symbol.cpp
        string_container.cpp options.cpp c_misc.cpp
        simplify_expr.cpp dstring.cpp simplify_expr2.cpp
        simplify_utils.cpp string2array.cpp array2string.cpp time_stopping.cpp symbol.cpp
        symbol_generator.cpp
        type_eq.cpp guard.cpp array_name.cpp union_find.cpp
        std_types.cpp std_code.cpp format_constant.cpp
        irep_serialization.cpp symbol_serialization.cpp fixedbv.cpp
        signal_catcher.cpp migrate.cpp show_symbol_table.cpp
        type_byte_size.cpp goto_expr_factory.cpp
        string_constant.cpp c_types.cpp ieee_float.cpp c_qualifiers.cpp
        c_sizeof.cpp c_link.cpp c_typecast.cpp fix_symbol.cpp destructor.cpp
        c_expr2string.cpp cpp_expr2string.cpp type2name.cpp
        message.cpp encoding.cpp
        )
# Boost is needed by anything that touches irep2
target_include_directories(util_esbmc
        PUBLIC ${CMAKE_BINARY_DIR}/src
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
        PUBLIC ${Boost_INCLUDE_DIRS}
        )

target_link_libraries(util_esbmc PUBLIC irep2 fmt::fmt ${Boost_LIBRARIES})

target_link_libraries(algorithms gotoprograms)
