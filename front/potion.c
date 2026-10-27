//
// potion.c
// the Potion!
//
// (c) 2008 why the lucky stiff, the freelance professor
// (c) 2013-2014 by perl11.com
//
#include <stdio.h>
#include <sys/stat.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#include "potion.h"
#include "internal.h"
#include "opcodes.h"
#include "khash.h"
#include "table.h"

const char potion_banner[] = "potion " POTION_VERSION
                             " (date='" POTION_DATE "', commit='" POTION_COMMIT
                             "', platform='" POTION_PLATFORM "', jit=%d)\n";
const char potion_version[] = POTION_VERSION;

static void potion_cmd_usage(Potion *P) {
  printf("usage: potion [options] [filename] [arguments]\n"
      "  -e  code           execute code instead of filename\n"
      "  -B, --bytecode     run with bytecode VM (slower, but cross-platform)\n"
      "  -X, --x86          run with x86 JIT VM (faster, x86 and x86-64)\n"
      "  -I, --inspect      print only the return value\n"
      "  -V, --verbose      show bytecode and ast info\n"
      "  -Ldirectory        add library search path\n"
      "  -Mmodule[=args]    load module\n"
      "  -d[module[=args]]  debug script\n"
      "      --debug[=module[,args]]\n"
      "  -c, --compile      compile the script to bytecode\n"
      "      --compile={target[,args]} generate bytecode (bc), C (c), or exe\n"
      "      --compile=c    write <filename>.c without running the script\n"
      "                     compile externally with -Icore and -Llib -lpotion\n"
      "                     add -DPOTION_MODULE -fPIC -shared for a module\n"
      "      --check        check the script syntax and exit\n"
      "  -h, --help         show this helpful stuff\n"
#ifdef DEBUG
      "  -D[itPpcvGJ?]       debugging flags, try -D?\n"
#endif
      "  -v, --version      show version\n"
      "(default: %s)\n",
#if POTION_JIT == 1
      "-X"
#else
      "-B"
#endif
  );
}

static void potion_cmd_stats(Potion *P) {
  printf("sizeof(PN=%d, PNObject=%d, PNTuple=%d, PNTuple+1=%d, PNTable=%d)\n",
      (int)sizeof(PN), (int)sizeof(struct PNObject), (int)sizeof(struct PNTuple),
      (int)(sizeof(PN) + sizeof(struct PNTuple)), (int)sizeof(struct PNTable));
  printf("GC (fixed=%" PRIdPTR ", actual=%" PRIdPTR ", reserved=%" PRIdPTR ")\n",
      PN_INT(potion_gc_fixed(P, 0, 0)), PN_INT(potion_gc_actual(P, 0, 0)),
      PN_INT(potion_gc_reserved(P, 0, 0)));
}

static void potion_cmd_version(Potion *P) {
  printf(potion_banner, POTION_JIT);
}

#define DBG_Pv(c) \
    if (P->flags & DEBUG_VERBOSE) \
      potion_p(P, c)
//v OR i!
#define DBG_Pvi(c) \
    if (P->flags & (DEBUG_INSPECT|DEBUG_VERBOSE)) \
      potion_p(P, c)

static int potion_cmd_failed;

