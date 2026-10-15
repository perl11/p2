/** \file syntaxobj.c
 * Runtime objects for linked and greg-compiled syntax libraries.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef SANDBOX
#include <dlfcn.h>
#endif
#include "p2.h"
#include "internal.h"

#include "table.h"
typedef struct {
  const char *name;
  const char *library;
  const char *entry;
  PNSyntaxParse linked;
  void *handle;
  PNSyntaxParse parser;
} PNSyntaxSpec;

struct PNSyntaxPayload {
  PNSyntaxParse parser;
  void *handle;
};

static PNType syntax_type;

#ifdef P2
static PNSyntaxSpec syntax_potion = { "potion", "libsyntax", "potion_parse", NULL, NULL, NULL };
static PNSyntaxSpec syntax_p5 = { "p5", "static", "p2_parse", p2_parse, NULL, NULL };
#else
static PNSyntaxSpec syntax_potion = { "potion", "static", "potion_parse", potion_parse, NULL, NULL };
static PNSyntaxSpec syntax_p5 = { "p5", "libsyntax-p5", "p2_parse", NULL, NULL, NULL };
#endif
static PNSyntaxSpec syntax_p6 = { "p6", "libsyntax-p6", "syntax_parse", NULL, NULL, NULL };

static PN potion_syntax_error1(Potion *P, const char *format, const char *arg) {
  return potion_error(P, potion_str_format(P, format, arg), 0, 0, PN_NIL);
}

static PNSyntaxSpec *potion_syntax_spec(const char *name) {
  if (!name) {
#ifdef P2
    return &syntax_p5;
#else
    return &syntax_potion;
#endif
  }
  if (!strcmp(name, "potion")) return &syntax_potion;
  if (!strcmp(name, "p5")) return &syntax_p5;
  if (!strcmp(name, "p6")) return &syntax_p6;
  return NULL;
}

static PN potion_syntax_new(Potion *P, PN name, PN source, PN path,
                            PN library, PN entry, PNSyntaxParse parser,
                            void *handle) {
  PN object = (PN)PN_ALLOC_N(syntax_type, struct PNObject, 6 * sizeof(PN));
  PN storage = (PN)potion_data_alloc(P, sizeof(struct PNSyntaxPayload));
  vPN(Syntax) syntax = (vPN(Syntax))potion_fwd(object);
  struct PNSyntaxPayload *data = (struct PNSyntaxPayload *)PN_DATA(storage);
  data->parser = parser;
  data->handle = handle;
  syntax->name = potion_fwd(name);
  syntax->source = potion_fwd(source);
  syntax->path = potion_fwd(path);
  syntax->library = potion_fwd(library);
  syntax->entry = potion_fwd(entry);
  syntax->data = potion_fwd(storage);
  PN_TOUCH(object);
  return object;
}

static PN potion_syntax_from_spec(Potion *P, PNSyntaxSpec *spec) {
  PNSyntaxParse parser = spec->linked ? spec->linked : spec->parser;
#ifndef SANDBOX
  if (!parser) {
    char *path = potion_find_file(P, (char *)spec->library, strlen(spec->library));
    const char *error;
    if (!path)
      return potion_syntax_error1(P, "Syntax library %s not found", spec->library);
    spec->handle = dlopen(path, RTLD_LAZY | RTLD_LOCAL);
    if (!spec->handle) {
      error = dlerror();
      free(path);
      return potion_syntax_error1(P, "Cannot load syntax library: %s",
                                  error ? error : spec->library);
    }
    dlerror();
    spec->parser = (PNSyntaxParse)dlsym(spec->handle, spec->entry);
    error = dlerror();
    free(path);
    if (error || !spec->parser) {
      dlclose(spec->handle);
      spec->handle = NULL;
      spec->parser = NULL;
      return potion_syntax_error1(P, "Cannot load syntax entry point: %s",
                                  error ? error : spec->entry);
    }
    parser = spec->parser;
  }
#else
  if (!parser)
    return potion_syntax_error1(P,
      "Dynamic syntax library disabled in sandbox: %s", spec->library);
#endif
  return potion_syntax_new(P, potion_str(P, spec->name), PN_NIL, PN_NIL,
                           potion_str(P, spec->library),
                           potion_str(P, spec->entry), parser, spec->handle);
}

PN potion_syntax(Potion *P, const char *name) {
  PNSyntaxSpec *spec = potion_syntax_spec(name);
  if (!spec) return potion_syntax_error1(P, "Unknown syntax: %s", name);
  return potion_syntax_from_spec(P, spec);
}

#ifndef SANDBOX
static unsigned long long potion_syntax_hash(PN source) {
  const unsigned char *s = (const unsigned char *)PN_STR_PTR(source);
  size_t i, len = PN_STR_LEN(source);
  unsigned long long hash = 1469598103934665603ULL;
  for (i = 0; i < len; i++) {
    hash ^= s[i];
    hash *= 1099511628211ULL;
  }
  return hash;
}

static void potion_syntax_safe_name(PN name, char *out, size_t size) {
  const char *s = PN_STR_PTR(name);
  size_t i, n = PN_STR_LEN(name), j = 0;
  for (i = 0; i < n && j + 1 < size; i++) {
    unsigned char c = (unsigned char)s[i];
    out[j++] = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '_') ? (char)c : '_';
  }
  if (!j && size > 1) out[j++] = 'G';
  out[j] = 0;
}

static int potion_syntax_write(const char *path, PN source) {
  FILE *file = fopen(path, "wb");
  size_t len = PN_STR_LEN(source);
  int ok;
  if (!file) return 0;
  ok = fwrite(PN_STR_PTR(source), 1, len, file) == len;
  if (fclose(file) != 0) ok = 0;
  return ok;
}

static const char *potion_syntax_greg(void) {
  static char installed[1024];
  if (access("bin/greg" POTION_EXE, X_OK) == 0) return "bin/greg" POTION_EXE;
  snprintf(installed, sizeof(installed), "%s/bin/greg%s", POTION_PREFIX, POTION_EXE);
  return access(installed, X_OK) == 0 ? installed : NULL;
}

static const char *potion_syntax_include(void) {
  static char installed[1024];
  if (access("core/potion.h", R_OK) == 0) return "core";
  snprintf(installed, sizeof(installed), "%s/include/potion", POTION_PREFIX);
  return access(installed, R_OK) == 0 ? installed : NULL;
}
#endif

PN potion_syntax_compile(Potion *P, PN name, PN source) {
  PN_CHECK_STR(name);
  PN_CHECK_STR(source);
#ifdef SANDBOX
  return potion_syntax_error1(P, "Grammar compilation disabled in sandbox: %s", PN_STR_PTR(name));
#elif POTION_WIN32
  return potion_syntax_error1(P, "Grammar compilation is not supported on this platform: %s", PN_STR_PTR(name));
#else
  char safe[65], base[160], ypath[192], cpath[192], sopath[192];
  char command[4096];
  const char *greg = potion_syntax_greg();
  const char *include = potion_syntax_include();
  unsigned long long hash = potion_syntax_hash(source);
  void *handle;
  PNSyntaxParse parser;
  const char *error;

  if (!greg)
    return potion_syntax_error1(P, "greg executable not found for grammar %s", PN_STR_PTR(name));
  if (!include)
    return potion_syntax_error1(P, "Potion headers not found for grammar %s", PN_STR_PTR(name));
  if (mkdir(".p2-syntax", 0700) != 0 && errno != EEXIST)
    return potion_syntax_error1(P, "Cannot create .p2-syntax for grammar %s", PN_STR_PTR(name));

  potion_syntax_safe_name(name, safe, sizeof(safe));
  snprintf(base, sizeof(base), ".p2-syntax/%s-%016llx", safe, hash);
  snprintf(ypath, sizeof(ypath), "%s.y", base);
  snprintf(cpath, sizeof(cpath), "%s.c", base);
  snprintf(sopath, sizeof(sopath), "%s%s", base, POTION_LOADEXT);

  if (access(sopath, R_OK) != 0) {
    if (!potion_syntax_write(ypath, source))
      return potion_syntax_error1(P, "Cannot write generated grammar: %s", ypath);
    snprintf(command, sizeof(command), "'%s' -o '%s' '%s'", greg, cpath, ypath);
    if (system(command) != 0)
      return potion_syntax_error1(P, "greg failed for generated grammar: %s", ypath);
#if defined(__APPLE__)
    snprintf(command, sizeof(command),
      "%s %s -w -DP2 -fPIC -dynamiclib -undefined dynamic_lookup -I'%s' -o '%s' '%s'",
      POTION_CC, POTION_CFLAGS, include, sopath, cpath);
#else
    snprintf(command, sizeof(command),
      "%s %s -w -DP2 -fPIC -shared -I'%s' -o '%s' '%s'",
      POTION_CC, POTION_CFLAGS, include, sopath, cpath);
#endif
    if (system(command) != 0)
      return potion_syntax_error1(P, "C compilation failed for generated grammar: %s", cpath);
  }

  handle = dlopen(sopath, RTLD_NOW | RTLD_LOCAL);
  if (!handle)
    return potion_syntax_error1(P, "Cannot load generated grammar: %s", dlerror());
  dlerror();
  parser = (PNSyntaxParse)dlsym(handle, "p6_grammar_parse");
  error = dlerror();
  if (error || !parser) {
    dlclose(handle);
    return potion_syntax_error1(P, "Generated grammar entry point missing: %s", error ? error : sopath);
  }
  return potion_syntax_new(P, name, source, potion_str(P, ypath),
                           potion_str(P, sopath), potion_str(P, "p6_grammar_parse"),
                           parser, handle);
#endif
}

static PN potion_syntax_get(Potion *P, PN cl, PN self, PN name) {
  return potion_syntax(P, PN_STR_PTR(name));
}
static PN potion_syntax_compile_method(Potion *P, PN cl, PN self, PN name, PN source) {
  return potion_syntax_compile(P, name, source);
}
static PN potion_syntax_potion(Potion *P, PN cl, PN self) { return potion_syntax_from_spec(P, &syntax_potion); }
static PN potion_syntax_p5(Potion *P, PN cl, PN self) { return potion_syntax_from_spec(P, &syntax_p5); }
static PN potion_syntax_p6(Potion *P, PN cl, PN self) { return potion_syntax_from_spec(P, &syntax_p6); }
static PN potion_syntax_name(Potion *P, PN cl, PN self) { return ((vPN(Syntax))self)->name; }
static PN potion_syntax_source(Potion *P, PN cl, PN self) { return ((vPN(Syntax))self)->source; }
static PN potion_syntax_path(Potion *P, PN cl, PN self) { return ((vPN(Syntax))self)->path; }
static PN potion_syntax_library(Potion *P, PN cl, PN self) { return ((vPN(Syntax))self)->library; }
static PN potion_syntax_entry(Potion *P, PN cl, PN self) { return ((vPN(Syntax))self)->entry; }

static PN potion_syntax_parse_method(Potion *P, PN cl, PN self, PN source, PN filename) {
  vPN(Syntax) syntax = (vPN(Syntax))self;
  struct PNSyntaxPayload *payload;
  const char *fallback;
  char *file;
  PN result;
  if (!PN_IS_STR(source) && (!PN_IS_PTR(source) || PN_TYPE(source) != PN_TBYTES))
    return potion_type_error_want(P, "source", source, "String or Bytes");
  payload = (struct PNSyntaxPayload *)PN_DATA(syntax->data);
  fallback = PN_IS_STR(syntax->name) ? PN_STR_PTR(syntax->name) : "<syntax>";
  file = strdup(PN_IS_STR(filename) && PN_STR_LEN(filename) ? PN_STR_PTR(filename) : fallback);
  if (!file) potion_allocation_error();
  result = payload->parser(P, source, file);
  free(file);
  return result;
}

PN potion_syntax_parse(Potion *P, const char *name, PN source, const char *filename) {
  PN syntax = potion_syntax(P, name);
  if (PN_TYPE(syntax) == PN_TERROR) return syntax;
  return potion_send(syntax, potion_str(P, "parse"), source,
                     potion_str(P, filename ? filename : "<syntax>"));
}

void potion_syntax_init(Potion *P) {
  PN ivars = PN_TUP0();
  PN syntax_vt;
  PN_PUSH(ivars, PN_STR("_name"));
  PN_PUSH(ivars, PN_STR("_source"));
  PN_PUSH(ivars, PN_STR("_path"));
  PN_PUSH(ivars, PN_STR("_library"));
  PN_PUSH(ivars, PN_STR("_entry"));
  PN_PUSH(ivars, PN_STR("_data"));
  syntax_vt = potion_class(P, PN_NIL, PN_VTABLE(PN_TOBJECT), ivars);
  syntax_type = potion_class_type(P, syntax_vt);
  potion_define_global(P, potion_str(P, "Syntax"), syntax_vt);
  potion_class_method(syntax_vt, "get", potion_syntax_get, "name=S");
  potion_class_method(syntax_vt, "compile", potion_syntax_compile_method, "name=S,source=S");
  potion_class_method(syntax_vt, "potion", potion_syntax_potion, 0);
  potion_class_method(syntax_vt, "p5", potion_syntax_p5, 0);
  potion_class_method(syntax_vt, "p6", potion_syntax_p6, 0);
  potion_method(syntax_vt, "name", potion_syntax_name, 0);
  potion_method(syntax_vt, "source", potion_syntax_source, 0);
  potion_method(syntax_vt, "path", potion_syntax_path, 0);
  potion_method(syntax_vt, "library", potion_syntax_library, 0);
  potion_method(syntax_vt, "entry", potion_syntax_entry, 0);
  potion_method(syntax_vt, "parse", potion_syntax_parse_method, "source=o|filename=S");
}
