/** \file load.c
 loading of external code, bytecode and shared libs

 (c) 2008 why the lucky stiff, the freelance professor
 (c) 2013,2026 by perl11.com */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dlfcn.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <sys/stat.h>
#include "p2.h"
#include "internal.h"
#include "table.h"

static PN potion_load_code(Potion *P, const char *filename) {
  PN buf, code;
  struct stat stats;
  int fd = -1;
  PN result = PN_NIL;
  if (stat(filename, &stats) == -1) {
    fprintf(stderr, "** %s does not exist.", filename);
    return PN_NIL;
  }
  fd = open(filename, O_RDONLY | O_BINARY);
  if (fd == -1) {
    fprintf(stderr, "** could not open %s. check permissions.", filename);
    return PN_NIL;
  }
  buf = potion_bytes(P, stats.st_size);
  if (read(fd, PN_STR_PTR(buf), stats.st_size) == stats.st_size) {
    PN_STR_PTR(buf)[stats.st_size] = '\0';
    code = potion_source_load(P, PN_NIL, buf);
    if (!PN_IS_PROTO(code)) {
      result = potion_run(P, potion_send(
#ifdef P2
          potion_syntax_parse(P, "p5", buf, filename),
#else
          potion_syntax_parse(P, "potion", buf, filename),
#endif
          PN_compile, potion_str(P, filename), PN_NIL),
        POTION_JIT);
    }
  } else {
    fprintf(stderr, "** could not read entire file: %s.", filename);
  }
  close(fd);
  return result;
}

static char *potion_initializer_name(Potion *P, const char *filename, PN_SIZE len) {
  PN_SIZE ext_name_len = 0;
  char *allocated_str, *ext_name, *func_name;
  while (*(filename + ++ext_name_len) != '.' && ext_name_len <= len);
  allocated_str = ext_name = malloc(ext_name_len + 1);
  if (allocated_str == NULL) potion_allocation_error();
  strncpy(ext_name, filename, ext_name_len);
  ext_name[ext_name_len] = '\0';
  ext_name += ext_name_len;
  while (*--ext_name != '/' && ext_name >= allocated_str);
  ext_name++;
  if (asprintf(&func_name, "Potion_Init_%s", ext_name) == -1)
    potion_allocation_error();
  free(allocated_str);
  return func_name;
}

static PN potion_load_dylib(Potion *P, const char *filename) {
  void *handle = dlopen(filename, RTLD_LAZY);
  void (*func)(Potion *);
  char *err, *init_func_name;
  if (handle == NULL) {
    // TODO: error
    fprintf(stderr, "** error loading %s: %s\n", filename, dlerror());
    return PN_NIL;
  }
  init_func_name = potion_initializer_name(P, filename, strlen(filename));
  func = (void (*)(Potion *))dlsym(handle, init_func_name);
  err = (char *)dlerror();
  if (err != NULL) {
    fprintf(stderr, "** error loading %s in %s: %s\n", init_func_name, filename, err);
    free(init_func_name);
    dlclose(handle);
    return PN_NIL;
  }
  free(init_func_name);
  func(P);
  return PN_TRUE;
}

static PN pn_loader_path;
static const char *pn_loader_extensions[] = {
#ifndef P2
    ".pnb"
  , ".pn"
#else
    ".pnb"
  , ".pn"
  , ".plc"
  , ".pl"
  , ".pmc"
  , ".pm"
  , ".p6"
  , ".raku"
#endif
  , POTION_LOADEXT // ".so"
};

static const char *find_extension(char *str) {
  int i;
  PN_SIZE str_len = strlen(str);
  struct stat st;
  for (i = 0;
       i < sizeof(pn_loader_extensions) / sizeof(void *);
       i++) {
    PN_SIZE len = strlen(pn_loader_extensions[i]);
    char buf[str_len + len + 1];
    strncpy(buf, str, str_len);
    strncpy(buf + str_len, pn_loader_extensions[i], len);
    buf[str_len + len] = '\0';
    if (stat(buf, &st) == 0 && S_ISREG(st.st_mode))
      return pn_loader_extensions[i];
  }
  return NULL;
}

