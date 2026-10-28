//
// p2.c
// perl 5+6 on potion
//
// (c) 2008 why the lucky stiff, the freelance professor
// (c) 2013 by perl11.com
//
#include <stdio.h>
#include <sys/stat.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>
#ifdef _WIN32
# include <windows.h>
#endif
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>

#include "p2.h"
#include "internal.h"
#include "opcodes.h"
#include "khash.h"
#include "table.h"

#ifdef POTION_JIT_TARGET
#define P2_BANNER(jit) "p2 " P2_VERSION \
  " (date='" POTION_DATE "', commit='" POTION_COMMIT \
  "', platform='" POTION_PLATFORM \
  "', jit=%d/" jit \
  ")\n"
#else
#define P2_BANNER(nojit) "p2 " P2_VERSION \
  " (date='" POTION_DATE "', commit='" POTION_COMMIT \
  "', platform='" POTION_PLATFORM \
  "', jit=%d" \
  ")\n"
#endif

void Potion_Init_libp5(Potion *);

#ifndef STATIC
/* libp5 is found via the loader path (cwd or install prefix), else next to
 * the libp2 shared library: <libdir>/p2/libp5.so (so tests may run from any cwd) */
static void p5_load(Potion *P) {
  void *h = NULL;
  char *path = potion_find_file(P, "libp5", 5);
  char buf[4096];
  if (path) {
    h = dlopen(path, RTLD_LAZY);
    if (!h) fprintf(stderr, "** %s: %s\n", path, dlerror());
  }
  if (!h) {
    buf[0] = 0;
#ifdef _WIN32
    /* <bindir>/libp5.dll, or <bindir>/../lib/p2/libp5.dll */
    if (GetModuleFileNameA(NULL, buf, sizeof buf - 32) > 0) {
      char *slash = strrchr(buf, '\\');
      if (slash) {
        char same[4096 + 32]; /* buf (< 4064 bytes) plus a suffix */
        *slash = 0;
        /* next to the exe (an install), else the build tree: <bindir>/../lib/p2 */
        snprintf(same, sizeof same, "%s\\libp5" POTION_LOADEXT, buf);
        if (GetFileAttributesA(same) != INVALID_FILE_ATTRIBUTES) strcpy(buf, same);
        else if (snprintf(same, sizeof same, "%s\\lib\\p2\\libp5" POTION_LOADEXT, buf), GetFileAttributesA(same) != INVALID_FILE_ATTRIBUTES) strcpy(buf, same);
        else strcat(buf, "\\..\\lib\\p2\\libp5" POTION_LOADEXT);
      }
      else buf[0] = 0;
    }
#else
    Dl_info info;
    if (dladdr((void *)potion_create, &info) && info.dli_fname) {
      char *slash;
      snprintf(buf, sizeof buf, "%s", info.dli_fname);
      if ((slash = strrchr(buf, '/'))) {
        *slash = 0;
        snprintf(slash, sizeof buf - (slash - buf), "/p2/libp5" POTION_LOADEXT);
      } else buf[0] = 0;
    }
#endif
    if (buf[0]) h = dlopen(buf, RTLD_LAZY);
  }
  void (*init)(Potion *) = h ? (void (*)(Potion *))dlsym(h, "Potion_Init_libp5") : NULL;
  if (init) init(P);
  else fprintf(stderr, "** libp5 not found: p5 file handles, map, grep and sort are unavailable\n");
  free(path);
}
#endif

const char p2_banner[]  = P2_BANNER(_XSTR(POTION_JIT_NAME));
const char p2_version[] = P2_VERSION;

// POTION_JIT 1/0 if jit is default
// POTION_JIT_TARGET defined if jittable
static void p2_cmd_usage(Potion *P) {
  printf("usage: p2 [options] [script] [arguments]\n"
#ifdef POTION_JIT_TARGET
      "  -B, --bytecode     run with bytecode VM (slower, but cross-platform)"
#if !POTION_JIT
	 " (default)\n"
#else
	 "\n"
#endif
      "  -J, --jit          run with JIT VM (faster, only x86, x86-64, ppc)"
#if POTION_JIT
	 " (default)\n"
#else
	 "\n"
#endif
#endif
      "  -Idirectory        add library search path\n"
      "  -c                 check script and exit\n" // compile-time checks only
      "  -d                 run script under the debugger\n"
#ifdef DEBUG
      "  -D[itvpPcGJ]       debugging flags, try -D?\n"
#endif
      "  -e code            execute code\n"
      "  -E code            execute perl5/p2 code with extended features enabled\n"
      "  -i[.bak]           edit <> files in place (makes backup if extension supplied)\n"
      "  -n, -p             loop over the <> lines of -e code or the script; -p prints $_\n"
      "  -l[oct], -a, -F pat  chomp lines and set $\\; autosplit into @F, split pattern\n"
      "  -0[oct|xHEX]       input record separator ($/): -00 paragraphs, -0777 slurp\n"
      "  -x[dir]            start the script at the first #!..perl line\n"
      "  -6                 use perl6 mode, using syntax-p6\n"
      "  -V, --verbose      print bytecode and ast info\n"
      "  -h, --help         print this usage info and exit\n"
      "  -v, --version      print version, patchlevel, features and exit\n"
      "  --inspect          print the return value\n"
      "  --stats            print statistics and exit\n"
      "  --compile          compile the script to bytecode\n"
      "  --compile={target[,args]} generate bytecode (bc), C (c), or exe\n"
      "                     exe args go to the C compiler: --compile=exe,-o=prog,-lm,-DX=1\n"
      "                     (-o=path is written as \"-o path\", other args are passed as is)\n"
      "  --compile=c        write <script>.c without running the script\n"
      "                     compile externally with -Icore; the link line for this platform\n"
      "                     is printed after writing (-lp5 -lp2, -lp6 -lp5 -lp2 for p6)\n"
      "                     add -DPOTION_MODULE -fPIC -shared for a module\n"
  );
}

static void p2_cmd_stats(Potion *P) {
  printf("sizeof(PN=%d, PNObject=%d, PNTuple=%d, PNTable=%d)\n",
      (int)sizeof(PN), (int)sizeof(struct PNObject), (int)sizeof(struct PNTuple),
      (int)sizeof(struct PNTable));
  printf("GC (fixed=%" PRIdPTR ", actual=%" PRIdPTR ", reserved=%" PRIdPTR ")\n",
      PN_INT(potion_gc_fixed(P, 0, 0)), PN_INT(potion_gc_actual(P, 0, 0)),
      PN_INT(potion_gc_reserved(P, 0, 0)));
}

static void p2_cmd_version(Potion *P) {
  printf(p2_banner, POTION_JIT);
}

#define DBG_Pv(p) \
    if (P->flags & DEBUG_VERBOSE) \
      potion_p(P, p)
#define DBG_Pvi(p) \
    if (P->flags & (DEBUG_INSPECT|DEBUG_VERBOSE)) \
      potion_p(P, p)

