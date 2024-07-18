#pragma once

#include <string>
#include <fstream>

namespace json_utils
{
template <typename JsonType>
JsonType find_class(const JsonType &ast_json, const std::string &class_name)
{
  auto it =
    std::find_if(ast_json.begin(), ast_json.end(), [&](const JsonType &obj) {
      return obj.contains("_type") && obj["_type"] == "ClassDef" &&
             obj["name"] == class_name;
    });

  return (it != ast_json.end()) ? *it : JsonType();
}

template <typename JsonType>
bool is_class(const std::string &name, const JsonType &ast_json)
{
  // Find class definition in the current json
  if (find_class(ast_json["body"], name) != JsonType())
    return true;

  // Find class definition in imported modules
  for (const auto &obj : ast_json["body"])
  {
    auto is_imported_class = [&ast_json,
                              &name](const std::string &module_name) {
      std::stringstream module_path;
      module_path << ast_json["ast_output_dir"].template get<std::string>()
                  << "/" << module_name << ".json";
      std::ifstream imported_file(module_path.str());
      if (!imported_file.is_open())
        return false;

      JsonType imported_module_json;
      imported_file >> imported_module_json;

      if (is_class(name, imported_module_json))
        return true;

      return false;
    };
    if (obj["_type"] == "ImportFrom")
      return is_imported_class(obj["module"].template get<std::string>());

    if (obj["_type"] == "Import")
    {
      for (const auto &imported : obj["names"])
      {
        if (is_imported_class(imported["name"].template get<std::string>()))
          return true;
      }
    }
  }

  return false;
}

template <typename JsonType>
bool is_module(const std::string &module_name, const JsonType &ast)
{
  std::stringstream file_path;
  file_path << ast["ast_output_dir"].template get<std::string>() << "/"
            << module_name << ".json";
  std::ifstream file(file_path.str());
  return file.is_open();
}

template <typename JsonType>
JsonType find_function(const JsonType &json, const std::string &func_name)
{
  for (const auto &elem : json)
  {
    if (elem["_type"] == "FunctionDef" && elem["name"] == func_name)
      return elem;
  }
  return JsonType();
}

} // namespace json_utils