static PN potion_cmd_exec(Potion *P, PN buf, char *filename, char *compile, char *addcode) {
  exec_mode_t exec = (exec_mode_t)(P->flags & ((1<<EXEC_BITS)-1));
  int fd = -1;
  char *outpath = NULL;
  PN code = PN_NIL;

  if (!buf && filename) {
    struct stat stats;
    if (stat(filename, &stats) == -1) {
      fprintf(stderr, "** %s does not exist.", filename);
      goto done;
    }
    fd = open(filename, O_RDONLY | O_BINARY);
    if (fd == -1) {
      fprintf(stderr, "** could not open %s. check permissions.", filename);
      goto done;
    }
    long size = stats.st_size;
    char *bufptr;
    if (addcode) {
      int len = strlen(addcode);
      size += len;
      buf = potion_bytes(P, size);
      bufptr = PN_STR_PTR(buf);
      PN_MEMCPY_N(bufptr, addcode, char, len);
      bufptr += len;
    } else {
      buf = potion_bytes(P, size);
      bufptr = PN_STR_PTR(buf);
    }
    // TODO: mmap instead of read all
    if (read(fd, bufptr, stats.st_size) == stats.st_size) {
      PN_STR_PTR(buf)[size] = '\0';
    } else {
      fprintf(stderr, "** could not read entire file.");
      goto done;
    }
  }
  else if (!filename) filename = "-e";

  code = potion_source_load(P, PN_NIL, buf);
  if (PN_IS_PROTO(code)) {
    DBG_v("\n-- loaded --\n");
  } else {
    code = potion_syntax_parse(P, "potion", buf, filename);
    if (!code || PN_TYPE(code) == PN_TERROR) {
      potion_p(P, code);
      goto done;
    }
    DBG_v("\n-- parsed --\n");
    DBG_Pv(code);
    code = potion_send(code, PN_compile, potion_str(P, filename), PN_NIL);
    DBG_v("\n-- compiled --\n");
  }
  DBG_Pv(code);
  if (exec == EXEC_VM || exec == EXEC_DEBUG) {
    code = potion_vm(P, code, P->lobby, PN_NIL, 0, NULL);
    DBG_v("\n-- vm returned %p (fixed=%" PRIdPTR ", actual=%" PRIdPTR ", reserved=%" PRIdPTR ", time=%0.6gms %dx/%dm/%di) --\n",
	  (void *)code,
	  PN_INT(potion_gc_fixed(P, 0, 0)), PN_INT(potion_gc_actual(P, 0, 0)),
	  PN_INT(potion_gc_reserved(P, 0, 0)), P->mem->time * 1000, P->mem->pass,
	  P->mem->majors, P->mem->minors);
    DBG_Pvi(code);
  } else if (exec == EXEC_JIT) {
#ifdef POTION_JIT_TARGET
    PN val;
    PN cl = potion_closure_new(P, (PN_F)potion_jit_proto(P, code), PN_NIL, 1);
    PN_CLOSURE(cl)->data[0] = code;
    val = PN_PROTO(code)->jit(P, cl, P->lobby);
    DBG_v("\n-- jit returned %p (fixed=%" PRIdPTR ", actual=%" PRIdPTR ", reserved=%" PRIdPTR ", time=%0.6gms %dx/%dm/%di) --\n", PN_PROTO(code)->jit,
	  PN_INT(potion_gc_fixed(P, 0, 0)), PN_INT(potion_gc_actual(P, 0, 0)),
	  PN_INT(potion_gc_reserved(P, 0, 0)), P->mem->time * 1000, P->mem->pass,
	  P->mem->majors, P->mem->minors);
    DBG_Pvi(val);
#else
    fprintf(stderr, "** potion built without JIT support\n");
#endif
  }
  else if (exec == EXEC_CHECK) {
    DBG_v("\n-- check --\n");
  }

  if (!code || PN_TYPE(code) == PN_TERROR) {
    if (exec == EXEC_COMPILE) potion_cmd_failed = 1;
    goto done;
  }

  if (exec >= MAX_EXEC)
    potion_fatal("fatal: stack corruption (exec > MAX_EXEC)\n");

  if (exec == EXEC_COMPILE) {
    const char *backend = compile ? compile : "bc";
    const char *suffix;
    char *c_opts = compile ? strchr(compile, ',') : NULL;
    FILE *pnb;
    size_t written, length;
    if (c_opts) *c_opts++ = '\0';
    if (!strcmp(backend, "bc")) suffix = "b";
    else if (!strcmp(backend, "c")) suffix = ".c";
    else if (!strcmp(backend, "exe")) suffix = ".out";
    else {
      fprintf(stderr, "** unknown compiler backend: %s\n", backend);
      potion_cmd_failed = 1;
      goto done;
    }
    code = potion_source_dump(P, PN_NIL, code, potion_str(P, backend),
                              c_opts ? potion_str(P, c_opts) : PN_NIL);
    if (!code || (PN_TYPE(code) != PN_TBYTES && !PN_IS_STR(code))) {
      fprintf(stderr, "** %s compiler failed to emit code\n", backend);
      if (code && PN_TYPE(code) == PN_TERROR) potion_p(P, code);
      potion_cmd_failed = 1;
      goto done;
    }
    if (!strcmp(backend, "exe")) {
      outpath = malloc(strlen(filename) + 5);
      if (!outpath) potion_allocation_error();
      strcpy(outpath, filename);
      strcat(outpath, suffix);
      {
        const char *shown = outpath, *o = c_opts; /* -o=PATH among the compiler arguments names the executable */
        while (o && *o) {
          if (!strncmp(o, "-o=", 3)) { static char outp[1024]; size_t l = strcspn(o + 3, ","); snprintf(outp, sizeof outp, "%.*s", (int)(l < 1023 ? l : 1023), o + 3); shown = outp; }
          o = strchr(o, ','); if (o) o++;
        }
        if (potion_exe_build(P, code, outpath, c_opts)) potion_cmd_failed = 1;
        else printf("** executable saved to %s\n** run it with: %s%s\n", shown, shown[0] == '/' ? "" : "./", shown);
      }
      goto done;
    }
    outpath = malloc(strlen(filename) + strlen(suffix) + 1);
    if (!outpath) potion_allocation_error();
    strcpy(outpath, filename);
    strcat(outpath, suffix);
    pnb = fopen(outpath, "wb");
    if (!pnb) {
      fprintf(stderr, "** could not open %s for writing. check permissions.\n", outpath);
      potion_cmd_failed = 1;
      goto done;
    }
    length = PN_STR_LEN(code);
    written = fwrite(PN_STR_PTR(code), 1, length, pnb);
    if (written != length || ferror(pnb)) {
      fprintf(stderr, "** could not write all %s compiled code (%lu/%lu) to %s\n",
              backend, (unsigned long)written, (unsigned long)length, outpath);
      potion_cmd_failed = 1;
    }
    if (fclose(pnb) != 0) {
      fprintf(stderr, "** could not close %s after writing\n", outpath);
      potion_cmd_failed = 1;
    }
    if (potion_cmd_failed) goto done;
    printf("** compiled code saved to %s\n", outpath);
    if (!strcmp(backend, "bc"))
      printf("** run it with: potion %s\n", outpath);
    else if (!strcmp(backend, "c")) {
      printf("** from the build tree, compile it with: %s %s -Icore \"%s\" -Llib -Wl,-rpath,\"$PWD/lib\" -lpotion -lm -o \"%s.out\"\n",
             POTION_CC, POTION_CFLAGS, outpath, filename);
      printf("** or build a loadable module: %s %s -Icore -DPOTION_MODULE -DPOTION_MODULE_NAME=compiled -fPIC -shared \"%s\" -Llib -Wl,-rpath,\"$PWD/lib\" -lpotion -lm -o compiled%s\n",
             POTION_CC, POTION_CFLAGS, outpath, POTION_LOADEXT);
    } else
      printf("** run it with: ./%s\n", outpath);
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

done:
  free(outpath);
  if (exec == EXEC_COMPILE && (!code || PN_TYPE(code) == PN_TERROR))
    potion_cmd_failed = 1;
  if (fd != -1)
    close(fd);

  return code;
}

char * addmodule(Potion *P, char *result, char *prefix, char *name) {
  char *args = strchr(name, '=');
  PN out = potion_bytes(P, 0);
  if (args) *args++ = '\0';
  if (prefix) {
    pn_printf(P, out, "load \"%s/%s\"\n", prefix, name);
  } else {
    pn_printf(P, out, "load \"%s\"\n", name);
  }
  if (args)
    pn_printf(P, out, "%s(%s)\n", name, args);
  else
    pn_printf(P, out, "%s()\n", name);
  return PN_STR_PTR(out);
}

int main(int argc, char *argv[]) {
  POTION_INIT_STACK(sp);
  Potion *P = potion_create(sp);
  int i;
  exec_mode_t exec = POTION_JIT ? EXEC_JIT : EXEC_VM;
  PN buf = PN_NIL;
  char *compile = NULL;
  char *fn = NULL;
  char *addmodules = NULL;

#if defined(STATIC) || defined(SANDBOX)
  Potion_Init_readline(P);
  Potion_Init_aio(P);
#ifndef SANDBOX
  Potion_Init_buffile(P);
#endif
#endif

  for (i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--")) { i++; break; }
    if (!strcmp(argv[i], "-I") || !strcmp(argv[i], "--inspect")) {
      P->flags |= DEBUG_INSPECT; continue; }
    if (!strcmp(argv[i], "-L")) {
#ifdef SANDBOX
    potion_fatal("-L disabled in SANDBOX");
#else
      char *extra_path = &argv[i][2]; // todo: flexible
      if (*extra_path)
	potion_loader_add(P, potion_str(P, extra_path));
      else {
	if (i == argc) {
	    fprintf(stderr, "-L missing directory\n");
	    goto END;
	}
	potion_loader_add(P, potion_str(P, argv[i+1]));
	i++;
      }
      continue;
#endif /* SANDBOX */
    }
    if (!strcmp(argv[i], "-V") || !strcmp(argv[i], "--verbose")) {
      P->flags |= DEBUG_VERBOSE; continue; }
    if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--version")) {
      potion_cmd_version(P); goto END; }
    if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
      potion_cmd_usage(P); goto END; }
    if (!strcmp(argv[i], "-s") || !strcmp(argv[i], "--stats")) {
      potion_cmd_stats(P); goto END; }
    if (!strcmp(argv[i], "--check")) { exec = EXEC_CHECK; continue; }
    if (!strncmp(argv[i], "--compile=", 10)) {
      exec = EXEC_COMPILE; compile = &argv[i][10]; continue; }
    if (!strcmp(argv[i], "--compile") || !strcmp(argv[i], "-c")) {
      exec = EXEC_COMPILE; compile = NULL; continue; }
    if (!strcmp(argv[i], "-B") || !strcmp(argv[i], "--bytecode")) {
      exec = EXEC_VM; continue; }
    if (!strcmp(argv[i], "-X") || !strcmp(argv[i], "--x86")) {
      exec = EXEC_JIT; continue; }
    if (!strcmp(argv[i], "--debug") || !strcmp(argv[i], "-d")) {
      addmodules = addmodule(P, addmodules, NULL, "debug");
      exec = EXEC_DEBUG; continue; }
    if (!strcmp(argv[i], "--debug=")) {
      addmodules = addmodule(P, addmodules, "debug", &argv[i][9]);
      exec = EXEC_DEBUG; continue; }
    if (argv[i][0] == '-' && argv[i][1] == 'd') {
      if (argv[i][2] == '=')
	addmodules = addmodule(P, addmodules, NULL,
			       PN_STR_PTR(PN_STRCAT("debug=", &argv[i][3])));
      else
	addmodules = addmodule(P, addmodules, "debug", &argv[i][2]);
      exec = EXEC_DEBUG; continue; }
    if (argv[i][0] == '-' && argv[i][1] == 'M') {
      addmodules = addmodule(P, addmodules, NULL, &argv[i][2]);
      continue; }
#ifdef DEBUG
    if (argv[i][0] == '-' && argv[i][1] == 'D') {
      if (strchr(&argv[i][2], '?')) {
	printf("-D debugging flags:\n");
	printf("  i  inspect\n");
	printf("  v  verbose\n");
	printf("  t  trace\n");
	printf("  p  parse\n");
	printf("  P  parse verbose\n");
	printf("  c  compile\n");
	printf("  J  disassemble Jit code\n");
	printf("  G  GC (use w/ or wo/ -Dv\n");
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
    if (argv[i][0] == '-') {
      if (argv[i][1] == 'e') {
        char *arg;
        if (strlen(argv[i]) == 2) {
          arg = argv[i+1];
        } else if (i <= argc) {
          arg = argv[i]+2;
        } else { // or go into interactive mode?
          potion_fatal("Missing argument for -e");
          goto END;
        }
	if (addmodules)
	  buf = potion_strcat(P, addmodules, arg);
	else
	  buf = potion_str(P, arg);
        continue;
      } else {
        fprintf(stderr, "** Unrecognized option: %s\n", argv[i]);
      }
    }
    else {
      break;
    }
  }
  P->flags = P->flags + exec;
  
  if (buf || i < argc) {
    PN args = PN_TUP0();
    if (buf == PN_NIL) { fn = argv[i++]; PN_PUSH(args, PN_STR(fn)); }
    else { PN_PUSH(args, PN_STR("-e")); }
    for (; i < argc; i++) PN_PUSH(args, PN_STR(argv[i]));
    potion_define_global(P, PN_STR("argv"), args);
    if (buf != PN_NIL) {
      potion_cmd_exec(P, buf, NULL, compile, "");
    } else {
      potion_cmd_exec(P, buf, fn, compile, addmodules);
    }
  } else {
    if (!exec || P->flags & DEBUG_INSPECT) potion_fatal("no filename given");
    PN args = PN_TUP0();
    PN_PUSH(args, PN_STR(""));
    potion_define_global(P, PN_STR("argv"), args);
    /* the loop is the interactive debugger: evaluate its lines with the plain VM, else `potion -d`
     * would start the c debugger, which reads the following input lines */
    P->flags = (Potion_Flags)(P->flags & ~((1 << EXEC_BITS) - 1));
    puts("potion: q or ^D quits, any other line is evaluated, :subs :classes :methods CLASS :b N :B N :bl");
    potion_eval(P, potion_byte_str(P, "load 'readline'"));
    for (;;) {
      PN line = potion_send(P->lobby, potion_str(P, "readline"), potion_str(P, ">> "));
      if (!line || !PN_IS_STR(line)) { putchar('\n'); break; }
      if (!strcmp(PN_STR_PTR(line), "q")) break;
      if (!PN_STR_LEN(line)) continue;
      if (PN_STR_PTR(line)[0] == ':') {
        if (!potion_debug_cmd(P, PN_STR_PTR(line))) puts("unknown command");
        continue;
      }
      PN obj = potion_send(line, potion_str(P, "eval"));
      PN s = potion_send(obj, PN_string);
      if (obj && PN_TYPE(obj) == PN_TERROR) printf("%s\n", PN_IS_STR(s) ? PN_STR_PTR(s) : "");
      else printf("=> %s\n", PN_IS_STR(s) ? PN_STR_PTR(s) : "");
      fflush(stdout);
    }
  }
 END:
#if !defined(POTION_JIT_TARGET) || defined(DEBUG) || defined(__SANITIZE_ADDRESS__)
  if (P != NULL)
    potion_destroy(P);
#endif
  return potion_cmd_failed ? 1 : 0;
}