/* Does buf begin (after leading ws/#comments) with a BARE 'use v6' or
 * 'use p6' pragma (not the block form 'use p6 { ... }', which the p5
 * grammar's syntax-block rule already captures reliably via balanced-
 * brace text capture)? The bare whole-file form ("rest of the file is
 * p6") turned out to be unsafe to express as a mid-parse PEG grammar
 * action here (G->pos/end-of-file semantics in this greg dialect don't
 * give a reliable "capture everything to true EOF" primitive at the
 * statement level) -- so detect it up front instead and switch the
 * whole buffer to p6 mode before any p5 parsing is attempted. */
static int pn_buf_is_bare_use_p6(PN buf) {
  const char *s; long len, i = 0;
  if (!PN_IS_STR(buf) && PN_TYPE(buf) != PN_TBYTES) return 0;
  s = PN_STR_PTR(buf);
  len = (long)PN_STR_LEN(buf);
  for (;;) {
    while (i < len && (s[i]==' '||s[i]=='\t'||s[i]=='\r'||s[i]=='\n')) i++;
    if (i < len && s[i] == '#') { while (i < len && s[i] != '\n') i++; continue; }
    break;
  }
  if (i + 3 > len || strncmp(s+i, "use", 3) != 0) return 0;
  i += 3;
  if (i >= len || !(s[i]==' '||s[i]=='\t')) return 0;
  while (i < len && (s[i]==' '||s[i]=='\t')) i++;
  if (i + 2 > len || !((!strncmp(s+i,"v6",2)) || (!strncmp(s+i,"p6",2)))) return 0;
  i += 2;
  if (i < len && (isalnum((unsigned char)s[i]) || s[i]=='_')) return 0; /* e.g. v60, p6x */
  while (i < len && (s[i]==' '||s[i]=='\t')) i++;
  return !(i < len && s[i] == '{'); /* block form: let the grammar handle it */
}

/* set when parsing or compiling failed: a program's own value may legitimately be undef (nil) */
static int p2_cmd_failed;

static PN p2_cmd_exec(Potion *P, PN buf, char *filename, char *compile) {
  /* NOTE: the low byte of P->flags packs BOTH the mode+exec value
   * (0x00-0x3f, see MAX_SYNTAX) AND, in its upper bits, DEBUG_INSPECT
   * (0x40) / DEBUG_VERBOSE (0x80) -- using an 0xff mask here (as this
   * used to) reads/clobbers those debug bits too: '-6 --inspect'
   * silently lost --inspect's effect because this 'replace mode+exec'
   * line cleared the WHOLE byte including DEBUG_INSPECT. Use 0x3f
   * (MAX_SYNTAX), not 0xff, for all mode+exec read/clear masks below. */
  if (!((P->flags & MAX_SYNTAX) >= MODE_P6 && (P->flags & MAX_SYNTAX) < MODE_P6 + (1<<EXEC_BITS))
      && pn_buf_is_bare_use_p6(buf)) {
    P->flags = (P->flags & ~MAX_SYNTAX) | (MODE_P6 + (P->flags & ((1<<EXEC_BITS)-1)));
  }
  PN code = p2_source_load(P, PN_NIL, buf);
  if (PN_IS_PROTO(code)) {
  } else if ((P->flags & 0x30) == (MODE_P6 & 0x30) &&
             (P->flags & MAX_SYNTAX) >= MODE_P6 &&
             (P->flags & MAX_SYNTAX) < MODE_P6 + (1<<EXEC_BITS) &&
             (PN_IS_STR(buf) || PN_TYPE(buf) == PN_TBYTES)) {
    /* p6 mode: parse with Syntax.p6 (no use p6 {} wrapping) */
    /* load libp6 (once) */
    { static int libp6_loaded = 0;
      if (!libp6_loaded) {
        void *h = potion_dlopen_find(P, "libp6");
        if (h) {
          void (*init)(Potion *) = dlsym(h, "Potion_Init_libp6");
          if (init) { init(P); libp6_loaded = 1; }
        }
      }
    }
    code = potion_syntax_parse(P, "p6", buf, filename);
    if (!code || PN_TYPE(code) == PN_TERROR) {
      p2_cmd_failed = 1;
      potion_p(P, code);
      return code;
    }
    code = potion_send(code, PN_compile, potion_str(P, filename), PN_NIL);
  } else {
    code = potion_syntax_parse(P, "p5", buf, filename);
    if (!code || PN_TYPE(code) == PN_TERROR) {
      p2_cmd_failed = 1;
      potion_p(P, code);
      return code;
    }
    code = potion_send(code, PN_compile, potion_str(P, filename), PN_NIL);
  }
  DBG_Pv(code);
  /* use same pattern as potion_run/front/potion.c's potion_cmd_exec:
   * decode the exec_mode_t enum properly from P->flags's low EXEC_BITS
   * bits -- EXEC_VM=0/EXEC_JIT=1/EXEC_DEBUG=2/EXEC_CHECK=3/EXEC_COMPILE=4
   * are NOT independent bit flags, 'P->flags & EXEC_JIT' (bitwise AND
   * with 1) only tests the low bit, so it was wrongly true for EXEC_VM
   * (0, intentionally false) and wrongly false for EXEC_COMPILE (4,
   * 0b100: low bit unset) -- the latter fell through to the VM-execute
   * branch below, overwriting 'code' (the compiled Proto that
   * p2_cmd_compile's caller needs to dump to a .plc file) with the
   * *executed result* instead, and EXEC_CHECK (-c) executed the script
   * instead of stopping after compilation. Also --inspect never printed
   * anything: DBG_Pvi() was defined but never called (see front/
   * potion.c for the reference implementation this was supposed to
   * mirror). */
  int in_p6_mode = (P->flags & MAX_SYNTAX) >= MODE_P6 && (P->flags & MAX_SYNTAX) < MODE_P6 + (1<<EXEC_BITS);
  exec_mode_t exec = in_p6_mode
    ? (exec_mode_t)((P->flags & MAX_SYNTAX) - MODE_P6)
    : (exec_mode_t)(P->flags & ((1<<EXEC_BITS)-1));
  if (exec == EXEC_VM || exec == EXEC_DEBUG) {
    code = potion_vm(P, code, P->lobby, PN_NIL, 0, NULL);
    DBG_Pvi(code);
  } else if (exec == EXEC_JIT) {
#ifdef POTION_JIT_TARGET
    PN val;
    PN cl = potion_closure_new(P, (PN_F)potion_jit_proto(P, code), PN_NIL, 1);
    PN_CLOSURE(cl)->data[0] = code;
    val = PN_PROTO(code)->jit(P, cl, P->lobby);
    DBG_Pvi(val);
    code = val;
#else
    fprintf(stderr, "** p2 built without JIT support\n");
#endif
  }
  /* EXEC_CHECK / EXEC_COMPILE: don't execute, leave 'code' as the
   * compiled Proto for the caller (p2_cmd_compile dumps it to bytecode;
   * -c just wanted to confirm it compiles). */
  return code;
}

