#include <ac_config.h>
#include <boost/filesystem.hpp>
#include <c2goto/cprover_library.h>
#include <cstdlib>
#include <fstream>
#include <goto-programs/goto_binary_reader.h>
#include <goto-programs/goto_functions.h>
#include <util/c_link.h>
#include <util/config.h>
#include <util/language.h>

extern "C"
{
  extern const uint8_t clib32_buf[];
  extern const uint8_t clib64_buf[];
  extern const unsigned int clib32_buf_size;
  extern const unsigned int clib64_buf_size;

  extern const uint8_t clib32_fp_buf[];
  extern const uint8_t clib64_fp_buf[];
  extern const unsigned int clib32_fp_buf_size;
  extern const unsigned int clib64_fp_buf_size;

  extern const uint8_t clib32_cherih_buf[];
  extern const uint8_t clib64_cherih_buf[];
  extern const unsigned int clib32_cherih_buf_size;
  extern const unsigned int clib64_cherih_buf_size;

  extern const uint8_t clib32_fp_cherih_buf[];
  extern const uint8_t clib64_fp_cherih_buf[];
  extern const unsigned int clib32_fp_cherih_buf_size;
  extern const unsigned int clib64_fp_cherih_buf_size;

  extern const uint8_t clib32_cherip_buf[];
  extern const uint8_t clib64_cherip_buf[];
  extern const unsigned int clib32_cherip_buf_size;
  extern const unsigned int clib64_cherip_buf_size;

  extern const uint8_t clib32_fp_cherip_buf[];
  extern const uint8_t clib64_fp_cherip_buf[];
  extern const unsigned int clib32_fp_cherip_buf_size;
  extern const unsigned int clib64_fp_cherip_buf_size;
}

namespace
{
/* [cheri][floatbv ? 1 : 0][wordsz == 64 ? 1 : 0] */
static const struct buffer
{
  const uint8_t *start;
  size_t size;
} clibs[3][2][2] = {
#ifdef ESBMC_BUNDLE_LIBC
  {
    {
      {&clib32_buf[0], clib32_buf_size},
      {&clib64_buf[0], clib64_buf_size},
    },
    {
      {&clib32_fp_buf[0], clib32_fp_buf_size},
      {&clib64_fp_buf[0], clib64_fp_buf_size},
    },
  },
  {
#  ifdef ESBMC_CHERI_HYBRID_SYSROOT
    {
      {NULL, 0}, // {&clib32_cherih_buf[0], clib32_cherih_buf_size},
      {&clib64_cherih_buf[0], clib64_cherih_buf_size},
    },
    {
      {NULL, 0}, // {&clib32_fp_cherih_buf[0], clib32_fp_cherih_buf_size},
      {&clib64_fp_cherih_buf[0], clib64_fp_cherih_buf_size},
    },
#  endif
  },
  {
#  ifdef ESBMC_CHERI_PURECAP_SYSROOT
    {
      {NULL, 0}, // {&clib32_cherip_buf[0], clib32_cherip_buf_size},
      {&clib64_cherip_buf[0], clib64_cherip_buf_size},
    },
    {
      {NULL, 0}, // {&clib32_fp_cherip_buf[0], clib32_fp_cherip_buf_size},
      {&clib64_fp_cherip_buf[0], clib64_fp_cherip_buf_size},
    },
#  endif
  },
#endif
};

const static std::vector<std::string> python_c_models = {"strncmp"};

} // namespace

static void generate_symbol_deps(
  irep_idt name,
  irept irep,
  std::multimap<irep_idt, irep_idt> &deps)
{
  std::pair<irep_idt, irep_idt> type;

  if (irep.id() == "symbol")
  {
    type = std::pair<irep_idt, irep_idt>(name, irep.identifier());
    deps.insert(type);
    return;
  }

  forall_irep (irep_it, irep.get_sub())
  {
    if (irep_it->id() == "symbol")
    {
      type = std::pair<irep_idt, irep_idt>(name, irep_it->identifier());
      deps.insert(type);
      generate_symbol_deps(name, *irep_it, deps);
    }
    else if (irep_it->id() == "argument")
    {
      type = std::pair<irep_idt, irep_idt>(name, irep_it->cmt_identifier());
      deps.insert(type);
    }
    else
    {
      generate_symbol_deps(name, *irep_it, deps);
    }
  }

  forall_named_irep (irep_it, irep.get_named_sub())
  {
    if (irep_it->second.id() == "symbol")
    {
      type = std::pair<irep_idt, irep_idt>(name, irep_it->second.identifier());
      deps.insert(type);
    }
    else if (irep_it->second.id() == "argument")
    {
      type =
        std::pair<irep_idt, irep_idt>(name, irep_it->second.cmt_identifier());
      deps.insert(type);
    }
    else
    {
      generate_symbol_deps(name, irep_it->second, deps);
    }
  }
}

