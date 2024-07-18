#ifndef CPROVER_ANSI_C_CPROVER_LIBRARY_H
#define CPROVER_ANSI_C_CPROVER_LIBRARY_H

#include <util/context.h>
#include <util/message.h>

class languaget;

/* Returns the path the headers of the internal libc have been extracted to
 * or NULL if no library is configured (either via config.ansi_c.lib or during
 * build time). */
const std::string *internal_libc_header_dir();

/* Adds the internal libc to `context` by parsing and linking all C sources.
 *
 * Note that parsing the entire ESBMC standard library is a slow process.
 */
void add_bundled_library_sources(
  contextt &context,
  const languaget &c_language);

void add_cprover_library(
  contextt &context,
  const languaget *language = nullptr);

#endif
