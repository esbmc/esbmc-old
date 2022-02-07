/*******************************************************************\

Module:

Author: Daniel Kroening, kroening@cs.cmu.edu

\*******************************************************************/

#include <cstring>
#include <langapi/mode.h>

const char *extensions_ansi_c[] = {"c", "i", nullptr};

#ifdef _WIN32
const char *extensions_cpp[] = {"cpp", "cc", "ipp", "cxx", NULL};
#else
const char *extensions_cpp[] = {"cpp", "cc", "ipp", "C", "cxx", nullptr};
#endif

const char *extensions_sol_ast[] = {"solast", nullptr};

int get_mode(const std::string &str)
{
  unsigned i;

  for(i = 0; mode_table[i].name != nullptr; i++)
    if(str == mode_table[i].name)
      return i;

  return -1;
}

int get_mode_filename(const std::string &filename, optionst *options)
{
  const char *ext = strrchr(filename.c_str(), '.');

  if(ext == nullptr)
    return -1;

  std::string extension = ext + 1;

  if(extension == "")
    return -1;

  int mode;
  bool first_match_found = false;
  for(mode = 0; mode_table[mode].name != nullptr; mode++)
  {
    for(unsigned i = 0; mode_table[mode].extensions[i] != nullptr; i++)
    {
      if(mode_table[mode].extensions[i] == extension)
      {
        // We have two frontends for C and C++ respectively: one is clang and the other one is C++.
        // We return mode on the second match using old-frontend.
        if(options->get_bool_option("old-frontend") && !first_match_found)
        {
          first_match_found = true;
          continue;
        }
        return mode;
      }
    }
  }

  return -1;
}

languaget *new_language(const char *mode, const messaget &msg)
{
  return (*mode_table[get_mode(mode)].new_language)(msg);
}