static int p2_cmd_compile(Potion *P, char *filename, char *compile) {
  PN buf;
  int fd = -1;
  char *plcpath = NULL;
  struct stat stats;
  int rc = 0;
  exec_mode_t exec = (exec_mode_t)(P->flags & ((1<<EXEC_BITS)-1));

  if (stat(filename, &stats) == -1) {
    fprintf(stderr, "** %s does not exist.", filename);
    rc = 1;
    goto done;
  }
  fd = open(filename, O_RDONLY | O_BINARY);
  if (fd == -1) {
    fprintf(stderr, "** could not open %s. check permissions.", filename);
    rc = 1;
    goto done;
  }

  buf = potion_bytes(P, stats.st_size);
  if (read(fd, PN_STR_PTR(buf), stats.st_size) == stats.st_size) {
    PN code;
    PN_STR_PTR(buf)[stats.st_size] = '\0';

    /* .p6, .raku files: enable p6 mode. NOTE: .t is NOT forced here --
     * roast5 (plain Perl 5 spec tests) and roast6 (Raku spec tests, which
     * all start with 'use v6;') both use the generic .t extension; the
     * content-based 'use v6;'/'use p6' pragma (see syntax-p5.y's stmt
     * alternatives) is what switches a .t file to p6 mode, not the
     * filename. Forcing it here broke every roast5 .t file (fed straight
     * to the p6/pvip parser regardless of content). */
    const char *ext = strrchr(filename, '.');
    if (ext && (strcmp(ext, ".p6") == 0 || strcmp(ext, ".raku") == 0)) {
      /* keep the exec mode: forcing the JIT fails on a build without one */
      int in_p6 = (P->flags & MAX_SYNTAX) >= MODE_P6 && (P->flags & MAX_SYNTAX) < MODE_P6 + (1<<EXEC_BITS);
      int ex = in_p6 ? (int)((P->flags & MAX_SYNTAX) - MODE_P6) : (int)(P->flags & ((1<<EXEC_BITS)-1));
      P->flags = (P->flags & ~MAX_SYNTAX) | (MODE_P6 + ex);
    }

    code = p2_cmd_exec(P, buf, filename, compile);
    if (p2_cmd_failed || (code ? PN_TYPE(code) == PN_TERROR : exec >= EXEC_COMPILE)) {
      rc = 1;
      goto done;
    }

    if (exec == EXEC_COMPILE) {
      const char *backend = compile ? compile : "bc";
      const char *suffix;
      char *opts = compile ? strchr(compile, ',') : NULL;
      FILE *plc;
      size_t written, length;
      if (opts) *opts++ = '\0';
      if (!strcmp(backend, "bc")) suffix = "c";
      else if (!strcmp(backend, "c")) suffix = ".c";
      else if (!strcmp(backend, "exe")) suffix = ".out";
      else {
        fprintf(stderr, "** unknown compiler backend: %s\n", backend);
        rc = 1;
        goto done;
      }
      code = potion_source_dump(P, PN_NIL, code, potion_str(P, backend),
                                opts ? potion_str(P, opts) : PN_NIL);
      if (!code || (PN_TYPE(code) != PN_TBYTES && !PN_IS_STR(code))) {
        fprintf(stderr, "** %s compiler failed to emit code\n", backend);
        if (code && PN_TYPE(code) == PN_TERROR) potion_p(P, code);
        rc = 1;
        goto done;
      }
      if (!strcmp(backend, "exe")) {
        plcpath = malloc(strlen(filename) + 5);
        if (!plcpath) potion_allocation_error();
        strcpy(plcpath, filename);
        strcat(plcpath, suffix);
        {
          const char *shown = plcpath, *o = opts; /* -o=PATH among the compiler arguments names the executable */
          while (o && *o) {
            if (!strncmp(o, "-o=", 3)) { static char outp[1024]; size_t l = strcspn(o + 3, ","); snprintf(outp, sizeof outp, "%.*s", (int)(l < 1023 ? l : 1023), o + 3); shown = outp; }
            o = strchr(o, ','); if (o) o++;
          }
          if (potion_exe_build(P, code, plcpath, opts)) rc = 1;
          else printf("** executable saved to %s\n** run it with: %s%s\n", shown, shown[0] == '/' ? "" : "./", shown);
        }
        goto done;
      }
      plcpath = malloc(strlen(filename) + strlen(suffix) + 1);
      if (!plcpath) potion_allocation_error();
      strcpy(plcpath, filename);
      strcat(plcpath, suffix);
      plc = fopen(plcpath, "wb");
      if (!plc) {
        fprintf(stderr, "** could not open %s for writing. check permissions.\n", plcpath);
        rc = 1;
        goto done;
      }
      length = PN_STR_LEN(code);
      written = fwrite(PN_STR_PTR(code), 1, length, plc);
      if (written != length || ferror(plc)) {
        fprintf(stderr, "** could not write all %s compiled code (%lu/%lu) to %s\n",
                backend, (unsigned long)written, (unsigned long)length, plcpath);
        rc = 1;
      }
      if (fclose(plc) != 0) {
        fprintf(stderr, "** could not close %s after writing\n", plcpath);
        rc = 1;
      }
      if (rc) goto done;
      printf("** compiled code saved to %s\n", plcpath);
      if (!strcmp(backend, "bc"))
        printf("** run it with: p2 %s\n", plcpath);
      else if (!strcmp(backend, "c")) {
        int in_p6 = (P->flags & MAX_SYNTAX) >= MODE_P6
                 && (P->flags & MAX_SYNTAX) < MODE_P6 + (1<<EXEC_BITS);
        char libs[600];
#ifdef _WIN32 /* a -lp2 finds the static libp2.a before the dll: link the dlls by path, PATH finds them */
        snprintf(libs, sizeof libs, "lib/p2/libp5.dll %s lib/libp2.dll -lm", in_p6 ? "lib/p2/libp6.dll" : "");
#elif defined(__APPLE__) /* libp5 and libp6 are loadable bundles: the program finds them at run time */
        snprintf(libs, sizeof libs, "-Llib/p2 -Llib -Wl,-rpath,\"$PWD/lib/p2\" -Wl,-rpath,\"$PWD/lib\" -lp2 -lm");
#else
        snprintf(libs, sizeof libs, "-Llib/p2 %s -Llib -Wl,-rpath,\"$PWD/lib/p2\" -Wl,-rpath,\"$PWD/lib\" -lp2 -lm",
                 in_p6 ? "-lp6 -lp5" : "-lp5");
#endif
        printf("** from the build tree, compile it with: %s %s -Icore \"%s\" %s -o \"%s.out\"\n",
               POTION_CC, POTION_CFLAGS, plcpath, libs, filename);
        printf("** or build a loadable module: %s %s -Icore -DPOTION_MODULE -DPOTION_MODULE_NAME=compiled -fPIC -shared \"%s\" %s -o compiled%s\n",
               POTION_CC, POTION_CFLAGS, plcpath, libs, POTION_LOADEXT);
      } else
        printf("** run it with: ./%s\n", plcpath);
    }

#if defined(DEBUG)
    if (P->flags & DEBUG_GC) { // GC sanity check
      fprintf(stderr, "\n-- gc check --\n");
      void *scanptr = (void *)((char *)P->mem->old_lo + (sizeof(PN) * 2));
      while ((PN)scanptr < (PN)P->mem->old_cur) {
	fprintf(stderr, "%p.vt = %x (%u)\n",
		scanptr, ((struct PNObject *)scanptr)->vt,
		potion_type_size(P, scanptr));
	if (((struct PNFwd *)scanptr)->fwd != POTION_FWD
	    && ((struct PNFwd *)scanptr)->fwd != POTION_COPIED) {
	  PNType vt = ((struct PNObject *)scanptr)->vt;
	  if ((signed int)vt < 0 || vt > PN_TUSER) {
	    fprintf(stderr, "** wrong type for allocated object: %p.vt = %x\n",
		    scanptr, vt);
	    break;
	  }
	}
	scanptr = (void *)((char *)scanptr + potion_type_size(P, scanptr));
	if ((PN)scanptr > (PN)P->mem->old_cur) {
	  fprintf(stderr, "** allocated object goes beyond GC pointer\n");
	  break;
	}
      }
    }
#endif

  } else {
    fprintf(stderr, "** could not read entire file.");
    rc = 1;
  }

done:
  free(plcpath);
  if (fd != -1)
    close(fd);
  return rc;
}

