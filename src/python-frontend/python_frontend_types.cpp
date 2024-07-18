#include <python_frontend_types.h>
#include <map>
#include <string>

bool is_builtin_type(const std::string &name)
{
  return (name == "int" || name == "float" || name == "bool" || name == "str");
}

bool is_consensus_type(const std::string &name)
{
  return (name == "uint64" || name == "Epoch" || name == "Gwei");
}

std::map<std::string, std::string> consensus_func_to_type = {
  {"hash", "uint256"}};

bool is_consensus_func(const std::string &name)
{
  return consensus_func_to_type.find(name) != consensus_func_to_type.end();
}

bool is_model_func(const std::string &name)
{
  return (name == "ESBMC_range_next_" || name == "ESBMC_range_has_next_");
}

std::string get_type_from_consensus_func(const std::string &name)
{
  if (!is_consensus_func(name))
    return std::string();

  return consensus_func_to_type[name];
}
