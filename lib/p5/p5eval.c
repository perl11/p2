/**\file lib/p5/p5eval.c
  p5 eval/die/warn/do/caller.

  (c) 2026 perl11.com */
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include "p5.h"

/* p5 die/eval: eval { BLOCK } runs the block under a setjmp frame; die sets
 * $@ and longjmps to the innermost frame, or prints to stderr and exits 255
 * when there is none. */
typedef struct PNEvalFrame {
  jmp_buf jb;
  struct PNEvalFrame *prev;
} PNEvalFrame;
static PNEvalFrame *eval_top;

static void p5_set_errsv(Potion *P, PN val) {
  potion_define_global(P, potion_str(P, "$@"), val);
}

static PN p5_eval(Potion *P, PN cl, PN self, PN block) {
  PNEvalFrame frame;
  volatile PN result = PN_NIL;
  if (PN_TYPE(block) != PN_TCLOSURE)
    return PN_NIL;
  frame.prev = eval_top;
  eval_top = &frame;
  if (setjmp(frame.jb) == 0) {
    result = PN_CLOSURE_CALL2(P, block, P->lobby, PN_NIL);
    eval_top = frame.prev;
    p5_set_errsv(P, PN_STR(""));
    return result;
  }
  eval_top = frame.prev;
  return PN_NIL;
}

/* eval "STRING": like eval BLOCK, and a syntax error of the string sets $@ too (p5synfail is
 * called by the parser's YY_ERROR while the string is compiled) */
static int eval_str;
static PN p5_evalstr(Potion *P, PN cl, PN self) {
  PNEvalFrame frame;
  volatile PN result = PN_NIL;
  int depth = eval_str;
  struct PNParseRoot *roots = P->parse_roots; /* a syntax error longjmps out of a running parse */
  if (!PN_IS_STR(self)) return PN_NIL;
  frame.prev = eval_top;
  eval_top = &frame;
  eval_str++;
  if (setjmp(frame.jb) == 0) {
    result = potion_send(self, potion_str(P, "eval"));
    eval_top = frame.prev;
    eval_str = depth;
    p5_set_errsv(P, PN_STR(""));
    return result;
  }
  eval_top = frame.prev;
  eval_str = depth;
  while (P->parse_roots != roots) { /* the parses the longjmp left */
    struct PNParseRoot *pr = P->parse_roots;
    P->parse_roots = pr->prev;
    if (pr->abort) pr->abort(pr->parse);
  }
  return PN_NIL;
}

/* eval STRING at a site with compile time hints: the string is compiled with the hints of the site */
static PN p5_evalstrh(Potion *P, PN cl, PN self, PN idx) {
  PN r;
  p5_hh_enter(P, idx);
  r = p5_evalstr(P, cl, self);
  p5_hh_leave(P);
  return r;
}

static PN p5_synfail(Potion *P, PN cl, PN self, PN msg, PN line) {
  PN text;
  char *cm;
  if (!eval_str || !eval_top) return PN_FALSE;
  cm = strdup(PN_STR_PTR(msg) + (strncmp(PN_STR_PTR(msg), "** ", 3) == 0 ? 3 : 0)); /* the format allocates: a GC string may move */
  if (!cm) potion_allocation_error();
  text = potion_str_format(P, "%s at (eval) line %d.\n", cm, (int)PN_INT(line));
  free(cm);
  p5_set_errsv(P, potion_str2(P, PN_STR_PTR(text), PN_STR_LEN(text)));
  longjmp(eval_top->jb, 1);
}

/* 'die "x"' self-chains (the string is the receiver), 'die("x")' passes it as
 * the argument */
static PN p5_msg(PN self, PN msg) {
  return PN_IS_STR(msg) ? msg : PN_IS_STR(self) ? self : PN_NIL;
}

/* caller: there is no call-stack introspection; at file level it is empty */
static PN p5_caller(Potion *P, PN cl, PN self) { return PN_NIL; }

/* do BLOCK: the value of the block */
static PN p5_do(Potion *P, PN cl, PN self, PN block) {
  if (PN_TYPE(block) != PN_TCLOSURE) return PN_NIL;
  return PN_CLOSURE_CALL2(P, block, P->lobby, PN_NIL);
}