struct p2sw { int chomp, autosplit, xskip, loop; char *fsplit; };
/* perl style switch bundle (the text after the '-'): -n -p -a -F -l -0 -x -i -w. Stops at -e/-E and returns
 * its position for the caller (the code argument), NULL at the end; *bad is set for an unknown switch */
static char *p2_switches(Potion *P, char *a, struct p2sw *sw, int *bad) {
  while (*a) {
    switch (*a) {
    case 'n': sw->loop = 1; a++; break;
    case 'p': sw->loop = 2; a++; break;
    case 'a': sw->autosplit = 1; if (!sw->loop) sw->loop = 1; a++; break;
    case 'w': case 'W': case 'X': a++; break; /* warnings: accepted and ignored */
    case 'F': /* -Fpattern: split on it, implies -a -n */
      sw->autosplit = 1; if (!sw->loop) sw->loop = 1;
      sw->fsplit = a + 1;
      a += strlen(a);
      break;
    case 'l': { /* -l[octal]: chomp the input lines, $\ = that character (default: the current $/) */
      char *e;
      long v = strtol(a + 1, &e, 8);
      char ors[2] = { '\n', 0 };
      sw->chomp = 1;
      if (e != a + 1) ors[0] = (char)v;
      potion_define_global(P, potion_str(P, "$\\"), potion_str2(P, ors, 1));
      a = e;
      break;
    }
    case '0': { /* -0[octal|xHEX]: the input record separator; -00 paragraph mode, -0777 slurp, -0 NUL */
      char *e;
      int hex = a[1] == 'x' || a[1] == 'X';
      long v = hex ? strtol(a + 2, &e, 16) : strtol(a + 1, &e, 8);
      if (!hex && e == a + 2 && a[1] == '0') potion_define_global(P, potion_str(P, "$/"), potion_str(P, ""));
      else if (v >= 0400) potion_define_global(P, potion_str(P, "$/"), PN_NIL);
      else { char rs[2] = { (char)v, 0 }; potion_define_global(P, potion_str(P, "$/"), potion_str2(P, rs, 1)); }
      a = e;
      break;
    }
    case 'x': sw->xskip = 1; a += strlen(a); break; /* -x[dir]: the script starts at the first #!..perl line */
    case 'i':
      potion_define_global(P, potion_str(P, "$^I"), potion_str(P, a + 1)); /* the rest is the backup extension */
      a += strlen(a);
      break;
    case 'e': case 'E': return a;
    default: *bad = 1; return NULL;
    }
  }
  return NULL;
}

/* ---- the variables of the interactive loop (p2 -d) ----
 * Every eval STRING is its own scope, so a variable would be gone with the next line. The session
 * keeps a table, "$x" "@a" "%h" => value, and each line is rewritten before it is evaluated:
 *  - a scalar `$x` becomes `$__S->{'$x'}` (the table is shared, so an assignment is kept at once,
 *    also when the line dies, and a closure made on one line sees the later values: an upvalue)
 *  - an array or hash the line mentions is loaded into a lexical before the line and stored back
 *    after it, also after a die (a whole array assignment cannot go through the table)
 *  - `my`/`our` at the start of a top level statement is dropped, so the variable is the session's;
 *    a `my` inside a block or a foreach stays a real lexical (locals, and what closures capture)
 * One line subs are defined again in front of every line. */
static PN repl_vars; /* "$x" "@a" "%h" => value */
static char repl_subs[8192];

static PN repl_state(Potion *P, PN cl, PN self) { return repl_vars; } /* repl_get('$') is the same */
static PN repl_get(Potion *P, PN cl, PN self, PN name) {
  PN v = PN_STR_PTR(name)[0] == '$' && !PN_STR_PTR(name)[1] ? repl_vars : potion_table_at(P, PN_NIL, repl_vars, name);
  if (v == PN_NIL && PN_STR_PTR(name)[0] == '@') return PN_TUP0();
  if (v == PN_NIL && PN_STR_PTR(name)[0] == '%') return potion_table_empty(P);
  return v;
}
static PN repl_set(Potion *P, PN cl, PN self, PN name, PN val) {
  potion_table_set(P, repl_vars, name, val);
  return val;
}

static int repl_word(int c) { return isalnum((unsigned char)c) || c == '_'; }

static int repl_has(char l[][64], int n, const char *key) {
  for (int i = 0; i < n; i++) if (!strcmp(l[i], key)) return 1;
  return 0;
}

/* the names declared with `my` in the line: my $x, my ($a, @b) */
static int repl_declared(const char *s, char d[][64], int max) {
  int n = 0;
  for (size_t i = 0; s[i]; i++) {
    if (strncmp(s + i, "my", 2) || (i && repl_word(s[i - 1])) || (s[i + 2] != ' ' && s[i + 2] != '\t')) continue;
    size_t q = i + 2;
    int list = 0;
    while (s[q] == ' ' || s[q] == '\t') q++;
    if (s[q] == '(') { list = 1; q++; }
    for (;;) {
      while (s[q] == ' ' || s[q] == '\t' || s[q] == ',') q++;
      if (s[q] != '$' && s[q] != '@' && s[q] != '%') break;
      size_t k = q + 1;
      while (repl_word(s[k])) k++;
      if (k - q > 1 && k - q < 40 && n < max) snprintf(d[n++], 64, "%c%.*s", s[q], (int)(k - q - 1), s + q + 1);
      q = k;
      if (!list) break;
    }
    i = q > i ? q - 1 : i;
  }
  return n;
}

