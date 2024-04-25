#pragma once

#include <python-frontend/json_utils.h>
#include <python-frontend/python_frontend_types.h>
#include <util/message.h>
#include <string>

template <class Json>
class python_annotation
{
public:
  python_annotation(Json &ast) : ast_(ast)
  {
  }

  void add_type_annotation()
  {
    // Add type annotation in global scope variables
    add_annotation(ast_);

    // Add type annotation in function bodies
    for (Json &element : ast_["body"])
    {
      if (element["_type"] == "FunctionDef")
      {
        add_annotation(element);
        update_end_col_offset(element);
      }
      else if (element["_type"] == "ClassDef")
      {
        for (auto &class_member : element["body"])
        {
          // Process methods
          if (class_member["_type"] == "FunctionDef")
          {
            add_annotation(class_member);
            update_end_col_offset(class_member);
          }
        }
      }
    }
  }

  // Add annotation in a specific function
  void add_type_annotation(const std::string &func_name)
  {
    for (Json &elem : ast_["body"])
    {
      if (elem["_type"] == "FunctionDef" && elem["name"] == func_name)
      {
        add_annotation(elem);
        update_end_col_offset(elem);
        return;
      }
    }
  }

private:
  void update_end_col_offset(Json &ast)
  {
    int max_col_offset = ast["end_col_offset"];
    for (auto &elem : ast["body"])
    {
      if (elem["end_col_offset"] > max_col_offset)
        max_col_offset = elem["end_col_offset"];
    }
    ast["end_col_offset"] = max_col_offset;
  }

  std::string get_type_from_constant(const Json &element)
  {
    if (element.contains("esbmc_type_annotation"))
      return element["esbmc_type_annotation"].template get<std::string>();

    auto rhs = element["value"];

    if (rhs.is_number_integer() || rhs.is_number_unsigned())
      return "int";
    if (rhs.is_number_float())
      return "float";
    if (rhs.is_boolean())
      return "bool";
    if (rhs.is_string())
      return "str";

    return std::string();
  }

  std::string get_type_from_binary_expr(const Json &element, const Json &body)
  {
    std::string type("");

    const Json &lhs =
      element.contains("value") ? element["value"]["left"] : element["left"];

    if (lhs["_type"] == "BinOp")
      type = get_type_from_binary_expr(lhs, body);
    // Floor division (//) operations always result in an integer value
    else if (
      element.contains("value") &&
      element["value"]["op"]["_type"] == "FloorDiv")
      type = "int";
    else
    {
      // If the LHS of the binary operation is a variable, its type is retrieved
      if (lhs["_type"] == "Name")
      {
        Json left_op = find_node(lhs["id"], body["body"]);
        if (!left_op.empty())
          type = left_op["annotation"]["id"];
      }
      else if (lhs["_type"] == "Constant")
        type = get_type_from_constant(lhs);
    }

    return type;
  }

  void add_annotation(Json &body)
  {
    for (auto &element : body["body"])
    {
      if (element["_type"] == "Assign" && element["type_comment"].is_null())
      {
        std::string type;
        // Get type from rhs constant
        if (element["value"]["_type"] == "Constant")
        {
          type = get_type_from_constant(element["value"]);
        }
        // Get type from RHS variable
        else if (element["value"]["_type"] == "Name")
        {
          // Find rhs variable declaration in the current function
          auto rhs_node = find_node(element["value"]["id"], body["body"]);

          // Find rhs variable in the function args
          if (rhs_node.empty() && body.contains("args"))
            rhs_node = find_node(element["value"]["id"], body["args"]["args"]);

          // Find rhs variable node in the global scope
          if (rhs_node.empty())
            rhs_node = find_node(element["value"]["id"], ast_["body"]);

          if (rhs_node.empty())
          {
            log_error(
              "Variable {} not found.",
              element["value"]["id"].template get<std::string>().c_str());
            abort();
          }

          // Get type from RHS type annotation
          type = rhs_node["annotation"]["id"];
        }
        // Get type from RHS binary expression
        else if (element["value"]["_type"] == "BinOp")
        {
          type = get_type_from_binary_expr(element, body);
        }
        // Get type from constructor call
        else if (
          element["value"]["_type"] == "Call" &&
          (json_utils::is_class<Json>(element["value"]["func"]["id"], ast_) ||
           is_builtin_type(element["value"]["func"]["id"]) ||
           is_consensus_type(element["value"]["func"]["id"])))
          type = element["value"]["func"]["id"];
        else
          continue;

        if (type.empty())
        {
          log_status("Type undefined for:\n{}", element.dump(2).c_str());
          abort();
        }

        // Update type field
        element["_type"] = "AnnAssign";

        auto target = element["targets"][0];
        std::string id;
        // Get lhs from simple variables on assignments.
        if (target.contains("id"))
          id = target["id"];
        // Get lhs from members access on assignments. e.g.: x.data = 10
        else if (target["_type"] == "Attribute")
          id = target["value"]["id"].template get<std::string>() + "." +
               target["attr"].template get<std::string>();

        assert(!id.empty());

        int col_offset =
          target["col_offset"].template get<int>() + id.size() + 1;

        // Add annotation field
        element["annotation"] = {
          {"_type", target["_type"]},
          {"col_offset", col_offset},
          {"ctx", {{"_type", "Load"}}},
          {"end_col_offset", col_offset + type.size()},
          {"end_lineno", target["end_lineno"]},
          {"id", type},
          {"lineno", target["lineno"]}};

        element["end_col_offset"] =
          element["end_col_offset"].template get<int>() + type.size() + 1;
        element["end_lineno"] = element["lineno"];
        element["simple"] = 1;

        // Convert "targets" array to "target" object
        element["target"] = target;
        element.erase("targets");

        // Remove "type_comment" field
        element.erase("type_comment");

        // Update value fields
        element["value"]["col_offset"] =
          element["value"]["col_offset"].template get<int>() + type.size() + 1;
        element["value"]["end_col_offset"] =
          element["value"]["end_col_offset"].template get<int>() + type.size() +
          1;

        /* Adjust column offset node on lines involving function
         * calls with arguments */
        if (element["value"].contains("args"))
        {
          for (auto &arg : element["value"]["args"])
          {
            arg["col_offset"] =
              arg["col_offset"].template get<int>() + type.size() + 1;
            arg["end_col_offset"] =
              arg["end_col_offset"].template get<int>() + type.size() + 1;
          }
        }
        // Adjust column offset in function call node
        if (element["value"].contains("func"))
        {
          element["value"]["func"]["col_offset"] =
            element["value"]["func"]["col_offset"].template get<int>() +
            type.size() + 1;
          element["value"]["func"]["end_col_offset"] =
            element["value"]["func"]["end_col_offset"].template get<int>() +
            type.size() + 1;
        }
      }
    }
  }

  const Json find_node(const std::string &node_name, const Json &body)
  {
    for (const Json &elem : body)
    {
      if (
        elem.contains("_type") &&
        ((elem["_type"] == "AnnAssign" && elem.contains("target") &&
          elem["target"].contains("id") &&
          elem["target"]["id"].template get<std::string>() == node_name) ||
         (elem["_type"] == "arg" && elem["arg"] == node_name)))
      {
        return elem;
      }
    }
    return Json();
  }

  Json &ast_;
};
