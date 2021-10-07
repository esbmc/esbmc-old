/*******************************************************************\

Module: Solidity AST module

\*******************************************************************/

// Remove warnings from Clang headers
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <clang/Frontend/ASTUnit.h>
#pragma GCC diagnostic pop

#include <solidity-frontend/solidity_language.h>
#include <solidity-frontend/solidity_convert.h>
#include <clang-c-frontend/clang_c_main.h>
#include <clang-c-frontend/clang_c_adjust.h>
#include <clang-c-frontend/clang_c_convert.h>
#include <c2goto/cprover_library.h>
#include <clang-c-frontend/expr2c.h>
#include <util/c_link.h>

languaget *new_solidity_language(const messaget &msg)
{
  return new solidity_languaget(msg);
}

std::string solidity_languaget::get_temp_file(const messaget &msg)
{
  // Create a temp file for clang-tool
  // needed to convert intrinsics
  auto p = boost::filesystem::temp_directory_path();
  if(!boost::filesystem::exists(p) || !boost::filesystem::is_directory(p))
  {
    msg.error("Can't find temporary directory (needed to dump clang headers)");
    abort();
  }

  // Create temporary directory
  p += "esbmc_solidity_tmp.c";
  boost::filesystem::ofstream(p.string());

  if(!boost::filesystem::is_regular_file(p))
  {
    msg.error(
      "Can't create temporary C file for clang-tool to complete the compilation job for intrinsics");
    abort();
  }

  // populate temp file
  std::ofstream f;
  f.open (p.string());
  f << temp_c_file();
  f.close();

  return p.string();
}

solidity_languaget::solidity_languaget(const messaget &msg)
  : clang_c_languaget(msg)
{
}

bool solidity_languaget::parse(
  const std::string &path,
  const messaget &msg)
{
  // prepare temp file
  temp_path = get_temp_file(msg);

  // get AST nodes of ESBMC intrinsics and the dummy main
  // populate ASTs inherited from parent class
  clang_c_languaget::parse(temp_path, msg);

  // delete after use
  boost::filesystem::remove(temp_path);
  assert(!boost::filesystem::is_regular_file(temp_path));

  // Process AST json file
  std::ifstream ast_json_file_stream(path);
  std::string new_line, ast_json_content;

  msg.debug("### ast_json_file_stream processing:... \n");
  while (getline(ast_json_file_stream, new_line))
  {
    if (new_line.find(".sol =======") != std::string::npos)
    {
      msg.debug("found .sol ====== , breaking ...\n");
      break;
    }
  }
  while (getline(ast_json_file_stream, new_line))
  {
    // file pointer continues from "=== *.sol ==="
    if (new_line.find(".sol =======") == std::string::npos)
    {
      ast_json_content = ast_json_content + new_line + "\n";
    }
    else
    {
      assert(!"Unsupported feature: found multiple contracts defined in a single .sol file");
    }
  }

  // parse explicitly
  ast_json = nlohmann::json::parse(ast_json_content);

  return false;
}

bool solidity_languaget::convert_intrinsics(contextt &context, const messaget &msg)
{
  clang_c_convertert converter(context, ASTs, msg);
  if(converter.convert())
    return true;
  return false;
}

bool solidity_languaget::typecheck(
  contextt &context,
  const std::string &module,
  const messaget &msg)
{
  contextt new_context(msg);
  convert_intrinsics(new_context, msg); // Add ESBMC and TACAS intrinsic symbols to the context
  msg.progress("Done conversion of intrinsics...");

  solidity_convertert converter(new_context, ast_json, sol_func_path, msg);
  if(converter.convert()) // Add Solidity symbols to the context
    return true;

  clang_c_adjust adjuster(new_context, msg);
  if(adjuster.adjust())
    return true;

  if(c_link(context, new_context, msg, module)) // also populates language_uit::context
    return true;

  return false;
}

void solidity_languaget::show_parse(std::ostream &)
{
  assert(!"come back and continue - solidity_languaget::show_parse");
}

bool solidity_languaget::final(
  contextt &context,
  const messaget &msg)
{
  add_cprover_library(context, msg);
  return clang_main(context, msg);
  //assert(!"come back and continue - solidity_languaget::final");
  return false;
}

std::string solidity_languaget::temp_c_file()
{
  std::string content =
    R"(int main() { return 0; } )";
  return content;
}