/* S rewritten into OUT (see above); NAMES gets the arrays and hashes the line mentions */
static int repl_rewrite(const char *s, char *out, size_t cap, char names[][64], int max) {
  static const char *skip[] = { "_", "ENV", "ARGV", "INC", "STDIN", "STDOUT", "STDERR", "__r", "__e", "__S", NULL };
  char decl[32][64];
  int nd = repl_declared(s, decl, 32), n = 0;
  size_t o = 0;
  int sq = 0, dq = 0, inexpr = 0; /* inside '...' (variables are text) or "..." (they interpolate and are rewritten), and inside "@{[ ]}" */
  for (size_t i = 0; s[i] && o + 100 < cap; i++) {
    char c = s[i], sig, key[64];
    size_t j = i + 1, k;
    int hash = 0, ok = 1, p;
    if (sq) { /* '...': no interpolation, copied as it is */
      out[o++] = c;
      if (c == '\\' && s[i + 1]) out[o++] = s[++i];
      else if (c == '\'') sq = 0;
      continue;
    }
    if (dq && c == '\\' && s[i + 1]) { out[o++] = c; out[o++] = s[++i]; continue; } /* "\$x" is text */
    if (!dq && c == '\'' && (i == 0 || !repl_word(s[i - 1]))) { sq = 1; out[o++] = c; continue; }
    if (dq && !strncmp(s + i, "@{[", 3)) { memcpy(out + o, s + i, 3); o += 3; i += 2; inexpr = 1; continue; }
    if (dq && inexpr && s[i] == ']' && s[i + 1] == '}') { out[o++] = ']'; out[o++] = '}'; i++; inexpr = 0; continue; }
    if (c == '"') { dq = !dq; inexpr = 0; out[o++] = c; continue; }
    if (c != '$' && c != '@' && c != '%') { out[o++] = c; continue; }
    if (c == '$' && s[j] == '#') { hash = 1; j++; }
    if (!(isalpha((unsigned char)s[j]) || s[j] == '_')) { out[o++] = c; continue; }
    for (k = j; repl_word(s[k]); k++);
    if (k - j > 40) { out[o++] = c; continue; }
    if (c == '%') { /* a hash, not the modulus: after an operator, or a keys/values/each */
      size_t b = i;
      while (b > 0 && s[b - 1] == ' ') b--;
      ok = b == 0 || strchr("=(,;{[!", s[b - 1]) != NULL
           || (b >= 4 && !strncmp(s + b - 4, "keys", 4)) || (b >= 6 && !strncmp(s + b - 6, "values", 6))
           || (b >= 4 && !strncmp(s + b - 4, "each", 4)) || (b >= 6 && !strncmp(s + b - 6, "exists", 6))
           || (b >= 6 && !strncmp(s + b - 6, "delete", 6));
    }
    sig = hash ? '@' : c;
    if (c == '$' && !hash && s[k] == '[') sig = '@';
    else if (c == '$' && !hash && s[k] == '{') sig = '%';
    else if (c == '@' && s[k] == '{') sig = '%'; /* @h{'a','b'}: a hash slice */
    else if (c == '%' && s[k] == '[') sig = '@'; /* %a[0,1]: an index/value slice */
    snprintf(key, sizeof key, "%c%.*s", sig, (int)(k - j), s + j);
    for (p = 0; skip[p]; p++) if (!strcmp(key + 1, skip[p])) break;
    if (!ok || skip[p] || repl_has(decl, nd, key) || (sig == '$' && key[2] == '\0' && (key[1] == 'a' || key[1] == 'b'))) {
      memcpy(out + o, s + i, k - i); o += k - i; /* untouched */
    } else if (sig == '$') {
      /* in "..." the quoted key does not interpolate in p2: use @{[ EXPR ]}, unless already inside one */
      o += snprintf(out + o, cap - o, dq && !inexpr ? "@{[ $__S->{'%s'} ]}" : "$__S->{'%s'}", key);
    } else {
      if (!repl_has(names, n, key) && n < max) strcpy(names[n++], key);
      memcpy(out + o, s + i, k - i); o += k - i;
    }
    i = k - 1;
  }
  out[o] = '\0';
  return n;
}

/* LINE with the declarations at the start of a top level statement removed: my $x = 1; => $x = 1; */
static void repl_strip_my(const char *s, char *out) {
  size_t o = 0;
  int stmt = 1, depth = 0;
  for (size_t i = 0; s[i]; i++) {
    if (stmt) {
      while (s[i] == ' ' || s[i] == '\t') out[o++] = s[i++];
      const char *kw = !strncmp(s + i, "my", 2) ? "my" : !strncmp(s + i, "our", 3) ? "our" : NULL;
      if (kw && depth == 0) {
        size_t l = strlen(kw), q = i + l;
        if ((s[q] == ' ' || s[q] == '\t') ) {
          while (s[q] == ' ' || s[q] == '\t') q++;
          if (s[q] == '$' || s[q] == '@' || s[q] == '%' || s[q] == '(') i = q;
        }
      }
      stmt = 0;
      if (!s[i]) break;
    }
    out[o++] = s[i];
    if (s[i] == '{') depth++;
    else if (s[i] == '}' && depth > 0) depth--;
    if (s[i] == ';') stmt = 1;
  }
  out[o] = '\0';
}

/* a line with an unclosed ( [ { or an unterminated quote continues on the next input line */
static int repl_open(const char *s) {
  int d = 0;
  char q = 0;
  for (size_t i = 0; s[i]; i++) {
    char c = s[i];
    if (q) { if (c == '\\' && s[i + 1]) i++; else if (c == q) q = 0; continue; }
    if (c == '\'' || c == '"') { q = c; continue; }
    if (c == '#' && (i == 0 || strchr(" \t\n;", s[i - 1]))) { while (s[i] && s[i] != '\n') i++; if (!s[i]) break; continue; }
    if (c == '(' || c == '[' || c == '{') d++;
    else if (c == ')' || c == ']' || c == '}') d--;
  }
  return d > 0 || q;
}

/* `local $x`, `local @a`, `local $h{k}` of a session variable: the variable is saved before the line and
 * restored after it (also after a die); the word local is dropped. Perl restores at the end of the
 * enclosing block, here it is the end of the input. LK gets the keys, OUT the text without local. */
static int repl_locals(const char *s, char *out, char lk[][64], int max) {
  static const char *skip[] = { "_", "ENV", "ARGV", "INC", "STDIN", "STDOUT", "STDERR", "__r", "__e", "__S", NULL };
  int n = 0;
  size_t o = 0;
  for (size_t i = 0; s[i]; i++) {
    if (!strncmp(s + i, "local", 5) && (i == 0 || !repl_word(s[i - 1])) && (s[i + 5] == ' ' || s[i + 5] == '\t')) {
      size_t q = i + 5, k;
      char key[64], sig, ks;
      int p;
      while (s[q] == ' ' || s[q] == '\t') q++;
      sig = s[q];
      if ((sig == '$' || sig == '@' || sig == '%') && (isalpha((unsigned char)s[q + 1]) || s[q + 1] == '_')) {
        for (k = q + 1; repl_word(s[k]); k++);
        ks = sig;
        if (sig == '$') { if (s[k] == '[') ks = '@'; else if (s[k] == '{') ks = '%'; }
        if (k - q - 1 < 40) {
          snprintf(key, sizeof key, "%c%.*s", ks, (int)(k - q - 1), s + q + 1);
          for (p = 0; skip[p]; p++) if (!strcmp(key + 1, skip[p])) break;
          if (!skip[p] && !(ks == '$' && key[2] == '\0' && (key[1] == 'a' || key[1] == 'b'))) {
            if (!repl_has(lk, n, key) && n < max) strcpy(lk[n++], key);
            i = q - 1; /* "local " is dropped */
            continue;
          }
        }
      }
    }
    out[o++] = s[i];
  }
  out[o] = '\0';
  return n;
}

/* the one line (or several, a multi line sub) subs defined so far and the arrays and hashes they use: the
 * subs are defined again in front of every line, so these are loaded in front of them as well */