char *potion_find_file(Potion *P, char *str, PN_SIZE str_len) {
  char *r = NULL;
  struct stat st;
  if (!str_len) str_len = strlen(str);
  PN_TUPLE_EACH(pn_loader_path, i, prefix, {
    PN_SIZE prefix_len = PN_STR_LEN(prefix);
    char dirname[prefix_len + 1 + str_len + 1];
    char *str_pos = dirname + prefix_len + 1;
    char *dot;
    const char *ext;
    memcpy(str_pos, str, str_len);
    dot = memchr(str, '.', str_len);
    if (dot == NULL)
      dirname[sizeof(dirname) - 1] = '\0';
    else
      *dot = '\0';
    memcpy(dirname, PN_STR_PTR(prefix), prefix_len);
    dirname[prefix_len] = '/';
    if (stat(dirname, &st) == 0 && S_ISREG(st.st_mode)) {
      if (asprintf(&r, "%s", dirname) == -1) potion_allocation_error();
      break;
    } else if ((ext = find_extension(dirname)) != NULL) {
      if (asprintf(&r, "%s%s", dirname, ext) == -1) potion_allocation_error();
      break;
    } else {
      char *file;
      if ((file = strrchr(str, '/')) == NULL)
        file = str;
      else
        file++;
      if (asprintf(&r, "%s/%s", dirname, file) == -1) potion_allocation_error();
      if (stat(r, &st) != 0 || !S_ISREG(st.st_mode)) {
        int r_len = prefix_len + 1 + str_len * 2 + 1;
        if ((ext = find_extension(r)) == NULL) { free(r); r = NULL; continue; }
        r = realloc(r, r_len + strlen(ext));
        if (r == NULL) potion_allocation_error();
        strcpy(r + r_len, ext);
      }
      break;
    }
  });
  return r;
}

#ifndef P2
PN potion_load(Potion *P, PN cl, PN self, PN file) {
  if (!file && PN_IS_STR(self))
    file = self;
  char *filename = potion_find_file(P, PN_STR_PTR(file), PN_STR_LEN(file));
  char *file_ext;
  PN result = PN_NIL;
  if (filename == NULL) {
    fprintf(stderr, "** can't find %s\n", PN_STR_PTR(file));
    return PN_NIL;
  }
  file_ext = filename + strlen(filename);
  while (*--file_ext != '.' && file_ext >= filename);
  if (file_ext++ != filename) {
    if (strcmp(file_ext, "pn") == 0)
      result = potion_load_code(P, filename);
    else if (strcmp(file_ext, "pnb") == 0)
      result = potion_load_code(P, filename);
    else if (strcmp(file_ext, &POTION_LOADEXT[1]) == 0)
      result = potion_load_dylib(P, filename);
    else
      fprintf(stderr, "** unrecognized file extension: %s\n", file_ext);
  } else {
    fprintf(stderr, "** no file extension: %s\n", filename);
  }
  free(filename);
  return result;
}

#else

/* load a .p6 or .raku file: set MODE_P6, load syntax+runtime libs, parse, run */
static PN p6_load(Potion *P, const char *filename) {
  struct stat stats;
  if (stat(filename, &stats) == -1) {
    fprintf(stderr, "** %s does not exist.\n", filename);
    return PN_NIL;
  }
  int fd = open(filename, O_RDONLY | O_BINARY);
  if (fd == -1) {
    fprintf(stderr, "** could not open %s.\n", filename);
    return PN_NIL;
  }
  PN buf = potion_bytes(P, stats.st_size);
  if (read(fd, PN_STR_PTR(buf), stats.st_size) != stats.st_size) {
    close(fd); return PN_NIL;
  }
  close(fd);
  PN_STR_PTR(buf)[stats.st_size] = '\0';

  /* set p6 mode */
  P->flags = (P->flags & ~0xff) | MODE_P6;


  /* load p6 runtime library (once) */
  { static int libp6_loaded = 0;
    if (!libp6_loaded) {
      void *h = dlopen(potion_find_file(P, "libp6", 5), RTLD_LAZY);
      if (h) {
        void (*init)(Potion *) = dlsym(h, "Potion_Init_libp6");
        if (init) { init(P); libp6_loaded = 1; }
      }
    }
  }

  /* parse p6 source directly (no use p6 {} wrapping) */
  PN code = potion_syntax_parse(P, "p6", buf, filename);
  if (code && PN_TYPE(code) != PN_TERROR) {
    code = potion_send(code, PN_compile, potion_str(P, filename), PN_NIL);
    return potion_run(P, code, P->flags & EXEC_JIT);
  }
  return PN_NIL;
}

