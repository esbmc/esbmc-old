#include <python-frontend/python_language.h>
#include <python-frontend/python_converter.h>
#include <python-frontend/python_annotation.h>
#include <clang-cpp-frontend/clang_cpp_adjust.h>
#include <util/message.h>
#include <util/filesystem.h>
#include <util/c_expr2string.h>

#include <cstdlib>
#include <fstream>

#include <boost/filesystem.hpp>
#include <boost/process.hpp>

namespace bp = boost::process;
namespace fs = boost::filesystem;

extern "C"
{
#include <pythonastgen.h> // generated by build system
#undef ESBMC_FLAIL
}

// TODO: Rename this function as it is dumping other files now.
static const std::string &dump_python_script()
{
  // Dump all Python (.py) files from src/python-frontend into a temporary directory
  static bool dumped = false;
  static auto p =
    file_operations::create_tmp_dir("esbmc-python-astgen-%%%%-%%%%-%%%%");
  if (!dumped)
  {
    dumped = true;
#define ESBMC_FLAIL(body, size, ...)                                           \
  {                                                                            \
    fs::path filePath(fs::path(p.path()) / #__VA_ARGS__);                      \
    fs::path directory = filePath.parent_path();                               \
    if (!directory.empty() && !fs::exists(directory))                          \
      fs::create_directories(directory);                                       \
    std::ofstream(filePath.string()).write(body, size);                        \
  }

#include <pythonastgen.h>
#undef ESBMC_FLAIL
  }
  return p.path();
}

languaget *new_python_language()
{
  return new python_languaget;
}

bool python_languaget::parse(const std::string &path)
{
  log_debug("python", "Parsing: {}", path);

  fs::path script(path);
  if (!fs::exists(script))
    return true;

  ast_output_dir = dump_python_script();
  const std::string python_script_path = ast_output_dir + "/parser.py";

  // Execute python script to generate json file from AST
  std::vector<std::string> args = {python_script_path, path, ast_output_dir};

  // Create a child process to execute Python
  bp::child process(bp::search_path("python3"), args);

  // Wait for execution
  process.wait();

  if (process.exit_code())
  {
    log_error("Python execution failed");
    return true;
  }

  std::stringstream script_path;
  script_path << ast_output_dir << "/" << script.stem().string() << ".json";

  // Parse and generate AST
  std::ifstream ast_json(script_path.str());
  if (!ast_json.good())
  {
    log_error(
      "<python-parser> {} was not generated\n", script_path.str().c_str());
    exit(1);
  }

  ast = nlohmann::json::parse(ast_json);

  // Add annotation
  python_annotation<nlohmann::json> ann(ast);
  const std::string function = config.options.get_option("function");
  if (!function.empty())
    ann.add_type_annotation(function);
  else
    ann.add_type_annotation();

  return false;
}

bool python_languaget::final(contextt & /*context*/)
{
  return false;
}

bool python_languaget::typecheck(
  contextt &context,
  const std::string & /*module*/)
{
  python_converter converter(context, ast);
  if (converter.convert())
    return true;

  clang_cpp_adjust adjuster(context);
  if (adjuster.adjust())
    return true;

  return false;
}

void python_languaget::show_parse(std::ostream &out)
{
  out << "AST:\n";
  const std::string function = config.options.get_option("function");
  if (function.empty())
  {
    out << ast.dump(4) << std::endl;
    return;
  }
  else
  {
    for (const auto &elem : ast["body"])
    {
      if (elem["_type"] == "FunctionDef" && elem["name"] == function)
      {
        out << elem.dump(4) << std::endl;
        return;
      }
    }
  }
  log_error("Function {} not found.\n", function.c_str());
  abort();
}

bool python_languaget::from_expr(
  const exprt &expr,
  std::string &code,
  const namespacet &ns,
  unsigned flags)
{
  code = c_expr2string(expr, ns, flags);
  return false;
}

bool python_languaget::from_type(
  const typet &type,
  std::string &code,
  const namespacet &ns,
  unsigned flags)
{
  code = c_type2string(type, ns, flags);
  return false;
}

unsigned python_languaget::default_flags(presentationt target) const
{
  unsigned f = 0;
  switch (target)
  {
  case presentationt::HUMAN:
    f |= c_expr2stringt::SHORT_ZERO_COMPOUNDS;
    break;
  case presentationt::WITNESS:
    f |= c_expr2stringt::UNIQUE_FLOAT_REPR;
    break;
  }
  return f;
}