static char repl_subnames[64][64];
static int repl_nsubn;
static char repl_pend[16384]; /* the sub of the input being evaluated, kept when it compiled */
static char repl_pendn[64][64];
static int repl_npend;
static void repl_commit(void) {
  if (repl_pend[0] && strlen(repl_subs) + strlen(repl_pend) + 2 < sizeof repl_subs) {
    strcat(repl_subs, repl_pend); strcat(repl_subs, "\n");
    for (int i = 0; i < repl_npend; i++)
      if (!repl_has(repl_subnames, repl_nsubn, repl_pendn[i]) && repl_nsubn < 64) strcpy(repl_subnames[repl_nsubn++], repl_pendn[i]);
  }
  repl_pend[0] = '\0'; repl_npend = 0;
}

static char *repl_wrap(const char *line) {
  static char buf[65536], rw[16384];
  char names[64][64], lk[16][64], all[128][64], body[8192], body2[8192];
  const char *t = line;
  int n, nl, na = 0, i;
  size_t o = 0;
  repl_pend[0] = '\0'; repl_npend = 0;
  repl_strip_my(line, body);
  nl = repl_locals(body, body2, lk, 16);
  n = repl_rewrite(body2, rw, sizeof rw, names, 64);
  while (*t == ' ' || *t == '\t') t++;
  if (!strncmp(t, "sub ", 4) && repl_word(t[4]) && line[strlen(line) - 1] == '}' && strlen(rw) < sizeof repl_pend) {
    memcpy(repl_pend, rw, strlen(rw) + 1); /* kept for the next inputs when it compiled (repl_commit) */
    for (i = 0; i < n && i < 64; i++) strcpy(repl_pendn[repl_npend++], names[i]);
  }
  for (i = 0; i < n; i++) strcpy(all[na++], names[i]);
  for (i = 0; i < repl_nsubn && na < 128; i++) if (!repl_has(all, na, repl_subnames[i])) strcpy(all[na++], repl_subnames[i]);
  o += snprintf(buf + o, sizeof buf - o, "my $__S = repl_get('$'); ");
  for (i = 0; i < na; i++)
    o += snprintf(buf + o, sizeof buf - o, "my %s = repl_get('%s'); ", all[i], all[i]);
  o += snprintf(buf + o, sizeof buf - o, "%s", repl_subs);
  for (i = 0; i < nl; i++) /* the values to restore */
    o += snprintf(buf + o, sizeof buf - o, "my $__l%d = %s%s%s; ", i,
                  lk[i][0] == '$' ? "$__S->{'" : lk[i][0] == '@' ? "[" : "{",
                  lk[i][0] == '$' ? lk[i] : lk[i], lk[i][0] == '$' ? "'}" : lk[i][0] == '@' ? "]" : "}");
  o += snprintf(buf + o, sizeof buf - o, "my $__r = eval { %s\n}; my $__e = $@; ", rw);
  for (i = 0; i < na; i++)
    o += snprintf(buf + o, sizeof buf - o, "repl_set('%s', %s%s%s); ", all[i],
                  all[i][0] == '@' ? "[" : "{", all[i], all[i][0] == '@' ? "]" : "}");
  for (i = 0; i < nl; i++) {
    if (lk[i][0] == '$') o += snprintf(buf + o, sizeof buf - o, "$__S->{'%s'} = $__l%d; ", lk[i], i);
    else o += snprintf(buf + o, sizeof buf - o, "repl_set('%s', $__l%d); ", lk[i], i);
  }
  snprintf(buf + o, sizeof buf - o, "die $__e if $__e; $__r");
  return buf;
}