/* the position of the statement being run (p5setloc is the first part of a statement that calls die or warn) */
static PN p5_locfile = PN_NIL;
static long p5_locline;
static PN p5_setloc(Potion *P, PN cl, PN self, PN file, PN line) {
  if (!p5_locfile) potion_gc_root(P, &p5_locfile);
  p5_locfile = file;
  p5_locline = PN_IS_INT(line) ? PN_INT(line) : 0;
  return PN_NIL;
}
/* MSG without a trailing newline gets " at FILE line N.\n" (an empty message the DEFAULT text) */
static PN p5_locmsg(Potion *P, PN msg, const char *dflt) {
  PN text = PN_IS_STR(msg) && PN_STR_LEN(msg) ? msg : PN_STR(dflt);
  PN full;
  if (PN_STR_PTR(text)[PN_STR_LEN(text) - 1] == '\n') return text;
  if (PN_IS_STR(p5_locfile) && p5_locline > 0)
    full = potion_str_format(P, "%s at %s line %ld.\n", PN_STR_PTR(text), PN_STR_PTR(p5_locfile), p5_locline);
  else
    full = potion_str_format(P, "%s\n", PN_STR_PTR(text));
  return potion_str2(P, PN_STR_PTR(full), PN_STR_LEN(full)); /* interned: p5 'eq' compares identities */
}

static PN p5_die(Potion *P, PN cl, PN self, PN msg) {
  PN text = p5_locmsg(P, p5_msg(self, msg), "Died");
  if (eval_top) {
    p5_set_errsv(P, text);
    longjmp(eval_top->jb, 1);
  }
  fputs(PN_STR_PTR(text), stderr);
  potion_destroy(P);
  exit(255);
}

static PN p5_warn(Potion *P, PN cl, PN self, PN msg) {
  PN text = p5_locmsg(P, p5_msg(self, msg), "Warning: something's wrong");
  fputs(PN_STR_PTR(text), stderr);
  return PN_TRUE;
}

/* wantarray: the call site sets the context (p5want: 1 list, 0 scalar), a sub that asks takes it at entry
 * and resets it, so a call without an annotation is a list call */
static int p5_want = 1;
static PN p5_want_set(Potion *P, PN cl, PN self, PN ctx) {
  p5_want = PN_IS_INT(ctx) ? (int)PN_INT(ctx) : 1;
  return PN_NIL;
}
static PN p5_want_end(Potion *P, PN cl, PN self, PN a, PN b) {
  p5_want = 1;
  return b;
}
static PN p5_want_get(Potion *P, PN cl, PN self) {
  int w = p5_want;
  p5_want = 1;
  return w == 2 ? PN_NIL : w == 0 ? PN_FALSE : PN_NUM(w); /* 2: void; scalar is false but defined (a string result would be called by the VM); scalar is the empty string (false but defined) */
}

/* system(CMD): run through sh -c, $? is the wait status; the stdio buffer is flushed first */
static PN p5_set_status(Potion *P, int st) {
  potion_define_global(P, PN_STR("$?"), PN_NUM(st));
  return PN_NUM(st);
}
/* %ENV is a lobby table filled from environ at startup; before a child process starts it is copied back */
#ifdef __APPLE__
# include <crt_externs.h>
# define environ (*_NSGetEnviron())
#elif !defined(_WIN32)
extern char **environ;
#endif
#ifdef _WIN32
# define p5_setenv(k, v) _putenv_s(k, v)
#else
# define p5_setenv(k, v) setenv(k, v, 1)
#endif
static int p5_env_ready;
void p5_env_sync(Potion *P) {
  PN env, keys;
  PN_SIZE i;
  if (!p5_env_ready) return; /* %ENV was never mentioned: the process environment is as it was */
  env = potion_send(P->lobby, PN_STR("%ENV"));
  if (!PN_IS_PTR(env) || PN_TYPE(env) != PN_TTABLE) return;
  keys = potion_send(env, PN_STR("keys"));
  if (!PN_IS_TUPLE(keys)) return;
#ifndef _WIN32
  { /* no clearenv on the BSDs and macOS: unset every variable */
    char name[256];
    while (environ && environ[0]) {
      const char *eq = strchr(environ[0], '=');
      size_t n = eq ? (size_t)(eq - environ[0]) : 0;
      if (!n || n >= sizeof name) break;
      memcpy(name, environ[0], n); name[n] = 0;
      if (unsetenv(name) != 0) break;
    }
  }
#endif
  for (i = 0; i < PN_TUPLE_LEN(keys); i++) {
    PN k = PN_TUPLE_AT(keys, i), v = potion_send(env, PN_STR("at"), k);
    k = p5_tostr(P, k);
    v = p5_tostr(P, v);
    if (PN_IS_STR(k) && PN_IS_STR(v) && PN_STR_LEN(k)) p5_setenv(PN_STR_PTR(k), PN_STR_PTR(v));
  }
}
static PN p5_system(Potion *P, PN cl, PN self, PN cmd) {
  int st;
  p5_env_sync(P);
  cmd = p5_tostr(P, cmd);
  if (!PN_IS_STR(cmd)) return PN_NUM(-1);
  fflush(stdout);
  st = system(PN_STR_PTR(cmd));
  return p5_set_status(P, st);
}
/* readpipe(CMD), `CMD`: the output of the command */
static PN p5_readpipe(Potion *P, PN cl, PN self, PN cmd) {
  FILE *fp;
  p5_env_sync(P);
  char buf[4096];
  size_t n;
  PN out = PN_STR("");
  cmd = p5_tostr(P, cmd);
  if (!PN_IS_STR(cmd)) return PN_NIL;
  fflush(stdout);
  fp = popen(PN_STR_PTR(cmd), "r");
  if (!fp) { p5_set_status(P, -1); return PN_NIL; }
  while ((n = fread(buf, 1, sizeof buf, fp)) > 0)
    out = potion_str_format(P, "%s%.*s", PN_STR_PTR(out), (int)n, buf);
  p5_set_status(P, pclose(fp));
  return potion_str2(P, PN_STR_PTR(out), PN_STR_LEN(out)); /* interned: p5 eq compares identities */
}