static void ingest_symbol(
  irep_idt name,
  std::multimap<irep_idt, irep_idt> &deps,
  std::list<irep_idt> &to_include)
{
  std::pair<
    std::multimap<irep_idt, irep_idt>::const_iterator,
    std::multimap<irep_idt, irep_idt>::const_iterator>
    range;
  std::multimap<irep_idt, irep_idt>::const_iterator it;

  range = deps.equal_range(name);
  if (range.first == range.second)
    return;

  for (it = range.first; it != range.second; it++)
    to_include.push_back(it->second);

  deps.erase(name);
}

void add_cprover_library(contextt &context, const languaget *language)
{
  if (config.ansi_c.lib == configt::ansi_ct::libt::LIB_NONE)
    return;

  contextt new_ctx, store_ctx;
  goto_functionst goto_functions;
  std::multimap<irep_idt, irep_idt> symbol_deps;
  std::list<irep_idt> to_include;
  const buffer *clib;

  switch (config.ansi_c.word_size)
  {
  case 16:
    log_warning(
      "this version of ESBMC does not have a C library for 16 bit machines");
    return;
  case 32:
  case 64:
    break;
  default:
    log_error("No C library for bitwidth {}", config.ansi_c.word_size);
    abort();
  }

  clib = &clibs[config.ansi_c.cheri][!config.ansi_c.use_fixed_for_float]
               [config.ansi_c.word_size == 64];

  if (clib->size == 0)
  {
    if (language)
      return add_bundled_library_sources(context, *language);
    log_error("Zero-lengthed internal C library");
    abort();
  }

  goto_binary_reader goto_reader;

  if (language && language->id() == "python")
    goto_reader.set_functions_to_read(python_c_models);

  if (goto_reader.read_goto_binary_array(
        clib->start, clib->size, new_ctx, goto_functions))
    abort();

  new_ctx.foreach_operand([&symbol_deps](const symbolt &s) {
    generate_symbol_deps(s.id, s.value, symbol_deps);
    generate_symbol_deps(s.id, s.type, symbol_deps);
  });

  // Add two hacks; we might use either pthread_mutex_lock or the checked
  // variant; so if one version is used, pull in the other too.
  std::pair<irep_idt, irep_idt> lockcheck(
    dstring("pthread_mutex_lock"), dstring("pthread_mutex_lock_check"));
  symbol_deps.insert(lockcheck);

  std::pair<irep_idt, irep_idt> condcheck(
    dstring("pthread_cond_wait"), dstring("pthread_cond_wait_check"));
  symbol_deps.insert(condcheck);

  std::pair<irep_idt, irep_idt> joincheck(
    dstring("pthread_join"), dstring("pthread_join_noswitch"));
  symbol_deps.insert(joincheck);

  /* The code just pulled into store_ctx might use other symbols in the C
   * library. So, repeatedly search for new C library symbols that we use but
   * haven't pulled in, then pull them in. We finish when we've made a pass
   * that adds no new symbols. */

  new_ctx.foreach_operand(
    [&context, &store_ctx, &symbol_deps, &to_include, &language](
      const symbolt &s) {
      const symbolt *symbol = context.find_symbol(s.id);
      if (
        (language && language->id() == "python") ||
        (symbol != nullptr && symbol->value.is_nil()))
      {
        store_ctx.add(s);
        ingest_symbol(s.id, symbol_deps, to_include);
      }
    });

  for (std::list<irep_idt>::const_iterator nameit = to_include.begin();
       nameit != to_include.end();
       nameit++)
  {
    symbolt *s = new_ctx.find_symbol(*nameit);
    if (s != nullptr)
    {
      store_ctx.add(*s);
      ingest_symbol(*nameit, symbol_deps, to_include);
    }
  }

  if (c_link(context, store_ctx, "<built-in-library>"))
  {
    // Merging failed
    log_error("Failed to merge C library");
    abort();
  }
}
