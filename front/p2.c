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
      "  --compile={c,exe}  load compiler backend and compile: C or exe\n" // or jvm, .net
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
        void *h = dlopen(potion_find_file(P, "libp6", 5), RTLD_LAZY);
        if (h) {
          void (*init)(Potion *) = dlsym(h, "Potion_Init_libp6");
          if (init) { init(P); libp6_loaded = 1; }
        }
      }
    }
    code = potion_syntax_parse(P, "p6", buf, filename);
    if (!code || PN_TYPE(code) == PN_TERROR) {
      potion_p(P, code);
      return code;
    }
    code = potion_send(code, PN_compile, potion_str(P, filename), PN_NIL);
  } else {
    code = potion_syntax_parse(P, "p5", buf, filename);
    if (!code || PN_TYPE(code) == PN_TERROR) {
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
    if (!code || PN_TYPE(code) == PN_TERROR) {
      rc = 1;
      goto done;
    }

    if (exec >= EXEC_COMPILE) { // needs an inputfile. TODO: -e"" -ofile
      char plcpath[255];
      FILE *plc;
      char *opts = NULL;
      if (compile) { // --compile=c[,OPTS]
	if ((opts = strchr(compile,','))) {
	  opts[0] = '\0';
	  opts++;
	}
      }
      if (exec == EXEC_COMPILE) {
        if (!compile || !strcmp(compile, "bc"))
	  sprintf(plcpath, "%sc", filename);  // .plc
        else if (!strcmp(compile, "c"))
	  sprintf(plcpath, "%s.c", filename); // .pl.c
        else if (!strcmp(compile, "exe"))
	  sprintf(plcpath, "%s.out", filename); // TODO: strip ext
      }

      plc = fopen(plcpath, "wb");
      if (!plc) {
	fprintf(stderr, "** could not open %s for writing. check permissions.", plcpath);
	goto done;
      }

      if (exec == EXEC_COMPILE) { // compile backend. default: bc
        if (!compile)
	  code = potion_source_dumpbc(P, PN_NIL, code, PN_NIL);
        else
	  code = potion_send(code, potion_str(P, "dump"),
			     potion_str(P, compile), opts ? potion_str(P, opts) : PN_NIL);
      }

      if (code && fwrite(PN_STR_PTR(code), 1, PN_STR_LEN(code), plc) == PN_STR_LEN(code)) {
	printf("** compiled code saved to %s\n", plcpath);
        fclose(plc);

	if (exec == EXEC_COMPILE) {
	  if (!compile || !strcmp(compile, "bc"))
	    printf("** run it with: p2 %s\n", plcpath);
	  else if (!strcmp(compile, "c"))
	    printf("** compile it with: %s %s %s\n", POTION_CC, POTION_CFLAGS, plcpath);
	  else if (!strcmp(compile, "exe"))
	    printf("** run it with: ./%s\n", plcpath);
	}
      } else {
        fprintf(stderr, "** could not write all %s compiled code.\n", compile ? compile : "bytecode");
      }
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
    if (!strcmp(argv[i], "--compile")) {
      exec = EXEC_COMPILE; continue; }
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
              if (!sw.fsplit) free(copy);
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
        if (!code || PN_TYPE(code) == PN_TERROR) rc = 1;
      }
    }
    else {
      potion_define_global(P, PN_STR("$0"), PN_STR(fn));
      rc = p2_cmd_compile(P, fn, compile);
    }
  } else {
    if (P->flags & DEBUG_INSPECT) potion_fatal("no filename given");
    potion_define_global(P, PN_STR("$0"), PN_STR("-i"));
    potion_define_global(P, potion_str(P, "$P2::interactive"), PN_NUM(1));
    // TODO: p5 not yet parsed
#if 0
    p2_eval(P, potion_byte_str(P,
      "load 'readline';\n" \
      "while ($code = readline('>> ')) {\n" \
      "  $obj = eval $code;\n" \
      "  if ($@) {\n" \
      "    say $@;\n" \
      "  } else {\n"  \
      "    say '=> ', $obj;\n" \
      "  }\n"
      "}"));
#else
#ifndef SANDBOX
    {
      PN code = potion_syntax_parse(P, "potion", potion_byte_str(P,
      "load 'readline'\n" \
      "loop:\n" \
      "  code = readline('>> ')\n" \
      "  if (not code): \"\\n\" print, break.\n" \
      "  if (code != ''):\n" \
      "    obj = code eval\n" \
      "    if (obj kind == Error):\n" \
      "      obj string print." \
      "    else: ('=> ', obj, \"\\n\") join print.\n" \
      "  .\n"
      "."), "<eval>");
      if (PN_TYPE(code) != PN_TSOURCE) return code;
      code = potion_send(code, PN_compile, PN_NIL, PN_NIL);
      return potion_run(P, code, P->flags & EXEC_JIT);
    }
#endif /* SANDBOX */
#endif
  }
END:
#if !defined(POTION_JIT_TARGET) || defined(DEBUG) || defined(__SANITIZE_ADDRESS__)
  if (P != NULL)
    potion_destroy(P);
#endif
  return rc;
}