PN p2_load(Potion *P, PN cl, PN self, PN file) {
  if (!file && PN_IS_STR(self))
    file = self;
  char *filename = potion_find_file(P, PN_STR_PTR(file), PN_STR_LEN(file)), *file_ext;
  PN result = PN_NIL;
  if (filename == NULL) {
    fprintf(stderr, "** can't find %s\n", PN_STR_PTR(file));
    return PN_NIL;
  }
  file_ext = filename + strlen(filename);
  while (*--file_ext != '.' && file_ext >= filename);
  if (file_ext++ != filename) {
    if (strcmp(file_ext, "pn") == 0)
      result = p2_load_code(P, filename);
    else if (strcmp(file_ext, "pnb") == 0)
      result = p2_load_code(P, filename);
    else if (strcmp(file_ext, "pl") == 0)
      result = p2_load_code(P, filename);
    else if (strcmp(file_ext, "plc") == 0)
      result = p2_load_code(P, filename);
    else if (strcmp(file_ext, "p6") == 0)
      result = p6_load(P, filename);
    else if (strcmp(file_ext, "raku") == 0)
      result = p6_load(P, filename);
    else if (strcmp(file_ext, &POTION_LOADEXT[1]) == 0)
      result = potion_load_dylib(P, filename);
    else
      fprintf(stderr, "** unrecognized file extension: %s\n", file_ext);
  } else {
    fprintf(stderr, "** no file extension: %s\n", filename);
  }
  free(filename);
  return result;
}
#endif

void potion_loader_add(Potion *P, PN path) {
  PN_PUSH(pn_loader_path, path);
}

#ifdef P2
static const char *potion_argv0;
void potion_set_argv0(const char *a) { potion_argv0 = a; }

/* the directory of libp2 (dladdr, when the loader reports a path), else <dir of the executable>/../lib (argv[0]: absolute,
 * relative to the cwd, or found in PATH), or of the executable on Windows. */
static int potion_own_libdir(char *buf, size_t sz) {
#ifdef _WIN32
  char *slash;
  if (GetModuleFileNameA(NULL, buf, (DWORD)sz) == 0 || !(slash = strrchr(buf, '\\'))) return 0;
  *slash = 0;
  return 1;
#else
  Dl_info info;
  char *slash;
  if (dladdr((void *)potion_loader_init, &info) && info.dli_fname && strchr(info.dli_fname, '/')) {
    char *rp = realpath(info.dli_fname, NULL);
    snprintf(buf, sz, "%s", rp ? rp : info.dli_fname);
    free(rp);
    slash = strrchr(buf, '/');
    *slash = 0;
    return 1;
  }
  if (potion_argv0) { /* <bindir>/../lib */
    char exe[4096], *rp = NULL;
    if (strchr(potion_argv0, '/')) rp = realpath(potion_argv0, NULL);
    else { /* a name in PATH */
      const char *path = getenv("PATH");
      while (path && *path && !rp) {
        const char *e = strchr(path, ':');
        size_t n = e ? (size_t)(e - path) : strlen(path);
        snprintf(exe, sizeof exe, "%.*s/%s", (int)n, path, potion_argv0);
        if (access(exe, X_OK) == 0) rp = realpath(exe, NULL);
        path = e ? e + 1 : NULL;
      }
    }
    if (rp) {
      slash = strrchr(rp, '/');
      if (slash) { *slash = 0; snprintf(buf, sz, "%s/../lib", rp); free(rp); return 1; }
      free(rp);
    }
  }
  return 0;
#endif
}
#endif

