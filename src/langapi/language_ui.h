#ifndef CPROVER_LANGUAGE_UI_H
#define CPROVER_LANGUAGE_UI_H

#include <util/language.h>
#include <util/parseoptions.h>

class language_uit
{
public:
  typedef std::map<language_idt, std::unique_ptr<languaget>> langmapt;
  langmapt langmap;

  contextt context;
  namespacet ns;

  language_uit();
  virtual ~language_uit() noexcept = default;

  virtual bool parse(const cmdlinet &cmdline);
  virtual bool parse(const std::string &filename);
  virtual bool typecheck();
  virtual bool final();

  virtual void clear_parse()
  {
    langmap.clear();
  }

  virtual void show_symbol_table();
  virtual void show_symbol_table_plain(std::ostream &out);
  virtual void show_symbol_table_xml_ui();

protected:
  /* The instance of this class manages the global migrate_namespace_lookup,
   * thus it cannot be copied. These functions are protected in order for
   * derived classes to opt-into move support. */
  language_uit(language_uit &&) noexcept;
  language_uit &operator=(language_uit &&) noexcept;
};

#endif