/* unlink LIST: the number of files removed (the parser pads the call to 6 arguments with nil) */
static PN p5_unlink(Potion *P, PN cl, PN self, PN a, PN b, PN c, PN d, PN e, PN f) {
  PN args[6] = { a, b, c, d, e, f };
  long i, n = 0;
  for (i = 0; i < 6; i++) {
    PN v = args[i] == PN_NIL ? PN_NIL : p5_tostr(P, args[i]);
    if (PN_IS_STR(v) && unlink(PN_STR_PTR(v)) == 0) n++;
  }
  return PN_NUM(n);
}

/* umask(MASK) sets the mask and returns the old one; umask() only reads it */
static PN p5_umask(Potion *P, PN cl, PN self, PN a) {
  mode_t old;
  if (PN_IS_INT(a)) return PN_NUM((long)umask((mode_t)PN_INT(a) & 0777));
  old = umask(0);
  umask(old);
  return PN_NUM((long)old);
}

/* `CMD` in list context: the output lines */
static PN p5_readpipelist(Potion *P, PN cl, PN self, PN cmd) {
  PN all = p5_readpipe(P, cl, self, cmd), out = PN_TUP0();
  const char *s, *nl;
  long len, pos = 0;
  if (!PN_IS_STR(all)) return out;
  s = PN_STR_PTR(all); len = PN_STR_LEN(all);
  while (pos < len) {
    nl = memchr(s + pos, '\n', len - pos);
    long n = nl ? nl - (s + pos) + 1 : len - pos;
    out = PN_PUSH(out, potion_str2(P, PN_STR_PTR(all) + pos, n)); /* all may have moved: re-read the pointer */
    pos += n;
    s = PN_STR_PTR(all);
  }
  return out;
}

/* a lobby table from key, value pairs */
static PN p5_pairs_table(Potion *P, const char *const *kv) {
  PN t = PN_TUP0();
  for (; kv[0]; kv += 2) { t = PN_PUSH(t, potion_str(P, kv[0])); t = PN_PUSH(t, potion_str(P, kv[1])); }
  return potion_send(t, PN_STR("table"));
}
/* %Config: the values the roast5 tests look at */
static void p5_config_init(Potion *P) {
  static const char *const kv[] = {
#ifdef _WIN32
    "osname", "MSWin32", "path_sep", ";", "archname", "MSWin32-x64", "d_link", "", "d_symlink", "", "d_readlink", "",
    "d_fchmod", "", "d_fchown", "",
#else
    "osname", "linux", "path_sep", ":", "archname", "x86_64-linux", "d_link", "define", "d_symlink", "define",
    "d_readlink", "define", "d_fchmod", "define", "d_fchown", "define", "i_pwd", "define",
#endif
    "useperlio", "define", "intsize", "4", "longsize", "8", "ivsize", "8", "uvsize", "8", "shortsize", "2", "ptrsize", "8", "nvsize", "8",
    "cc", "cc", "ccflags", "", "extensions", "", "osvers", "", "myuname", "", "d_truncate", "define", "d_ftruncate", "define",
    "version", "5.42.0", "PERL_VERSION", "42", "PERL_REVISION", "5", "perl_patchlevel", "", "sig_name", "ZERO HUP INT QUIT ILL TRAP ABRT BUS FPE KILL USR1 SEGV USR2 PIPE ALRM TERM",
    0, 0 };
  potion_define_global(P, PN_STR("%Config"), p5_pairs_table(P, kv));
}
/* %ENV and %Config are built when a program mentions them (p5envinit, called by the parser): the startup of
 * every other program allocates the same whatever the size of its environment */
