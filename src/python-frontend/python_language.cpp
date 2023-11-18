#include <python-frontend/python_language.h>
#include <python-frontend/python_converter.h>
#include <python-frontend/python_annotation.h>
#include <util/message.h>
#include <util/filesystem.h>
#include <util/c_expr2string.h>

#include <cstdlib>

#include <boost/filesystem.hpp>
#include <boost/process.hpp>

namespace bp = boost::process;
namespace fs = boost::filesystem;

extern "C"
{
#include <pythonastgen.h> // generated by build system
#undef ESBMC_FLAIL
}

static const std::string &dump_python_script()
{
  // Dump astgen.py into a temporary directory
  static bool dumped = false;
  static auto p =
    file_operations::create_tmp_dir("esbmc-python-astgen-%%%%-%%%%-%%%%");
  if(!dumped)
  {
    dumped = true;
#define ESBMC_FLAIL(body, size, ...)                                           \
  std::ofstream(p.path() + "/" #__VA_ARGS__).write(body, size);

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
  if(!fs::exists(script))
    return true;

  ast_output_dir = dump_python_script();
  const std::string python_script_path = ast_output_dir + "/astgen.py";

  // Execute python script to generate json file from AST
  std::vector<std::string> args = {python_script_path, path, ast_output_dir};

  // Create a child process to execute Python
  bp::child process(bp::search_path("python3"), args);

  // Wait for execution
  process.wait();

  if(process.exit_code())
  {
    log_error("Python execution failed");
    return true;
  }

  std::ifstream ast_json(ast_output_dir + "/ast.json");
  ast = nlohmann::json::parse(ast_json);

  python_annotation<nlohmann::json> ann;
  ann.add_type_annotation(ast["body"]);

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
  return converter.convert();
}

void python_languaget::show_parse(std::ostream &out)
{
  out << "AST:\n";
  out << ast.dump(4) << std::endl;
}

bool python_languaget::from_expr(
  const exprt &expr,
  std::string &code,
  const namespacet &ns)
{
  code = c_expr2string(expr, ns);
  return false;
}

bool python_languaget::from_type(
  const typet &type,
  std::string &code,
  const namespacet &ns)
{
  code = c_type2string(type, ns);
  return false;
}