void potion_loader_init(Potion *P) {
  pn_loader_path = PN_TUP0();
  // relocatable path - relative to exe in argv[0]
  //PN arg0 = potion_send(potion_str(P, "$^X")); // but too early for argv[0]
  //if (arg0) PN_PUSH(pn_loader_path, potion_strcat(P, basename(PN_STR_PTR(arg0)), "../lib/potion"));
#ifdef P2
  { char dir[1100], sub[1200]; /* first: the libraries this binary belongs to, then the cwd relative ones */
    if (potion_own_libdir(dir, sizeof dir)) {
#ifdef _WIN32
      /* the dlls are typically next to the exe, or in <bindir>/lib/p2; a build tree has <bindir>/../lib/{p2,p6,potion} */
      PN_PUSH(pn_loader_path, potion_str(P, dir));
      snprintf(sub, sizeof sub, "%s\\p2", dir); PN_PUSH(pn_loader_path, potion_str(P, sub));
      snprintf(sub, sizeof sub, "%s\\lib\\p2", dir); PN_PUSH(pn_loader_path, potion_str(P, sub));
      snprintf(sub, sizeof sub, "%s\\lib\\p6", dir); PN_PUSH(pn_loader_path, potion_str(P, sub));
      snprintf(sub, sizeof sub, "%s\\lib\\potion", dir); PN_PUSH(pn_loader_path, potion_str(P, sub));
      snprintf(sub, sizeof sub, "%s\\..\\lib\\p2", dir); PN_PUSH(pn_loader_path, potion_str(P, sub));
      snprintf(sub, sizeof sub, "%s\\..\\lib\\p6", dir); PN_PUSH(pn_loader_path, potion_str(P, sub));
      snprintf(sub, sizeof sub, "%s\\..\\lib\\potion", dir); PN_PUSH(pn_loader_path, potion_str(P, sub));
#else
      snprintf(sub, sizeof sub, "%s/p2", dir); PN_PUSH(pn_loader_path, potion_str(P, sub));
      snprintf(sub, sizeof sub, "%s/p6", dir); PN_PUSH(pn_loader_path, potion_str(P, sub));
      snprintf(sub, sizeof sub, "%s/potion", dir); PN_PUSH(pn_loader_path, potion_str(P, sub));
#endif
    } }
  PN_PUSH(pn_loader_path, potion_str(P, "lib/p2"));
  PN_PUSH(pn_loader_path, potion_str(P, POTION_PREFIX"/lib/p2"));
  PN_PUSH(pn_loader_path, potion_str(P, "lib/p6"));
  PN_PUSH(pn_loader_path, potion_str(P, POTION_PREFIX"/lib/p6"));
#endif
  PN_PUSH(pn_loader_path, potion_str(P, "lib/potion"));
  PN_PUSH(pn_loader_path, potion_str(P, POTION_PREFIX"/lib/potion"));
  PN_PUSH(pn_loader_path, potion_str(P, "."));

#ifdef P2
#define LOADER_PATH "@INC"
#else
#define LOADER_PATH "LOADER_PATH"
#endif
  potion_define_global(P, potion_str(P, LOADER_PATH), pn_loader_path);
  potion_method(P->lobby, "load", potion_load, "file=S");
}

/// Locate a file for p5 require: as given (relative to the cwd), else in each
/// -I directory. Returns a malloc'ed path or NULL.
char *potion_find_require(Potion *P, const char *name) {
  struct stat st;
  char *r = NULL;
  if (stat(name, &st) == 0 && S_ISREG(st.st_mode))
    return strdup(name);
  if (pn_loader_path == PN_NIL || !pn_loader_path) return NULL;
  PN_TUPLE_EACH(pn_loader_path, i, prefix, {
    if (asprintf(&r, "%s/%s", PN_STR_PTR(prefix), name) == -1)
      potion_allocation_error();
    if (stat(r, &st) == 0 && S_ISREG(st.st_mode))
      return r;
    free(r);
    r = NULL;
  });
  return NULL;
}