static PN p5_env_init(Potion *P, PN cl, PN self) {
  PN t = PN_TUP0();
  if (p5_env_ready) return PN_NIL;
  p5_env_ready = 1;
  p5_config_init(P);
  char **e;
  for (e = environ; e && *e; e++) {
    const char *eq = strchr(*e, '=');
    if (!eq || eq == *e) continue;
    t = PN_PUSH(t, potion_str2(P, *e, (PN_SIZE)(eq - *e)));
    t = PN_PUSH(t, potion_str(P, eq + 1));
  }
  potion_define_global(P, PN_STR("%ENV"), potion_send(t, PN_STR("table")));
  return PN_NIL;
}

/* $!{ENAME}: true when $! is the message of that errno (the text is what $! holds here) */
static PN p5_errnoname(Potion *P, PN cl, PN self, PN name) {
  static const struct { const char *n; int e; } tab[] = {
    {"EPERM", EPERM}, {"ENOENT", ENOENT}, {"ESRCH", ESRCH}, {"EINTR", EINTR}, {"EIO", EIO}, {"ENXIO", ENXIO}, {"E2BIG", E2BIG},
    {"ENOEXEC", ENOEXEC}, {"EBADF", EBADF}, {"ECHILD", ECHILD}, {"EAGAIN", EAGAIN}, {"ENOMEM", ENOMEM}, {"EACCES", EACCES},
    {"EFAULT", EFAULT}, {"EBUSY", EBUSY}, {"EEXIST", EEXIST}, {"EXDEV", EXDEV}, {"ENODEV", ENODEV}, {"ENOTDIR", ENOTDIR},
    {"EISDIR", EISDIR}, {"EINVAL", EINVAL}, {"ENFILE", ENFILE}, {"EMFILE", EMFILE}, {"ENOTTY", ENOTTY}, {"EFBIG", EFBIG},
    {"ENOSPC", ENOSPC}, {"ESPIPE", ESPIPE}, {"EROFS", EROFS}, {"EMLINK", EMLINK}, {"EPIPE", EPIPE}, {"EDOM", EDOM},
    {"ERANGE", ERANGE}, {"ENOTEMPTY", ENOTEMPTY}, {"ENAMETOOLONG", ENAMETOOLONG}, {0, 0}
  };
  PN cur = potion_send(P->lobby, PN_STR("$!")), nm = p5_tostr(P, name);
  int i;
  if (!PN_IS_STR(cur) || !PN_IS_STR(nm)) return PN_STR0;
  for (i = 0; tab[i].n; i++)
    if (!strcmp(tab[i].n, PN_STR_PTR(nm))) {
      const char *msg = strerror(tab[i].e);
      return PN_STR_LEN(cur) == strlen(msg) && !memcmp(PN_STR_PTR(cur), msg, PN_STR_LEN(cur)) ? PN_NUM(tab[i].e) : PN_STR0;
    }
  return PN_STR0;
}

void p5_eval_init(Potion *P) {
  potion_method(P->lobby, "p5envinit", p5_env_init, 0);
  potion_method(P->lobby, "p5errnoname", p5_errnoname, "name=o");
  potion_define_global(P, PN_STR("%INC"), potion_send(PN_TUP0(), PN_STR("table")));
  potion_method(P->lobby, "readpipelist", p5_readpipelist, "cmd=o");
  potion_method(P->lobby, "unlink", p5_unlink, "a=o,b=o,c=o,d=o,e=o,f=o");
  potion_method(P->lobby, "p5evalstrh", p5_evalstrh, "idx=o");
  potion_method(P->lobby, "umask", p5_umask, "a=o");
  potion_method(P->lobby, "system", p5_system, "cmd=o");
  potion_method(P->lobby, "readpipe", p5_readpipe, "cmd=o");
  potion_define_global(P, PN_STR("$?"), PN_NUM(0));
  potion_define_global(P, PN_STR("$$"), PN_NUM((long)getpid()));
  potion_method(P->lobby, "p5eval", p5_eval, "block=&");
  potion_method(P->lobby, "p5evalstr", p5_evalstr, 0);
  potion_method(P->lobby, "p5synfail", p5_synfail, "msg=o,line=o");
  potion_method(P->lobby, "p5do", p5_do, "block=&");
  potion_method(P->lobby, "caller", p5_caller, 0);
  potion_method(P->lobby, "die",  p5_die, "|msg=o");
  potion_method(P->lobby, "p5setloc", p5_setloc, "file=o,line=o");
  potion_method(P->lobby, "p5want", p5_want_set, "ctx=o");
  potion_method(P->lobby, "p5wantget", p5_want_get, 0);
  potion_method(P->lobby, "p5wantend", p5_want_end, "a=o,b=o");
  potion_method(P->lobby, "warn",  p5_warn, "|msg=o");
  potion_define_global(P, PN_STR("$@"), PN_STR(""));
}
