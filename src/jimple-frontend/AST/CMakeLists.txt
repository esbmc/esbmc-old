add_library(jimple_ast
        jimple_file.cpp
        jimple_modifiers.cpp
        jimple_method.cpp
        jimple_class_field.cpp
        jimple_type.cpp
        jimple_method_body.cpp
        jimple_declaration.cpp
        jimple_statement.cpp
        jimple_expr.cpp
        jimple_globals.cpp)

target_link_libraries(jimple_ast nlohmann_json::nlohmann_json fmt::fmt)
