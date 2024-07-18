#include <goto-programs/goto_function_serialization.h>
#include <goto-programs/goto_program_irep.h>
#include <goto-programs/read_bin_goto_object.h>
#include <langapi/mode.h>
#include <util/base_type.h>
#include <util/irep_serialization.h>
#include <util/namespace.h>
#include <util/symbol_serialization.h>

#define BINARY_VERSION 1

bool read_bin_goto_object(
  std::istream &in,
  const std::string &filename,
  contextt &context,
  std::vector<std::string> &functions,
  goto_functionst &goto_functions)
{
  std::ostringstream str;

  {
    char hdr[4];
    hdr[0] = in.get();
    hdr[1] = in.get();
    hdr[2] = in.get();

    if (hdr[0] != 'G' || hdr[1] != 'B' || hdr[2] != 'F')
    {
      hdr[3] = in.get();

      if (hdr[0] == 0x7f && hdr[1] == 'E' && hdr[2] == 'L' && hdr[3] == 'F')
      {
        if (filename != "")
          str << "Sorry, but I can't read ELF binary `" << filename << "'";
        else
          str << "Sorry, but I can't read ELF binaries";
      }
      else
        str << "`" << filename << "' is not a goto-binary."
            << "\n";

      log_error("{}", str.str());
      abort();
    }
  }

  irep_serializationt::ireps_containert ic;
  irep_serializationt irepconverter(ic);
  symbol_serializationt symbolconverter(ic);
  goto_function_serializationt gfconverter(ic);

  {
    unsigned version = irepconverter.read_long(in);

    if (version != BINARY_VERSION)
    {
      str << "The input was compiled with a different version of "
          << "goto-cc, please recompile";
      log_error("{}", str.str());
      abort();
    }
  }

  unsigned count = irepconverter.read_long(in);

  for (unsigned i = 0; i < count; i++)
  {
    irept t;
    symbolconverter.convert(in, t);
    symbolt symbol;
    symbol.from_irep(t);

    if (!symbol.is_type && symbol.type.is_code())
    {
      // makes sure there is an empty function
      // for every function symbol and fixes
      // the function types.
      auto it = goto_functions.function_map.find(symbol.id);
      if (it == goto_functions.function_map.end())
        goto_functions.function_map.emplace(symbol.id, goto_functiont());
      goto_functions.function_map.at(symbol.id).type =
        to_code_type(symbol.type);
    }

    // Add functions only from the list
    if (!functions.empty())
    {
      auto it = std::find(
        functions.begin(), functions.end(), symbol.get_function_name().c_str());
      if (it == functions.end())
        continue;
    }

    context.add(symbol);
  }

  assert(migrate_namespace_lookup);

  count = irepconverter.read_long(in);
  for (unsigned i = 0; i < count; i++)
  {
    irept t;
    dstring fname = irepconverter.read_string(in);
    gfconverter.convert(in, t);
    auto it = goto_functions.function_map.find(fname);
    if (it == goto_functions.function_map.end())
      goto_functions.function_map.emplace(fname, goto_functiont());
    goto_functiont &f = goto_functions.function_map.at(fname);
    convert(t, f.body);
    f.body_available = f.body.instructions.size() > 0;
  }

  return false;
}

bool read_bin_goto_object(
  std::istream &in,
  const std::string &filename,
  contextt &context,
  goto_functionst &goto_functions)
{
  std::vector<std::string> empty;
  return read_bin_goto_object(in, filename, context, empty, goto_functions);
}
