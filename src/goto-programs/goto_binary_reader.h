#pragma once

#include <util/context.h>
#include <vector>
#include <string>

class context;
class goto_functionst;

class goto_binary_reader
{
public:
  bool read_goto_binary_array(
    const void *data,
    size_t size,
    contextt &context,
    goto_functionst &dest);

  void set_functions_to_read(const std::vector<std::string> &funcs)
  {
    functions = funcs;
  }

  bool read_goto_binary(
    const std::string &path,
    contextt &context,
    goto_functionst &dest);

private:
  std::vector<std::string> functions; // functions to read
};