int main(int argc, char *argv[]) {
  POTION_INIT_STACK(sp);
  potion_set_argv0(argv[0]);
  Potion *P = potion_create(sp);
  int i, exec = POTION_JIT ? EXEC_JIT : EXEC_VM;
  PN buf = PN_NIL;
  char *compile = NULL;
  char *fn = NULL;
  int rc = 0;
  char *srcname = NULL;
  struct p2sw sw = { 0, 0, 0, 0, NULL }; /* -l -a -x -n/-p (while (<>) { CODE }, -p with a continue { print }) -F */
  char *fcopy = NULL; /* the shebang line's copy that -F's pattern points into */

#if defined(STATIC) || defined(SANDBOX)
  Potion_Init_readline(P);
  Potion_Init_aio(P);
#ifndef SANDBOX
  Potion_Init_buffile(P);
#endif
  Potion_Init_libp5(P);
#else
  /* the p5 natives (file handles, map/grep/sort, @_) live in libp5, not the core */
  p5_load(P);
#endif

  for (i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--")) { i++; break; }
    if (!strncmp(argv[i], "-I", 2)) {
#ifdef SANDBOX
      potion_fatal("-I disabled in SANDBOX");
#else
      char *extra_path = &argv[i][2]; // todo: flexible
      if (*extra_path)
	potion_loader_add(P, potion_str(P, extra_path));
      else {
	if (i == argc) {
	    fprintf(stderr, "-I missing directory\n");
	    goto END;
	}
	potion_loader_add(P, potion_str(P, argv[i+1]));
	i++;
      }
      continue;
#endif
    }
    if (!strcmp(argv[i], "--inspect")) {
      P->flags |= DEBUG_INSPECT; continue; }
    if (!strcmp(argv[i], "-V") || !strcmp(argv[i], "--verbose")) {
      P->flags |= DEBUG_VERBOSE; continue; }
#ifdef DEBUG
    if (argv[i][0] == '-' && argv[i][1] == 'D') {
      if (strchr(&argv[i][2], '?')) {
	printf("-D debugging flags:\n");
	printf("  i  inspect\n");
	printf("  v  verbose\n");
	printf("  t  trace\n");
	printf("  c  compile\n");
	printf("  p  parse\n");
	printf("  P  parse verbose\n");
	printf("  J  Jit\n");
	printf("  G  GC\n");
	goto END;
      }
      if (strchr(&argv[i][2], 'i')) P->flags |= DEBUG_INSPECT;
      if (strchr(&argv[i][2], 'v')) P->flags |= DEBUG_VERBOSE;
      if (strchr(&argv[i][2], 't')) { P->flags |= DEBUG_TRACE;
	exec = exec==EXEC_JIT ? EXEC_VM : exec; }
      if (strchr(&argv[i][2], 'p')) P->flags |= DEBUG_PARSE;
      if (strchr(&argv[i][2], 'P')) P->flags |= DEBUG_PARSE_VERBOSE;
      if (strchr(&argv[i][2], 'c')) P->flags |= DEBUG_COMPILE;
      if (strchr(&argv[i][2], 'J')) P->flags |= DEBUG_JIT;
      if (strchr(&argv[i][2], 'G')) P->flags |= DEBUG_GC;
      continue;
    }
#endif
    if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--version")) {
      p2_cmd_version(P); goto END; }
    if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
      p2_cmd_usage(P); goto END; }
    if (!strcmp(argv[i], "--stats")) { // TODO: afterwards
      p2_cmd_stats(P); goto END; }
    if (!strncmp(argv[i], "--compile=", 10)) {
      exec = EXEC_COMPILE; compile = &argv[i][10]; continue; }
    if (!strcmp(argv[i], "--compile")) {
      exec = EXEC_COMPILE; compile = NULL; continue; }
    if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "--check")) {
      exec = EXEC_CHECK; continue; }
    if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--debug")) {
      exec = EXEC_DEBUG | EXEC_VM; continue; }
    if (!strcmp(argv[i], "-B") || !strcmp(argv[i], "--bytecode")) {
      exec = EXEC_VM; continue; }
    if (!strcmp(argv[i], "-J") || !strcmp(argv[i], "--jit")) {
      exec = EXEC_JIT; continue; }
    if (argv[i][0] == '-') {
      if (argv[i][1] == '6') { // extend later to -6:something?
        if (strlen(argv[i]) == 2) {
          potion_define_global(P, potion_str(P, "$0"), potion_str(P, "-6"));
          P->flags = (P->flags & ~MAX_SYNTAX) + MODE_P6;
          continue;
        }
      }
      /* perl style switch bundle: -n -p -i[ext] -e code -E code, e.g. -pe, -ne, -pi.bak -e */
      {
        int bad = 0;
        char *a = p2_switches(P, argv[i] + 1, &sw, &bad);
        if (a && !bad) { /* -e code / -E code: the code is the rest of the bundle or the next argument */
          char *arg = NULL;
          int big = *a == 'E';
          if (a[1]) arg = a + 1;
          else if (i + 1 < argc) arg = argv[++i];  /* the code is not a script argument */
          potion_define_global(P, potion_str(P, "$0"), potion_str(P, big ? "-E" : "-e"));
          if (arg) buf = potion_str(P, arg);
          if (big) P->flags = (P->flags & ~MAX_SYNTAX) + MODE_P2;
        }
        if (!bad) continue;
        fprintf(stderr, "** Unrecognized option: %s\n", argv[i]);
      }
    }
    else {
      break;
    }
  }
  P->flags += exec;

  potion_define_global(P, potion_str(P, "$P2::mode"), PN_NUM((P->flags & 0xff))); // first flags word: p5,p2,p6
  potion_define_global(P, potion_str(P, "$P2::execmode"), PN_NUM(exec)); // exec_jit, exec_vm
  potion_define_global(P, potion_str(P, "$P2::verbose"),  PN_NUM((P->flags & ~0xff) >> 8)); // second flags word
  {
    char *exe = argv[0];
#if !defined(_WIN32) && !defined(WIN32)
    char rp[PATH_MAX];
    if (strchr(argv[0], '/') && realpath(argv[0], rp)) exe = rp; /* tests chdir and run $^X again */
#endif
    potion_define_global(P, potion_str(P, "$^X"), potion_str(P, exe));
  }

  if (buf == PN_NIL && !compile && ((i < argc && !strcmp(argv[i], "-")) || (i >= argc && !isatty(0)))) {
    /* the program is read from stdin: `p2 < file`, `p2 -` */
    size_t cap = 4096, len = 0, n;
    char *txt = malloc(cap);
    if (!txt) potion_allocation_error();
    while ((n = fread(txt + len, 1, cap - len, stdin)) > 0) {
      len += n;
      if (len == cap) { cap *= 2; txt = realloc(txt, cap); if (!txt) potion_allocation_error(); }
    }
    if (i < argc) i++;
    buf = potion_byte_str2(P, txt, len);
    free(txt);
    srcname = "-";
    potion_define_global(P, PN_STR("$0"), PN_STR("-"));
  }

  if (buf || i < argc) {
    potion_define_global(P, potion_str(P, "$P2::interactive"), PN_NIL);
    PN args = PN_TUP0();
    if (buf == PN_NIL) fn = argv[i++];
    for (; i < argc; i++) PN_PUSH(args, PN_STR(argv[i]));
    potion_define_global(P, PN_STR("@ARGV"), args);
    if (buf == PN_NIL && fn && !sw.xskip) { /* #!/usr/bin/perl -n -l0: the switches of the script's first line */
      FILE *fp = fopen(fn, "rb");
      char line[512];
      if (fp) {
        if (fgets(line, sizeof line, fp) && line[0] == '#' && line[1] == '!' && (strstr(line, "perl") || strstr(line, "p2"))) {
          char *t = strpbrk(line, " \t"); /* after the interpreter path */
          int bad = 0;
          while (t && *t && !bad) {
            char *e;
            int last;
            while (*t == ' ' || *t == '\t') t++;
            if (*t != '-') break;
            e = t + strcspn(t, " \t\r\n");
            last = *e == 0;
            *e = 0;
            if (strcmp(t, "--")) {
              char *copy = strdup(t + 1), *r = p2_switches(P, copy, &sw, &bad); /* -e is not allowed there, and -F's text must live on */
              if (r) bad = 1;
              if (sw.fsplit >= copy && sw.fsplit <= copy + strlen(copy)) { free(fcopy); fcopy = copy; }
              else free(copy);
            }
            if (last) break;
            t = e + 1;
          }
        }
        fclose(fp);
      }
    }
    if ((sw.loop || sw.xskip) && buf == PN_NIL && fn) { /* -n/-p/-x with a script file: read its text */
      FILE *fp = fopen(fn, "rb");
      long sz;
      char *txt;
      if (!fp) { fprintf(stderr, "** Can't open perl script \"%s\"\n", fn); rc = 2; goto END; }
      fseek(fp, 0, SEEK_END); sz = ftell(fp); fseek(fp, 0, SEEK_SET);
      txt = malloc(sz + 1);
      if (!txt || fread(txt, 1, sz, fp) != (size_t)sz) { fclose(fp); fprintf(stderr, "** Can't read %s\n", fn); rc = 2; goto END; }
      fclose(fp);
      txt[sz] = 0;
      if (sw.xskip) { /* skip the leading garbage up to a #! line that names perl or p2 */
        char *l = txt, *hit = NULL;
        while (l && *l) {
          char *nl = strchr(l, '\n');
          if (l[0] == '#' && l[1] == '!' && (strstr(l, "perl") || strstr(l, "p2")) && (!nl || strstr(l, "perl") < nl || strstr(l, "p2") < nl)) { hit = l; break; }
          l = nl ? nl + 1 : NULL;
        }
        if (!hit) { fprintf(stderr, "** No Perl script found in input\n"); rc = 2; free(txt); goto END; }
        sz -= hit - txt;
        memmove(txt, hit, sz + 1);
      }
      buf = potion_byte_str2(P, txt, sz);
      free(txt);
      potion_define_global(P, PN_STR("$0"), PN_STR(fn));
      srcname = fn;
    }
    if (sw.loop && buf != PN_NIL) {
      char split[512] = "";
      if (sw.autosplit) {
        const char *f = sw.fsplit && *sw.fsplit ? sw.fsplit : NULL;
        if (!f) snprintf(split, sizeof split, "@F = split(' ', $_);\n");
        else if (*f == '/' || *f == '"' || *f == '\'') snprintf(split, sizeof split, "@F = split(%s, $_);\n", f);
        else snprintf(split, sizeof split, "@F = split(/%s/, $_);\n", f);
      }
      {
        const char *src = PN_STR_PTR(buf), *d1 = NULL, *d2 = NULL, *dat;
        char *code = strdup(src);
        if (!code) potion_allocation_error();
        /* the __DATA__ / __END__ text stays behind the loop */
        d1 = !strncmp(src, "__DATA__", 8) ? src : strstr(src, "\n__DATA__");
        d2 = !strncmp(src, "__END__", 7) ? src : strstr(src, "\n__END__");
        dat = d1 && (!d2 || d1 < d2) ? d1 : d2;
        if (dat) { code[dat - src + (dat == src ? 0 : 1)] = 0; dat = dat + (dat == src ? 0 : 1); }
        /* BEGIN blocks run at compile time, in front of the loop: move them out of it */
        {
          size_t n = strlen(code);
          char *pre = calloc(n + 1, 1), *body = calloc(n + 1, 1), *l = code;
          if (!pre || !body) potion_allocation_error();
          while (*l) {
            char *nl = strchr(l, '\n'), *e = nl ? nl + 1 : l + strlen(l), *q;
            int hoist = !strncmp(l, "BEGIN", 5) && (l[5] == ' ' || l[5] == '{' || l[5] == '\t');
            if (hoist) {
              int depth = 0, started = 0, quote = 0;
              for (q = l; *q; q++) {
                if (quote) { if (*q == '\\' && q[1]) q++; else if (*q == quote) quote = 0; }
                else if (*q == '\'' || *q == '"') quote = *q;
                else if (*q == '#') { while (*q && *q != '\n') q++; if (!*q) break; }
                else if (*q == '{') { depth++; started = 1; }
                else if (*q == '}') { depth--; }
                if (started && depth == 0) { q++; while (*q && *q != '\n') q++; if (*q) q++; break; }
              }
              strncat(pre, l, q - l);
              l = q;
            } else {
              strncat(body, l, e - l);
              l = e;
            }
          }
          buf = potion_str_format(P, "%s\nwhile (<>) {\n%s%s%s\n}%s\n%s", pre, sw.chomp ? "chomp;\n" : "", split, body,
                                  sw.loop == 2 ? " continue { print; }" : "", dat ? dat : "");
          free(pre); free(body);
        }
        free(code);
      }
    }
    if (buf != PN_NIL) {
      if (srcname == NULL) potion_define_global(P, PN_STR("$0"), PN_STR("-e"));
      {
        PN code = p2_cmd_exec(P, buf, srcname ? srcname : "-e", compile);
        if (p2_cmd_failed || (code && PN_TYPE(code) == PN_TERROR)) rc = 1;
      }
    }
    else {
      potion_define_global(P, PN_STR("$0"), PN_STR(fn));
      rc = p2_cmd_compile(P, fn, compile);
    }
  } else {
    if (!PN_DEBUGGING(P)) { /* the interactive loop is the debugger: p2 -d */
      p2_cmd_usage(P);
      goto END;
    }
    /* the loop itself is the debugger: evaluate the lines with the plain VM, else the c debugger
     * (potion_debug) would read the following lines of input */
    P->flags = (Potion_Flags)(P->flags & ~((1 << EXEC_BITS) - 1));
    if (P->flags & DEBUG_INSPECT) potion_fatal("no filename given");
    potion_define_global(P, PN_STR("$0"), PN_STR("-i"));
    repl_vars = potion_table_empty(P);
    potion_gc_root(P, &repl_vars);
    potion_method(P->lobby, "repl_state", repl_state, 0);
    potion_method(P->lobby, "repl_get", repl_get, "name=S");
    potion_method(P->lobby, "repl_set", repl_set, "name=S,val=o");
    potion_define_global(P, potion_str(P, "$P2::interactive"), PN_NUM(1));
    puts("p2 -d: q or ^D quits, h shows this help, any other line is evaluated as perl5, :subs :classes :methods CLASS :b N :B N :bl");
#ifndef SANDBOX
#ifndef STATIC
    { void *h = potion_dlopen_find(P, "readline");
      void (*init)(Potion *) = h ? (void (*)(Potion *))dlsym(h, "Potion_Init_readline") : NULL;
      if (!init) { fprintf(stderr, "** the readline module is not found\n"); rc = 1; goto END; }
      init(P); }
#endif
    /* read-eval-print loop: each line is evaluated as p5 code, `q` or EOF leaves. The variables
     * and one line subs persist, the line is rewritten first (repl_wrap). */
    for (;;) {
      PN line = potion_send(P->lobby, potion_str(P, "readline"), potion_str(P, ">> "));
      if (!line || !PN_IS_STR(line)) { putchar('\n'); break; }
      if (!strcmp(PN_STR_PTR(line), "q")) break;
      if (!strcmp(PN_STR_PTR(line), "h")) { puts("q or ^D quits, h shows this help, :subs lists the subs, :classes the classes, :methods CLASS its methods, :b N / :B N / :bl set, delete and list breakpoints (for a script run with p2 -d file), any other line is evaluated as perl5"); continue; }
      if (PN_STR_PTR(line)[0] == ':') {
        if (!potion_debug_cmd(P, PN_STR_PTR(line))) puts("unknown command, h for help");
        continue;
      }
      if (!PN_STR_LEN(line)) continue;
      static char acc[32768]; /* the input, several lines when brackets or a string are open */
      snprintf(acc, sizeof acc, "%s", PN_STR_PTR(line));
      while (repl_open(acc)) {
        PN more = potion_send(P->lobby, potion_str(P, "readline"), potion_str(P, ".. "));
        if (!more || !PN_IS_STR(more) || !PN_STR_LEN(more)) break; /* EOF or an empty line ends it */
        if (strlen(acc) + PN_STR_LEN(more) + 2 >= sizeof acc) break;
        strcat(acc, "\n"); strcat(acc, PN_STR_PTR(more));
      }
      potion_define_global(P, PN_STR("$@"), PN_STR0);
      PN code = potion_str(P, repl_wrap(acc));
      PN obj = potion_send(code, potion_str(P, "p5evalstr"));
      if (obj == repl_vars) { /* `$x = 5` as an expression yields the container: show the variable */
        const char *l = acc;
        char key[64];
        size_t k = 0;
        while (*l == ' ' || *l == '\t') l++;
        if (!strncmp(l, "my ", 3) || !strncmp(l, "our ", 4)) l += l[1] == 'y' ? 3 : 4;
        if (*l == '$') { key[k++] = *l++; while (repl_word(*l) && k < 62) key[k++] = *l++; }
        key[k] = '\0';
        obj = k > 1 ? potion_table_at(P, PN_NIL, repl_vars, potion_str(P, key)) : PN_NIL;
      }
      PN err = potion_send(P->lobby, potion_str(P, "$@"));
      if (PN_IS_STR(err) && PN_STR_LEN(err)) repl_pend[0] = 0; else repl_commit(); /* a sub that did not compile is not kept */
      if (PN_IS_STR(err) && PN_STR_LEN(err)) {
        fputs(PN_STR_PTR(err), stdout);
        if (PN_STR_PTR(err)[PN_STR_LEN(err) - 1] != '\n') putchar('\n');
      } else if (obj == PN_NIL) {
        puts("=> undef");
      } else {
        PN s = potion_send(obj, PN_string);
        printf("=> %s\n", PN_IS_STR(s) ? PN_STR_PTR(s) : "");
      }
      fflush(stdout);
    }
#endif /* SANDBOX */
  }
END:
  free(fcopy);
#if !defined(POTION_JIT_TARGET) || defined(DEBUG) || defined(__SANITIZE_ADDRESS__)
  if (P != NULL)
    potion_destroy(P);
#endif
  return rc;
}
