#pragma once

#include <python-frontend/json_utils.h>
#include <python-frontend/python_frontend_types.h>
#include <util/message.h>
#include <string>

template <class Json>
class python_annotation
{
public:
  python_annotation(Json &ast) : ast_(ast), current_func(nullptr)
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
        current_func = &element;
        add_annotation(element);
        update_end_col_offset(element);
        current_func = nullptr;
      }
      else if (element["_type"] == "ClassDef")
      {
        for (auto &class_member : element["body"])
        {
          // Process methods
          if (class_member["_type"] == "FunctionDef")
          {
            current_func = &class_member;
            add_annotation(class_member);
            update_end_col_offset(class_member);
            current_func = nullptr;
          }
        }
      }
    }
  }

  // Add annotation in a specific function
  void add_type_annotation(const std::string &func_name)
  {
    // Add type annotation in global scope variables
    add_annotation(ast_);

    for (Json &elem : ast_["body"])
    {
      if (elem["_type"] == "FunctionDef" && elem["name"] == func_name)
      {
        current_func = &elem;
        add_annotation(elem);
        update_end_col_offset(elem);
        current_func = nullptr;
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
        Json left_op = find_annotated_assign(lhs["id"], body["body"]);
        if (!left_op.empty())
          type = left_op["annotation"]["id"];
      }
      else if (lhs["_type"] == "Constant")
        type = get_type_from_constant(lhs);
    }

    return type;
  }

  std::string
  get_function_return_type(const std::string &func_name, const Json &ast)
  {
    for (const Json &elem : ast["body"])
    {
      if (
        elem["_type"] == "FunctionDef" && elem["name"] == func_name &&
        elem.contains("returns") && !elem["returns"].is_null())
      {
        return elem["returns"]["id"];
      }
    }
    return std::string();
  }

  void add_annotation(Json &body)
  {
    for (auto &element : body["body"])
    {
      auto &stmt_type = element["_type"];
      if (stmt_type == "If" || stmt_type == "While")
        add_annotation(element);

      if (stmt_type == "Assign" && element["type_comment"].is_null())
      {
        std::string inferred_type("");

        // Check if LHS was previously annotated
        if (
          element.contains("targets") &&
          element["targets"][0]["_type"] == "Name")
        {
          // Search for LHS annotation in the current scope
          Json node =
            find_annotated_assign(element["targets"][0]["id"], body["body"]);
          if (node == Json() && current_func != nullptr)
            // Fall back to the current function
            node = find_annotated_assign(
              element["targets"][0]["id"], (*current_func)["body"]);
          if (node == Json())
            // Fall back to variables in the global scope
            node =
              find_annotated_assign(element["targets"][0]["id"], ast_["body"]);

          if (node != Json())
            inferred_type = node["annotation"]["id"];
        }

        const auto &value_type = element["value"]["_type"];

        // Get type from RHS constant
        if (value_type == "Constant")
        {
          inferred_type = get_type_from_constant(element["value"]);
        }
        else if (
          value_type == "UnaryOp" && element["value"]["operand"]["_type"] ==
                                       "Constant") // Handle negative numbers
        {
          inferred_type = get_type_from_constant(element["value"]["operand"]);
        }

        // Get type from RHS variable
        else if (value_type == "Name" || value_type == "Subscript")
        {
          std::string rhs_var_name;
          if (value_type == "Name")
            rhs_var_name = element["value"]["id"];
          else if (value_type == "Subscript")
            rhs_var_name = element["value"]["value"]["id"];

          // Find RHS variable declaration in the current scope
          auto rhs_node = find_annotated_assign(rhs_var_name, body["body"]);

          // Find RHS variable declaration in the current function
          if (rhs_node.empty() && current_func)
            rhs_node =
              find_annotated_assign(rhs_var_name, (*current_func)["body"]);

          // Find RHS variable in the function args
          if (rhs_node.empty() && body.contains("args"))
            rhs_node = find_annotated_assign(
              element["value"]["id"], body["args"]["args"]);

          // Find RHS variable node in the global scope
          if (rhs_node.empty())
            rhs_node =
              find_annotated_assign(element["value"]["id"], ast_["body"]);

          if (rhs_node.empty())
          {
            log_error(
              "Variable {} not found.",
              element["value"]["id"].template get<std::string>().c_str());
            abort();
          }

          // Get type from RHS type annotation
          inferred_type = rhs_node["annotation"]["id"];
        }

        // Get type from RHS binary expression
        else if (value_type == "BinOp")
        {
          std::string got_type = get_type_from_binary_expr(element, body);
          if (!got_type.empty())
            inferred_type = got_type;
        }

        // Get type from constructor call
        else if (
          value_type == "Call" &&
          (json_utils::is_class<Json>(element["value"]["func"]["id"], ast_) ||
           is_builtin_type(element["value"]["func"]["id"]) ||
           is_consensus_type(element["value"]["func"]["id"])))
          inferred_type = element["value"]["func"]["id"];

        else if (
          value_type == "Call" &&
          is_consensus_func(element["value"]["func"]["id"]))
          inferred_type =
            get_type_from_consensus_func(element["value"]["func"]["id"]);

        // Get type from function return
        else if (
          value_type == "Call" &&
          !is_model_func(element["value"]["func"]["id"]))
        {
          std::string func_name = element["value"]["func"]["id"];
          inferred_type = get_function_return_type(func_name, ast_);
        }
        else
          continue;

        if (inferred_type.empty())
        {
          log_error("Type undefined for:\n{}", element.dump(2).c_str());
          abort();
        }

        // Update type field
        element["_type"] = "AnnAssign";

        auto target = element["targets"][0];
        std::string id;
        // Get LHS from simple variables on assignments.
        if (target.contains("id"))
          id = target["id"];
        // Get LHS from members access on assignments. e.g.: x.data = 10
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
          {"end_col_offset", col_offset + inferred_type.size()},
          {"end_lineno", target["end_lineno"]},
          {"id", inferred_type},
          {"lineno", target["lineno"]}};

        element["end_col_offset"] =
          element["end_col_offset"].template get<int>() + inferred_type.size() +
          1;
        element["end_lineno"] = element["lineno"];
        element["simple"] = 1;

        // Convert "targets" array to "target" object
        element["target"] = target;
        element.erase("targets");

        // Remove "type_comment" field
        element.erase("type_comment");

        // Update value fields
        element["value"]["col_offset"] =
          element["value"]["col_offset"].template get<int>() +
          inferred_type.size() + 1;
        element["value"]["end_col_offset"] =
          element["value"]["end_col_offset"].template get<int>() +
          inferred_type.size() + 1;

        /* Adjust column offset node on lines involving function
         * calls with arguments */
        if (element["value"].contains("args"))
        {
          for (auto &arg : element["value"]["args"])
          {
            arg["col_offset"] =
              arg["col_offset"].template get<int>() + inferred_type.size() + 1;
            arg["end_col_offset"] = arg["end_col_offset"].template get<int>() +
                                    inferred_type.size() + 1;
          }
        }
        // Adjust column offset in function call node
        if (element["value"].contains("func"))
        {
          element["value"]["func"]["col_offset"] =
            element["value"]["func"]["col_offset"].template get<int>() +
            inferred_type.size() + 1;
          element["value"]["func"]["end_col_offset"] =
            element["value"]["func"]["end_col_offset"].template get<int>() +
            inferred_type.size() + 1;
        }
      }
    }
  }

  const Json
  find_annotated_assign(const std::string &node_name, const Json &body) const
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
  Json *current_func;
};
