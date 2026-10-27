/**\file lib/p5/p5file.c
  p5 file handles on top of the core PNFile (unbuffered fd IO): 2/3-arg open,
  <FH> readline, eof, print/say FH LIST and close.

  The core PNFile has no read buffer; p5 readline keeps its pending bytes in a
  side table indexed by file descriptor, released by p5close or when the fd is
  reopened.

  (c) 2026 perl11.com */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <dirent.h>
#ifndef _WIN32
# include <glob.h>
#endif
#ifndef _WIN32
# include <sys/file.h>
#endif
#include <sys/time.h>
#ifdef _WIN32
# include <utime.h>
#endif
#include "p5.h"
#ifdef _WIN32 /* no setuid/sticky bits, symlinks, sockets, fifos or uids in the mingw stat */
# ifndef S_ISUID
#  define S_ISUID 0
#  define S_ISGID 0
#  define S_ISVTX 0
# endif
# ifndef S_ISLNK
#  define S_ISLNK(m) 0
# endif
# ifndef S_ISSOCK
#  define S_ISSOCK(m) 0
# endif
# ifndef S_ISFIFO
#  define S_ISFIFO(m) (((m) & S_IFMT) == _S_IFIFO)
# endif
# ifndef S_ISBLK
#  define S_ISBLK(m) 0
# endif
# define lstat stat
# define geteuid() 0
# ifndef R_OK
#  define R_OK 4
#  define W_OK 2
#  define X_OK 0
# endif
#endif

#ifndef O_BINARY
# define O_BINARY 0   /* windows: no CRLF translation */
#endif

struct p5buf { char *b; long len, pos, lines; };
static struct p5buf *p5bufs;
static int p5nbufs;

/* the (cleared) read buffer of fd */
static struct p5buf *p5_buf(int fd, int clear) {
  if (fd >= p5nbufs) {
    int n = fd + 16;
    p5bufs = realloc(p5bufs, n * sizeof *p5bufs);
    if (!p5bufs) potion_allocation_error();
    memset(p5bufs + p5nbufs, 0, (n - p5nbufs) * sizeof *p5bufs);
    p5nbufs = n;
  }
  if (clear) {
    free(p5bufs[fd].b);
    memset(&p5bufs[fd], 0, sizeof p5bufs[fd]);
  }
  return &p5bufs[fd];
}

/**\memberof Lobby
  p5 3-arg open(FH, MODE, PATH): MODE is <, >, >>, +<, +>, +>> with an optional
  ":layer" suffix (ignored). On error sets $!.
  \return a new PNFile, or PN_NIL on error */
/* open(FH, "cmd|") / open(FH, "|cmd"): a popen'ed pipe; the table maps the fd to its FILE so that close waits for the child */
static struct { int fd; FILE *fp; } p5_pipes[64];
static PN p5_popen(Potion *P, PN cmd, int wr) {
  FILE *fp;
  struct PNFile *file;
  int i;
  p5_env_sync(P);
  fflush(stdout);
  cmd = p5_tostr(P, cmd);
  if (!(fp = popen(PN_STR_PTR(cmd), wr ? "w" : "r"))) {
    potion_define_global(P, PN_STR("$!"), PN_STR(strerror(errno)));
    return PN_NIL;
  }
  for (i = 0; i < 64 && p5_pipes[i].fp; i++) ;
  if (i == 64) { pclose(fp); return PN_NIL; }
  p5_pipes[i].fd = fileno(fp); p5_pipes[i].fp = fp;
  p5_buf(p5_pipes[i].fd, 1);
  file = (struct PNFile *)potion_object_new(P, PN_NIL, PN_VTABLE(PN_TFILE));
  file->fd = p5_pipes[i].fd;
  file->path = cmd;
  PN_TOUCH(file);
  file->mode = wr ? O_WRONLY : O_RDONLY;
  return (PN)file;
}

/* open(FH, ">&STDOUT"), open(FH, "<&", $fh), ">&=N":
   a duplicate of a handle, a standard handle name or a file descriptor */
static PN p5_dupopen(Potion *P, int flags, PN target) {
  int fd = -1, nfd;
  struct PNFile *file;
  if (PN_IS_PTR(target) && PN_VTYPE(target) == PN_TFILE) fd = ((struct PNFile *)target)->fd;
  else if (PN_IS_INT(target)) fd = (int)PN_INT(target);
  else {
    PN t = p5_tostr(P, target);
    const char *c = PN_IS_STR(t) ? PN_STR_PTR(t) : "";
    if (*c == '*') c++;
    if (!strncmp(c, "main::", 6)) c += 6;
    if (!strcmp(c, "STDIN")) fd = 0;
    else if (!strcmp(c, "STDOUT")) fd = 1;
    else if (!strcmp(c, "STDERR")) fd = 2;
    else if (*c >= '0' && *c <= '9') fd = atoi(c);
    else {
      PN g = potion_send(P->lobby, potion_str_format(P, "*%s", c));
      if (PN_IS_PTR(g) && PN_VTYPE(g) == PN_TFILE)
        fd = ((struct PNFile *)g)->fd;
    }
  }
  if (fd < 0 || (nfd = dup(fd)) < 0) {
    potion_define_global(P, PN_STR("$!"), PN_STR(strerror(fd < 0 ? EBADF : errno)));
    return PN_NIL;
  }
  p5_buf(nfd, 1);
  file = (struct PNFile *)potion_object_new(P, PN_NIL, PN_VTABLE(PN_TFILE));
  file->fd = nfd;
  file->path = PN_STR("dup");
  PN_TOUCH(file);
  file->mode = flags;
  return (PN)file;
}
static PN p5_open3(Potion *P, PN cl, PN self, PN mode, PN path) {
  const char *m = PN_STR_PTR(mode);
  struct PNFile *file;
  int fd, flags;
  while (*m == ' ') m++;
  { /* dup modes: <& >& >>& +<& (and the fdopen forms with =) */
    const char *amp = strchr(m, '&');
    if (amp && amp > m && amp - m <= 3 && strspn(m, "+<>") == (size_t)(amp - m)) {
      int fl = !strncmp(m, "+<", 2) ? O_RDWR : !strncmp(m, ">>", 2) ?
        O_WRONLY | O_APPEND : *m == '>' ? O_WRONLY : O_RDONLY;
      return p5_dupopen(P, fl, path);
    }
  }
  if (!strncmp(m, "-|", 2)) return p5_popen(P, p5_tostr(P, path), 0); /* open(FH, "-|", CMD): read from a command */
  if (!strncmp(m, "|-", 2)) return p5_popen(P, p5_tostr(P, path), 1); /* open(FH, "|-", CMD): write to a command */
  if (!strncmp(m, "+<", 2)) flags = O_RDWR;
  else if (!strncmp(m, "+>>", 3)) flags = O_RDWR | O_CREAT | O_APPEND;
  else if (!strncmp(m, "+>", 2)) flags = O_RDWR | O_CREAT | O_TRUNC;
  else if (!strncmp(m, ">>", 2)) flags = O_WRONLY | O_CREAT | O_APPEND;
  else if (*m == '>') flags = O_WRONLY | O_CREAT | O_TRUNC;
  else if (*m == '<') flags = O_RDONLY;
  else return PN_NIL;
  path = p5_tostr(P, path);
  if ((fd = open(PN_STR_PTR(path), flags | O_BINARY, 0666)) == -1) {
    potion_define_global(P, PN_STR("$!"), PN_STR(strerror(errno)));
    return PN_NIL;
  }
  p5_buf(fd, 1);
  file = (struct PNFile *)potion_object_new(P, PN_NIL, PN_VTABLE(PN_TFILE));
  file->fd = fd;
  file->path = path;
  PN_TOUCH(file);
  file->mode = flags;
  return (PN)file;
}

/**\memberof Lobby
  p5 2-arg open(FH, "<path"), ">path", ">>path", "path" (read).
  \return a new PNFile, or PN_NIL on error */
static PN p5_open2(Potion *P, PN cl, PN self, PN spec) {
  /* a copy of the text: the strings built below may move the GC string */
  char buf[1024], mb[8];
  const char *s = buf, *p;
  long n = 0;
  spec = p5_tostr(P, spec);
  snprintf(buf, sizeof buf, "%.*s", (int)PN_STR_LEN(spec), PN_STR_PTR(spec));
  while (*s == ' ') s++;
  { /* a trailing | reads from a command, a leading | writes to one */
    size_t l = strlen(s);
    while (l && s[l - 1] == ' ') l--;
    if (l > 1 && s[l - 1] == '|') {
      char cmd[1024];
      snprintf(cmd, sizeof cmd, "%.*s", (int)(l - 1), s); return p5_popen(P, PN_STR(cmd), 0);
    }
    if (s[0] == '|') {
      s++;
      while (*s == ' ') s++;
      return p5_popen(P, PN_STR(s), 1);
    }
  }
  while (s[n] == '+' || s[n] == '<' || s[n] == '>') n++;
  if (!n) return p5_open3(P, cl, self, PN_STR("<"), PN_STR(s));
  p = s + n;
  if (*p == '&') { /* dup of a handle: keep the & in the mode */
    char dm[8], tgt[512];
    int dl = (int)(n < 6 ? n : 6);
    p++;
    if (*p == '=') p++;
    while (*p == ' ') p++;
    snprintf(dm, sizeof dm, "%.*s&", dl, s);
    snprintf(tgt, sizeof tgt, "%s", p);
    { PN m = PN_STR(dm); return p5_open3(P, cl, self, m, PN_STR(tgt)); }
  }
  while (*p == ' ') p++;
  snprintf(mb, sizeof mb, "%.*s", (int)(n < 7 ? n : 7), s);
  { PN m = PN_STR(mb); return p5_open3(P, cl, self, m, PN_STR(p)); }
}

/* append another chunk to the read buffer of fd; 0 on eof or error */
static int p5_fill(struct PNFile *f) {
  char tmp[4096];
  struct p5buf *pb;
  ssize_t r;
  if (f->fd < 0) return 0;
  while ((r = read(f->fd, tmp, sizeof tmp)) == -1 && errno == EINTR) ;
  if (r <= 0) return 0;
  pb = p5_buf(f->fd, 0);
  if (pb->pos == pb->len) pb->pos = pb->len = 0;
  pb->b = realloc(pb->b, pb->len + r);
  if (!pb->b) potion_allocation_error();
  memcpy(pb->b + pb->len, tmp, r);
  pb->len += r;
  return 1;
}

/* seek(FH, POS, WHENCE): the read buffer is dropped; tell(FH): the position of the next byte read */
static PN p5_seek(Potion *P, PN cl, PN self, PN pos, PN whence) {
  struct PNFile *f = (struct PNFile *)self;
  struct p5buf *pb;
  off_t r;
  int w = PN_IS_NUM(whence) ? (int)PN_DBL(whence) : 0;
  if (f->fd < 0) return PN_FALSE;
  pb = p5_buf(f->fd, 0);
  if (w == 1) { /* relative to the logical position, behind the buffered bytes */
    r = lseek(f->fd, (off_t)(PN_IS_NUM(pos) ? (long)PN_DBL(pos) : 0) - (pb->len - pb->pos), SEEK_CUR);
  } else
    r = lseek(f->fd, (off_t)(PN_IS_NUM(pos) ? (long)PN_DBL(pos) : 0), w == 2 ? SEEK_END : SEEK_SET);
  pb->pos = pb->len = 0;
  return r == (off_t)-1 ? PN_FALSE : PN_TRUE;
}
static PN p5_tell(Potion *P, PN cl, PN self) {
  struct PNFile *f = (struct PNFile *)self;
  struct p5buf *pb;
  off_t r;
  if (f->fd < 0) return PN_NUM(-1);
  pb = p5_buf(f->fd, 0);
  r = lseek(f->fd, 0, SEEK_CUR);
  return r == (off_t)-1 ? PN_NUM(-1) : PN_NUM((long)r - (pb->len - pb->pos));
}

/* read(FH, $buf, LEN [, OFFSET]): the new value of $buf; the byte count is p5readn (undef at an error).
 * An OFFSET beyond the end pads with \0, a negative one counts from the end. */
static long p5_read_n = -1;
static PN p5_readbuf(Potion *P, PN cl, PN self, PN buf, PN len, PN off) {
  struct PNFile *f = (struct PNFile *)self;
  long want = PN_IS_NUM(len) ? (long)PN_DBL(len) : 0, o = PN_IS_NUM(off) ? (long)PN_DBL(off) : 0, have, got = 0, blen = 0;
  struct p5buf *pb;
  char *out, *old = NULL;
  PN r;
  p5_read_n = -1;
  if (f->fd < 0 || want < 0) return buf;
  if (PN_IS_STR(buf)) {
    blen = (long)PN_STR_LEN(buf);
    old = malloc(blen + 1);
    if (!old)
      potion_allocation_error();
    memcpy(old, PN_STR_PTR(buf), blen);
  }
  if (o < 0) { o += blen; if (o < 0) o = 0; }
  pb = p5_buf(f->fd, 0);
  while (pb->len - pb->pos < want && p5_fill(f)) pb = p5_buf(f->fd, 0);
  have = pb->len - pb->pos;
  got = have < want ? have : want;
  out = malloc(o + got + 1);
  if (!out) potion_allocation_error();
  if (o <= blen) { if (old) memcpy(out, old, o); }
  else { if (old) memcpy(out, old, blen); memset(out + blen, 0, o - blen); }
  memcpy(out + o, pb->b + pb->pos, got);
  pb->pos += got;
  p5_read_n = got;
  r = potion_str2(P, out, o + got);
  free(out); free(old);
  return r;
}

static PN p5_readn(Potion *P, PN cl, PN self, PN assigned) {
  return p5_read_n < 0 ? PN_NIL : PN_NUM(p5_read_n);
}

/**\memberof PNFile
  p5 <FH>: the next line including its newline
  \return PNString or PN_NIL at eof */
static PN p5_readline1(Potion *P, PN cl, PN self) {
  struct PNFile *f = (struct PNFile *)self;
  long scan = 0, m = 1;
  char sepb[64] = "\n";
  int slurp = 0, para = 0;
  PN rs;
  if (f->fd < 0) return PN_NIL;
  rs = potion_send(P->lobby, potion_str(P, "$/")); /* $/ : the input record separator */
  if (rs == PN_NIL) slurp = 1;
  else if (PN_IS_STR(rs)) {
    if (PN_STR_LEN(rs) == 0) { para = 1; strcpy(sepb, "\n\n"); m = 2; }
    else if (PN_STR_LEN(rs) < sizeof sepb) {
      memcpy(sepb, PN_STR_PTR(rs), PN_STR_LEN(rs));
      m = PN_STR_LEN(rs);
    }
  }
  for (;;) {
    struct p5buf *pb = p5_buf(f->fd, 0);
    char *nl = NULL;
    if (!slurp && pb->b && pb->len - pb->pos >= scan + m) {
      if (m == 1) nl = memchr(pb->b + pb->pos + scan, sepb[0], pb->len - pb->pos - scan);
      else {
        long i, end = pb->len - pb->pos - m;
        for (i = scan; i <= end; i++)
          if (!memcmp(pb->b + pb->pos + i, sepb, m)) { nl = pb->b + pb->pos + i; break; }
      }
    }
    if (nl) {
      long n = nl - (pb->b + pb->pos) + m;
      PN s = PN_STRN(pb->b + pb->pos, n);
      pb = p5_buf(f->fd, 0);
      pb->pos += n;
      if (para) { /* extra blank lines after a paragraph are skipped */
        for (;;) {
          while (pb->pos < pb->len && pb->b[pb->pos] == '\n') pb->pos++;
          if (pb->pos < pb->len || !p5_fill(f)) break;
          pb = p5_buf(f->fd, 0);
        }
      }
      return s;
    }
    scan = pb->len - pb->pos > m ? pb->len - pb->pos - (m - 1) : 0;
    if (!p5_fill(f)) {
      pb = p5_buf(f->fd, 0);
      if (pb->len > pb->pos) {
        PN s = PN_STRN(pb->b + pb->pos, pb->len - pb->pos);
        pb = p5_buf(f->fd, 0);
        pb->pos = pb->len;
        return s;
      }
      return PN_NIL;
    }
  }
}

/* p5_readline1 and the line counter $. of that handle */
static long p5_argv_lines;
static PN p5_readline(Potion *P, PN cl, PN self) {
  PN line;
  potion_define_global(P, PN_STR("*^LASTFH"), self); /* bare eof tests the last handle read */
  line = p5_readline1(P, cl, self);
  if (line != PN_NIL) {
    struct PNFile *f = (struct PNFile *)self;
    long n = ++p5_buf(f->fd, 0)->lines;
    potion_define_global(P, PN_STR("$."), PN_NUM(n));
  }
  return line;
}

/**\memberof PNFile
  p5 eof(FH): true at end of file (or a closed handle) */
static PN p5_eof(Potion *P, PN cl, PN self) {
  struct PNFile *f = (struct PNFile *)self;
  struct p5buf *pb;
  if (f->fd < 0) return PN_TRUE;
  pb = p5_buf(f->fd, 0);
  if (pb->len > pb->pos) return PN_FALSE;
  return p5_fill(f) ? PN_FALSE : PN_TRUE;
}

/**\memberof Lobby
  p5 bare `eof` / `eof()`: the handle last read, true when none was */
static PN p5_eof_last(Potion *P, PN cl, PN self) {
  PN fh = potion_send(P->lobby, PN_STR("*^LASTFH"));
  if (!PN_IS_PTR(fh) || PN_VTYPE(fh) != PN_TFILE) return PN_TRUE;
  return p5_eof(P, cl, fh);
}

/**\memberof PNFile
  p5 print FH LIST: write the (already joined) string
  \return true, or false on a closed or failing handle */
static PN p5_write(Potion *P, PN self, PN str) {
  struct PNFile *f = (struct PNFile *)self;
  PN s = p5_tostr(P, str);
  const char *p = PN_STR_PTR(s);
  long left = PN_STR_LEN(s);
  if (f->fd < 0) return PN_FALSE;
  fflush(stdout); /* a duplicate of STDOUT writes to the same place: keep the order */
  while (left > 0) {
    ssize_t r = write(f->fd, p, left);
    if (r == -1) { if (errno == EINTR) continue; return PN_FALSE; }
    p += r; left -= r;
  }
  return PN_TRUE;
}
static PN p5_print(Potion *P, PN cl, PN self, PN str) {
  PN r = p5_write(P, self, str), ors;
  if (r != PN_TRUE) return r;
  ors = potion_send(P->lobby, potion_str(P, "$\\")); /* $\ : the output record separator */
  if (PN_IS_STR(ors) && PN_STR_LEN(ors)) r = p5_write(P, self, ors);
  return r;
}

/**\memberof PNFile
  p5 say FH LIST */
static PN p5_say(Potion *P, PN cl, PN self, PN str) {
  PN r = p5_write(P, self, str);
  if (r == PN_TRUE) r = p5_write(P, self, PN_STR("\n"));
  return r;
}

/**\memberof PNFile
  p5 close(FH)
  \return true, false if already closed */
static PN p5_close(Potion *P, PN cl, PN self) {
  struct PNFile *f = (struct PNFile *)self;
  int r;
  if (f->fd < 0) return PN_FALSE;
  p5_buf(f->fd, 1);
  { int i;
    for (i = 0; i < 64; i++)
      if (p5_pipes[i].fp && p5_pipes[i].fd == f->fd) {
        /* a command pipe: wait for the child, $? is its status */
        int st = pclose(p5_pipes[i].fp);
        p5_pipes[i].fp = NULL;
        f->fd = -1;
        potion_define_global(P, PN_STR("$?"), PN_NUM(st));
        return st == 0 ? PN_TRUE : PN_FALSE;
      } }
  while ((r = close(f->fd)) == -1 && errno == EINTR) ;
  f->fd = -1;
  return PN_TRUE;
}

/* select(FH): the default output handle of print/say without a handle (nil: STDOUT) */
static int p5_sel_on;
int p5_sel_out(Potion *P, const char *str, long len) {
  PN f;
  if (!p5_sel_on) return 0;
  f = potion_send(P->lobby, PN_STR("*P5::SELECTED"));
  if (!(PN_IS_PTR(f) && PN_VTYPE(f) == PN_TFILE)) return 0;
  p5_write(P, f, PN_STRN((char *)str, len));
  return 1;
}
/* select(FH) returns the previously selected handle; select(undef, undef, undef, SECS) sleeps */
static PN p5_select(Potion *P, PN cl, PN self, PN fh, PN b, PN c, PN d) {
  PN prev = potion_send(P->lobby, PN_STR("*P5::SELECTED"));
  if (d == PN_P5NOARG) d = PN_NIL; /* call sites are padded with the missing-argument sentinel */
  if (b == PN_P5NOARG) b = PN_NIL;
  if (PN_IS_INT(d) || PN_IS_DBL(d)) {
    double secs = PN_IS_INT(d) ? (double)PN_INT(d) : PN_DBL(d);
    if (secs > 0) usleep((useconds_t)(secs * 1e6));
    return PN_ZERO;
  }
  if (PN_IS_PTR(fh) && PN_VTYPE(fh) == PN_TFILE) {
    struct PNFile *f = (struct PNFile *)fh;
    p5_sel_on = !(f->fd == 1);
    potion_define_global(P, PN_STR("*P5::SELECTED"), p5_sel_on ? fh : PN_NIL);
  } else if (fh != PN_NIL) {
    p5_sel_on = 0;
    potion_define_global(P, PN_STR("*P5::SELECTED"), PN_NIL);
  }
  return PN_IS_PTR(prev) ? prev : potion_send(P->lobby, PN_STR("*STDOUT"));
}

/* $^I in-place edit: the backup name of PATH for the extension EXT ("bak.*" is a prefix, '*' the
 * basename; without '*' the extension is appended) */
static PN p5_inplace_backup(Potion *P, PN path, PN ext) {
  const char *p = PN_STR_PTR(path), *e = PN_STR_PTR(ext), *base = strrchr(p, '/');
  long dirlen = base ? (long)(base - p) + 1 : 0, n = 0, i;
  char *b = malloc(PN_STR_LEN(path) + PN_STR_LEN(ext) * (PN_STR_LEN(path) + 1) + 2);
  PN r;
  if (!b) potion_allocation_error();
  base = base ? base + 1 : p;
  if (!strchr(e, '*')) {
    memcpy(b, p, PN_STR_LEN(path));
    memcpy(b + PN_STR_LEN(path), e, PN_STR_LEN(ext));
    n = PN_STR_LEN(path) + PN_STR_LEN(ext);
  }
  else {
    int slash = strchr(e, '/') != NULL;
    if (!slash) { memcpy(b, p, dirlen); n = dirlen; }
    for (i = 0; i < (long)PN_STR_LEN(ext); i++) {
      if (e[i] == '*') {
        memcpy(b + n, base, strlen(base));
        n += strlen(base);
      }
      else b[n++] = e[i];
    }
  }
  r = PN_STRN(b, n);
  free(b);
  return r;
}
/* the end of an in-place edited file: close ARGVOUT, print goes to STDOUT again */
static void p5_inplace_end(Potion *P) {
  PN out = potion_send(P->lobby, PN_STR("*ARGVOUT"));
  if (PN_IS_PTR(out) && PN_VTYPE(out) == PN_TFILE) {
    struct PNFile *f = (struct PNFile *)out;
    if (f->fd >= 0) { close(f->fd); f->fd = -1; }
    potion_define_global(P, PN_STR("*ARGVOUT"), PN_NIL);
    potion_define_global(P, PN_STR("*P5::SELECTED"), PN_NIL);
    p5_sel_on = 0;
  }
}
/* start the in-place edit of the file just opened for reading: move it to the backup (or unlink
 * it), create it again as ARGVOUT and select that */
static void p5_inplace_begin(Potion *P, PN path, PN ext) {
  struct stat st;
  PN out;
  mode_t mode = 0666;
  if (stat(PN_STR_PTR(path), &st) == 0) mode = st.st_mode & 07777;
  if (PN_STR_LEN(ext)) {
    PN bak = p5_inplace_backup(P, path, ext);
    if (!strcmp(PN_STR_PTR(path), PN_STR_PTR(bak)) || rename(PN_STR_PTR(path), PN_STR_PTR(bak)) != 0) unlink(PN_STR_PTR(path)); /* $^I = '*': the backup is the file itself */
  } else unlink(PN_STR_PTR(path));
  out = p5_open3(P, PN_NIL, PN_NIL, PN_STR(">"), path);
  if (out == PN_NIL || out == PN_FALSE) return;
  chmod(PN_STR_PTR(path), mode);
  potion_define_global(P, PN_STR("*ARGVOUT"), out);
  potion_define_global(P, PN_STR("*P5::SELECTED"), out);
  p5_sel_on = 1;
}

/**\memberof Lobby
  p5 diamond operator: read the next record from @ARGV files, or STDIN when
  @ARGV is empty before the first read. Files are consumed from @ARGV.
*/
static PN p5_argv_read(Potion *P, PN cl, PN self, PN argv) {
  PN active = potion_send(P->lobby, PN_STR("$P5::ARGV_LIST"));
  PN file = potion_send(P->lobby, PN_STR("*ARGV"));
  PN input = potion_send(P->lobby, PN_STR("*STDIN"));
  if (!PN_IS_TUPLE(argv)) argv = PN_TUP0();
  if (active != argv) {
    if (active == PN_NIL && PN_IS_PTR(file) && PN_VTYPE(file) == PN_TFILE) { /* *ARGV = *DATA: the first <> reads that handle */
      potion_define_global(P, PN_STR("$P5::ARGV_LIST"), argv);
    } else {
    if (PN_IS_PTR(file) && PN_VTYPE(file) == PN_TFILE && file != input)
      p5_close(P, cl, file);
    file = PN_NIL;
    potion_define_global(P, PN_STR("*ARGV"), PN_NIL);
    potion_define_global(P, PN_STR("$P5::ARGV_LIST"), argv);
    p5_argv_lines = 0;
    }
  }
  for (;;) {
    if (PN_IS_PTR(file) && PN_VTYPE(file) == PN_TFILE) {
      PN line = p5_readline(P, cl, file);
      if (line != PN_NIL) { potion_define_global(P, PN_STR("$."), PN_NUM(++p5_argv_lines)); return line; } /* $. runs on over all the files */
      if (file != input) p5_close(P, cl, file);
      p5_inplace_end(P);
      potion_define_global(P, PN_STR("*ARGV"), PN_FALSE);
      file = PN_FALSE;
    }
    if (PN_TUPLE_LEN(argv) > 0) {
      PN path = potion_tuple_shift(P, cl, argv);
      file = p5_open3(P, cl, self, PN_STR("<"), path);
      if (file == PN_NIL) {
        file = PN_FALSE;
        continue;
      }
      potion_define_global(P, PN_STR("*ARGV"), file);
      potion_define_global(P, PN_STR("$ARGV"), ((struct PNFile *)file)->path);
      { PN ext = potion_send(P->lobby, PN_STR("$^I")); /* in-place edit */
        if (PN_IS_STR(ext)) p5_inplace_begin(P, path, ext); }
      continue;
    }
    if (file == PN_NIL) {
      file = input;
      potion_define_global(P, PN_STR("*ARGV"), file);
      continue;
    }
    return PN_NIL;
  }
}

/* <> / <FH> in list context: all remaining records */
static PN p5_argv_all(Potion *P, PN cl, PN self, PN argv) {
  PN out = PN_TUP0(), line;
  while ((line = p5_argv_read(P, cl, self, argv)) != PN_NIL) out = PN_PUSH(out, line);
  return out;
}
static PN p5_read_all(Potion *P, PN cl, PN self) {
  PN out = PN_TUP0(), line;
  while ((line = p5_readline(P, cl, self)) != PN_NIL) out = PN_PUSH(out, line);
  return out;
}

/* the bareword handle STDIN: fd 0 */
static void p5_std_in(Potion *P) {
  struct PNFile *file = (struct PNFile *)potion_object_new(P, PN_NIL, PN_VTABLE(PN_TFILE));
  file->fd = 0;
  file->path = PN_STR("STDIN");
  PN_TOUCH(file);
  file->mode = O_RDONLY;
  potion_define_global(P, PN_STR("*STDIN"), (PN)file);
  { /* STDOUT and STDERR as handle values: \*STDOUT, open(FH, ">&", \*STDERR); print STDOUT / STDERR LIST keep their own paths */
    static const char *const nm[2] = { "STDOUT", "STDERR" };
    int i;
    for (i = 0; i < 2; i++) {
      struct PNFile *f = (struct PNFile *)potion_object_new(P, PN_NIL, PN_VTABLE(PN_TFILE));
      f->fd = i + 1;
      f->path = PN_STR(nm[i]);
      PN_TOUCH(f);
      f->mode = O_WRONLY;
      potion_define_global(P, PN_STR(i ? "*STDERR" : "*STDOUT"), (PN)f);
    }
  }
}

/**\memberof Lobby
  the text after __END__/__DATA__ becomes the handle DATA: an unlinked temporary file
  \return the new PNFile */
static PN p5_data(Potion *P, PN cl, PN self, PN text) {
  struct PNFile *file;
  FILE *tmp = tmpfile();
  int fd;
  if (!tmp) return PN_NIL;
  fd = dup(fileno(tmp));
  if (fwrite(PN_STR_PTR(text), 1, PN_STR_LEN(text), tmp) != (size_t)PN_STR_LEN(text)) { fclose(tmp); return PN_NIL; }
  fflush(tmp);
  lseek(fd, 0, SEEK_SET);
  fclose(tmp);
  p5_buf(fd, 1);
  file = (struct PNFile *)potion_object_new(P, PN_NIL, PN_VTABLE(PN_TFILE));
  file->fd = fd;
  file->path = PN_STR("DATA");
  PN_TOUCH(file);
  file->mode = O_RDONLY;
  potion_define_global(P, PN_STR("*DATA"), (PN)file);
  { /* `*ARGV = *DATA` ran in a BEGIN block, before the handle existed */
    PN alias = potion_bind(P, P->lobby, PN_STR("$P5::DATA_ALIAS")) == PN_NIL ? PN_NIL : potion_send(P->lobby, PN_STR("$P5::DATA_ALIAS"));
    if (PN_IS_STR(alias)) potion_define_global(P, PN_STRCAT("*", PN_STR_PTR(alias)), (PN)file);
  }
  return (PN)file;
}

/* -e -f -d -r -w -x -o -R -W -X -O -z -s -l -p -S -b -c -u -g -k -t -T -B -M -A -C FILE: FILE is a path or an open handle,
 * "_" is the file of the last test. Undef when the file does not exist, "" for false. */
static time_t p5_basetime;
static PN p5_ftest(Potion *P, PN cl, PN self, PN op, PN arg) {
  static struct stat st;
  static char lastpath[1024];
  int o = PN_STR_PTR(op)[0], ok;
  const char *path = NULL;
  PN sv = PN_NIL;
  if (!p5_basetime) p5_basetime = time(NULL);
  if (PN_IS_PTR(arg) && PN_VTYPE(arg) == PN_TFILE) {
    struct PNFile *f = (struct PNFile *)arg;
    if (o == 't') return isatty(f->fd) ? PN_NUM(1) : PN_STR0;
    ok = fstat(f->fd, &st) == 0;
    if (ok && PN_IS_STR(f->path)) { strncpy(lastpath, PN_STR_PTR(f->path), sizeof lastpath - 1); path = lastpath; }
  } else {
    sv = p5_tostr(P, arg);
    if (PN_STR_LEN(sv) == 1 && PN_STR_PTR(sv)[0] == '_') { ok = lastpath[0] != 0; path = lastpath; if (ok) ok = (o == 'l' ? lstat(path, &st) : stat(path, &st)) == 0; }
    else {
      path = PN_STR_PTR(sv);
      ok = (o == 'l' ? lstat(path, &st) : stat(path, &st)) == 0;
      strncpy(lastpath, path, sizeof lastpath - 1);
      path = lastpath;
    }
  }
  if (o == 't') return PN_STR0;
  if (!ok) { return PN_NIL; }
  switch (o) {
  case 'e': return PN_NUM(1);
  case 'f': return S_ISREG(st.st_mode) ? PN_NUM(1) : PN_STR0;
  case 'd': return S_ISDIR(st.st_mode) ? PN_NUM(1) : PN_STR0;
  case 'l': return S_ISLNK(st.st_mode) ? PN_NUM(1) : PN_STR0;
  case 'p': return S_ISFIFO(st.st_mode) ? PN_NUM(1) : PN_STR0;
  case 'S': return S_ISSOCK(st.st_mode) ? PN_NUM(1) : PN_STR0;
  case 'b': return S_ISBLK(st.st_mode) ? PN_NUM(1) : PN_STR0;
  case 'c': return S_ISCHR(st.st_mode) ? PN_NUM(1) : PN_STR0;
  case 'u': return st.st_mode & S_ISUID ? PN_NUM(1) : PN_STR0;
  case 'g': return st.st_mode & S_ISGID ? PN_NUM(1) : PN_STR0;
  case 'k': return st.st_mode & S_ISVTX ? PN_NUM(1) : PN_STR0;
  case 'z': return st.st_size == 0 ? PN_NUM(1) : PN_STR0;
  case 's': return st.st_size > 0 ? PN_NUM((long)st.st_size) : PN_STR0;
  case 'o': case 'O': return st.st_uid == geteuid() ? PN_NUM(1) : PN_STR0;
  case 'r': case 'R': return path && access(path, R_OK) == 0 ? PN_NUM(1) : PN_STR0;
  case 'w': case 'W': return path && access(path, W_OK) == 0 ? PN_NUM(1) : PN_STR0;
  case 'x': case 'X': return path && access(path, X_OK) == 0 ? PN_NUM(1) : PN_STR0;
  case 'M': return potion_double(P, (double)(p5_basetime - st.st_mtime) / 86400.0);
  case 'A': return potion_double(P, (double)(p5_basetime - st.st_atime) / 86400.0);
  case 'C': return potion_double(P, (double)(p5_basetime - st.st_ctime) / 86400.0);
  case 'T': case 'B': {
    unsigned char buf[512];
    FILE *fp = path ? fopen(path, "rb") : NULL;
    size_t n, i, odd = 0;
    if (!fp) return PN_STR0;
    n = fread(buf, 1, sizeof buf, fp);
    fclose(fp);
    if (n == 0) return PN_NUM(1); /* an empty file is both */
    for (i = 0; i < n; i++) {
      if (buf[i] == 0) { odd = n; break; }
      if (buf[i] < 32 && buf[i] != '\n' && buf[i] != '\r' && buf[i] != '\t' && buf[i] != '\b' && buf[i] != '\f' && buf[i] != 27) odd++;
    }
    return (o == 'T') == (odd * 3 < n) ? PN_NUM(1) : PN_STR0;
  }
  }
  return PN_STR0;
}

/* ---- file system operations: chmod chown utime mkdir rmdir chdir rename link symlink readlink truncate stat lstat
 * opendir readdir closedir rewinddir. They return the perl values (true/false, counts, lists); a failure sets $! to
 * the error text. A file argument is a path or an open handle (fchmod/fchown/futimes/fstat/ftruncate). */
static PN p5_oserr(Potion *P) {
  potion_define_global(P, PN_STR("$!"), PN_STR(strerror(errno)));
  return PN_FALSE;
}
static int p5_isfh(PN a) { return PN_IS_PTR(a) && PN_VTYPE(a) == PN_TFILE && ((struct PNFile *)a)->fd >= 0; }
/* the C path of a path argument (a pointer into the GC string, use it before the next allocation) */
static const char *p5_cpath(Potion *P, PN a) { a = p5_tostr(P, a); return PN_IS_STR(a) ? PN_STR_PTR(a) : ""; }

static PN p5_mkdir(Potion *P, PN cl, PN self, PN path, PN mode) {
  long m = PN_IS_INT(mode) ? PN_INT(mode) : 0777;
#ifdef _WIN32
  if (mkdir(p5_cpath(P, path)) == 0) return PN_NUM(1);
  (void)m;
#else
  if (mkdir(p5_cpath(P, path), (mode_t)m) == 0) return PN_NUM(1);
#endif
  return p5_oserr(P);
}
static PN p5_rmdir(Potion *P, PN cl, PN self, PN path) { return rmdir(p5_cpath(P, path)) == 0 ? PN_NUM(1) : p5_oserr(P); }
static PN p5_chdir(Potion *P, PN cl, PN self, PN path) { return chdir(p5_cpath(P, path)) == 0 ? PN_NUM(1) : p5_oserr(P); }
static PN p5_rename(Potion *P, PN cl, PN self, PN a, PN b) {
  char from[1024];
  snprintf(from, sizeof from, "%s", p5_cpath(P, a));
  return rename(from, p5_cpath(P, b)) == 0 ? PN_NUM(1) : p5_oserr(P);
}
static PN p5_link(Potion *P, PN cl, PN self, PN a, PN b) {
#ifdef _WIN32
  errno = ENOSYS; return p5_oserr(P);
#else
  char from[1024];
  snprintf(from, sizeof from, "%s", p5_cpath(P, a));
  return link(from, p5_cpath(P, b)) == 0 ? PN_NUM(1) : p5_oserr(P);
#endif
}
static PN p5_symlink(Potion *P, PN cl, PN self, PN a, PN b) {
#ifdef _WIN32
  errno = ENOSYS; return p5_oserr(P);
#else
  char from[1024];
  snprintf(from, sizeof from, "%s", p5_cpath(P, a));
  return symlink(from, p5_cpath(P, b)) == 0 ? PN_NUM(1) : p5_oserr(P);
#endif
}
static PN p5_readlink(Potion *P, PN cl, PN self, PN path) {
#ifdef _WIN32
  return PN_NIL;
#else
  char buf[4096];
  ssize_t n = readlink(p5_cpath(P, path), buf, sizeof buf - 1);
  if (n < 0) { p5_oserr(P); return PN_NIL; }
  return potion_str2(P, buf, (PN_SIZE)n);
#endif
}
static PN p5_truncate(Potion *P, PN cl, PN self, PN file, PN len) {
  off_t n = PN_IS_INT(len) ? (off_t)PN_INT(len) : 0;
  int r;
  if (p5_isfh(file)) r = ftruncate(((struct PNFile *)file)->fd, n);
  else
#ifdef _WIN32
    { int fd = open(p5_cpath(P, file), O_WRONLY | O_BINARY); r = fd < 0 ? -1 : chsize(fd, (long)n); if (fd >= 0) close(fd); }
#else
    r = truncate(p5_cpath(P, file), n);
#endif
  return r == 0 ? PN_NUM(1) : p5_oserr(P);
}
/* chmod MODE, LIST: the number of files changed */
static PN p5_chmod(Potion *P, PN cl, PN self, PN mode, PN a, PN b, PN c, PN d, PN e, PN f) {
  PN args[6] = { a, b, c, d, e, f };
  long i, n = 0, m = PN_IS_INT(mode) ? PN_INT(mode) : 0;
  for (i = 0; i < 6; i++) {
    int ok;
    if (args[i] == PN_NIL) continue;
    if (p5_isfh(args[i])) {
#ifdef _WIN32
      ok = 0; errno = ENOSYS;
#else
      ok = fchmod(((struct PNFile *)args[i])->fd, (mode_t)m) == 0;
#endif
    } else ok = chmod(p5_cpath(P, args[i]), (mode_t)m) == 0;
    if (ok) n++; else p5_oserr(P);
  }
  return PN_NUM(n);
}
/* chown UID, GID, LIST (-1 leaves it alone) */
static PN p5_chown(Potion *P, PN cl, PN self, PN uid, PN gid, PN a, PN b, PN c, PN d, PN e, PN f) {
  PN args[6] = { a, b, c, d, e, f };
  long i, n = 0;
#ifdef _WIN32
  errno = ENOSYS; p5_oserr(P); return PN_NUM(0);
#else
  uid_t u = PN_IS_INT(uid) ? (uid_t)PN_INT(uid) : (uid_t)-1;
  gid_t g = PN_IS_INT(gid) ? (gid_t)PN_INT(gid) : (gid_t)-1;
  for (i = 0; i < 6; i++) {
    int ok;
    if (args[i] == PN_NIL) continue;
    ok = p5_isfh(args[i]) ? fchown(((struct PNFile *)args[i])->fd, u, g) == 0 : chown(p5_cpath(P, args[i]), u, g) == 0;
    if (ok) n++; else p5_oserr(P);
  }
  return PN_NUM(n);
#endif
}
static double p5_tsec(PN v) { return PN_IS_INT(v) ? (double)PN_INT(v) : PN_IS_DBL(v) ? PN_DBL(v) : -1; }
/* utime ATIME, MTIME, LIST (both undef: now) */
static PN p5_utime(Potion *P, PN cl, PN self, PN at, PN mt, PN a, PN b, PN c, PN d, PN e, PN f) {
  PN args[6] = { a, b, c, d, e, f };
  long i, n = 0;
  int now = at == PN_NIL && mt == PN_NIL;
  double as = p5_tsec(at), ms = p5_tsec(mt);
#ifdef _WIN32
  struct utimbuf ub;
  ub.actime = (time_t)as; ub.modtime = (time_t)ms;
#else
  struct timeval tv[2];
  tv[0].tv_sec = (time_t)as; tv[0].tv_usec = (long)((as - (double)(time_t)as) * 1e6);
  tv[1].tv_sec = (time_t)ms; tv[1].tv_usec = (long)((ms - (double)(time_t)ms) * 1e6);
#endif
  for (i = 0; i < 6; i++) {
    int ok;
    if (args[i] == PN_NIL) continue;
#ifdef _WIN32
    ok = !p5_isfh(args[i]) && utime(p5_cpath(P, args[i]), now ? NULL : &ub) == 0;
#else
    ok = p5_isfh(args[i]) ? futimes(((struct PNFile *)args[i])->fd, now ? NULL : tv) == 0 : utimes(p5_cpath(P, args[i]), now ? NULL : tv) == 0;
#endif
    if (ok) n++; else p5_oserr(P);
  }
  return PN_NUM(n);
}
/* stat / lstat FILE: the 13 element list (dev ino mode nlink uid gid rdev size atime mtime ctime blksize blocks), empty on failure */
static PN p5_statlist(Potion *P, PN file, int l) {
  struct stat st;
  int ok;
  PN out;
  if (p5_isfh(file)) ok = fstat(((struct PNFile *)file)->fd, &st) == 0;
  else ok = (l ? lstat(p5_cpath(P, file), &st) : stat(p5_cpath(P, file), &st)) == 0;
  if (!ok) { p5_oserr(P); return PN_TUP0(); }
  out = PN_TUP0();
  out = PN_PUSH(out, PN_NUM((long)st.st_dev));
  out = PN_PUSH(out, PN_NUM((long)st.st_ino));
  out = PN_PUSH(out, PN_NUM((long)st.st_mode));
  out = PN_PUSH(out, PN_NUM((long)st.st_nlink));
  out = PN_PUSH(out, PN_NUM((long)st.st_uid));
  out = PN_PUSH(out, PN_NUM((long)st.st_gid));
  out = PN_PUSH(out, PN_NUM((long)st.st_rdev));
  out = PN_PUSH(out, PN_NUM((long)st.st_size));
  out = PN_PUSH(out, PN_NUM((long)st.st_atime));
  out = PN_PUSH(out, PN_NUM((long)st.st_mtime));
  out = PN_PUSH(out, PN_NUM((long)st.st_ctime));
#ifdef _WIN32
  out = PN_PUSH(out, PN_NUM(4096));
  out = PN_PUSH(out, PN_NUM(((long)st.st_size + 511) / 512));
#else
  out = PN_PUSH(out, PN_NUM((long)st.st_blksize));
  out = PN_PUSH(out, PN_NUM((long)st.st_blocks));
#endif
  return out;
}
static PN p5_stat(Potion *P, PN cl, PN self, PN file) { return p5_statlist(P, file, 0); }
static PN p5_lstat(Potion *P, PN cl, PN self, PN file) { return p5_statlist(P, file, 1); }
/* a directory handle is a PNFile with fd -1, the entry names in path and the read position in mode */
static PN p5_opendir(Potion *P, PN cl, PN self, PN path) {
  DIR *d = opendir(p5_cpath(P, path));
  struct dirent *de;
  PN names = PN_TUP0();
  struct PNFile *h;
  if (!d) { p5_oserr(P); return PN_NIL; }
  while ((de = readdir(d))) names = PN_PUSH(names, potion_str(P, de->d_name));
  closedir(d);
  h = (struct PNFile *)potion_object_new(P, PN_NIL, PN_VTABLE(PN_TFILE));
  h->fd = -1;
  h->path = names;
  PN_TOUCH(h);
  h->mode = 0;
  return (PN)h;
}
static int p5_isdirh(PN h) { return PN_IS_PTR(h) && PN_VTYPE(h) == PN_TFILE && ((struct PNFile *)h)->fd == -1 && PN_IS_TUPLE(((struct PNFile *)h)->path); }
static PN p5_readdir(Potion *P, PN cl, PN self) {
  struct PNFile *h = (struct PNFile *)self;
  if (!p5_isdirh(self) || h->mode >= PN_TUPLE_LEN(h->path)) return PN_NIL;
  return PN_TUPLE_AT(h->path, h->mode++);
}
static PN p5_readdirall(Potion *P, PN cl, PN self) {
  struct PNFile *h = (struct PNFile *)self;
  PN out = PN_TUP0();
  if (!p5_isdirh(self)) return out;
  while (h->mode < PN_TUPLE_LEN(h->path)) out = PN_PUSH(out, PN_TUPLE_AT(h->path, h->mode++));
  return out;
}
static PN p5_rewinddir(Potion *P, PN cl, PN self) { if (p5_isdirh(self)) ((struct PNFile *)self)->mode = 0; return PN_NUM(1); }
static PN p5_closedir(Potion *P, PN cl, PN self) {
  if (!p5_isdirh(self)) return PN_FALSE;
  ((struct PNFile *)self)->path = PN_NIL;
  return PN_NUM(1);
}

/* fileno(FH): the file descriptor (STDIN/STDOUT/STDERR are 0/1/2 in the parser), undef for a closed or directory handle */
static PN p5_fileno(Potion *P, PN cl, PN self, PN h) {
  if (PN_IS_PTR(h) && PN_VTYPE(h) == PN_TFILE && ((struct PNFile *)h)->fd >= 0) return PN_NUM(((struct PNFile *)h)->fd);
  return PN_NIL;
}
/* getc(FH): the next byte as a one character string, undef at the end */
static PN p5_getc(Potion *P, PN cl, PN self, PN h) {
  struct PNFile *f = (struct PNFile *)h;
  struct p5buf *pb;
  char c;
  if (!(PN_IS_PTR(h) && PN_VTYPE(h) == PN_TFILE) || f->fd < 0) return PN_NIL;
  pb = p5_buf(f->fd, 0);
  if (pb->pos >= pb->len && !p5_fill(f)) return PN_NIL;
  pb = p5_buf(f->fd, 0);
  c = pb->b[pb->pos++];
  return potion_str2(P, &c, 1);
}
/* flock(FH, OP): advisory lock, 1 or false */
static PN p5_flock(Potion *P, PN cl, PN self, PN h, PN op) {
#if defined(_WIN32) || !defined(LOCK_SH)
  return PN_IS_PTR(h) && PN_VTYPE(h) == PN_TFILE ? PN_NUM(1) : PN_FALSE;
#else
  int r;
  if (!(PN_IS_PTR(h) && PN_VTYPE(h) == PN_TFILE) || ((struct PNFile *)h)->fd < 0) return PN_FALSE;
  while ((r = flock(((struct PNFile *)h)->fd, PN_IS_INT(op) ? (int)PN_INT(op) : LOCK_SH)) == -1 && errno == EINTR) ;
  return r == 0 ? PN_NUM(1) : p5_oserr(P);
#endif
}

/* glob(PATTERNS): the files matching the blank separated patterns, sorted per pattern; a pattern without
 * wildcards is returned as it is (braces and ~ are expanded) */
static PN p5_glob(Potion *P, PN cl, PN self, PN pat) {
  PN out = PN_TUP0(), sv = p5_tostr(P, pat);
  char *copy, *tok, *save = NULL;
  if (!PN_IS_STR(sv)) return out;
  copy = strdup(PN_STR_PTR(sv));
  if (!copy) potion_allocation_error();
  for (tok = strtok_r(copy, " \t\n", &save); tok; tok = strtok_r(NULL, " \t\n", &save)) {
#ifdef _WIN32
    out = PN_PUSH(out, potion_str(P, tok));
#else
    glob_t g;
    size_t i;
    int fl = 0;
# ifdef GLOB_NOMAGIC
    fl |= GLOB_NOMAGIC;
# endif
# ifdef GLOB_BRACE
    fl |= GLOB_BRACE;
# endif
# ifdef GLOB_TILDE
    fl |= GLOB_TILDE;
# endif
    memset(&g, 0, sizeof g);
    if (glob(tok, fl, NULL, &g) == 0)
      for (i = 0; i < g.gl_pathc; i++) out = PN_PUSH(out, potion_str(P, g.gl_pathv[i]));
    globfree(&g);
#endif
  }
  free(copy);
  return out;
}

void p5_file_init(Potion *P) {
  potion_define_global(P, PN_STR("$/"), PN_STR("\n")); /* the input record separator */
  potion_define_global(P, PN_STR("*ARGV"), PN_NIL);
  potion_define_global(P, PN_STR("$ARGV"), PN_NIL);
  potion_define_global(P, PN_STR("$P5::ARGV_LIST"), PN_NIL);
  potion_define_global(P, PN_STR("*P5::SELECTED"), PN_NIL);
  potion_method(P->lobby, "select", p5_select, "fh=o,b=o,c=o,d=o");
  potion_method(P->lobby, "p5ftest", p5_ftest, "op=o,arg=o");
  potion_method(P->lobby, "p5data", p5_data, "text=o");
  potion_method(P->lobby, "p5argvread", p5_argv_read, "argv=o");
  potion_method(P->lobby, "p5argvall", p5_argv_all, "argv=o");
  PN file_vt = PN_VTABLE(PN_TFILE);
  potion_method(P->lobby, "p5open2", p5_open2, "spec=o");
  potion_method(P->lobby, "p5open3", p5_open3, "mode=o,path=o");
  potion_method(file_vt, "p5readline", p5_readline, 0);
  potion_method(file_vt, "p5readall", p5_read_all, 0);
  potion_method(file_vt, "p5eof", p5_eof, 0);
  potion_method(P->lobby, "p5eofl", p5_eof_last, 0);
  potion_method(file_vt, "p5print", p5_print, "str=o");
  potion_method(file_vt, "p5say", p5_say, "str=o");
  potion_method(file_vt, "p5close", p5_close, 0);
  potion_method(file_vt, "p5seek", p5_seek, "pos=o,whence=o");
  potion_method(file_vt, "p5tell", p5_tell, 0);
  potion_method(file_vt, "p5readbuf", p5_readbuf, "buf=o,len=o,off=o");
  potion_method(P->lobby, "p5readn", p5_readn, "x=o");
  potion_method(P->lobby, "glob", p5_glob, "pat=o");
  potion_method(P->lobby, "fileno", p5_fileno, "h=o");
  potion_method(P->lobby, "getc", p5_getc, "h=o");
  potion_method(P->lobby, "flock", p5_flock, "h=o,op=o");
  potion_method(P->lobby, "mkdir", p5_mkdir, "path=o,mode=o");
  potion_method(P->lobby, "rmdir", p5_rmdir, "path=o");
  potion_method(P->lobby, "chdir", p5_chdir, "path=o");
  potion_method(P->lobby, "rename", p5_rename, "a=o,b=o");
  potion_method(P->lobby, "link", p5_link, "a=o,b=o");
  potion_method(P->lobby, "symlink", p5_symlink, "a=o,b=o");
  potion_method(P->lobby, "readlink", p5_readlink, "path=o");
  potion_method(P->lobby, "truncate", p5_truncate, "file=o,len=o");
  potion_method(P->lobby, "chmod", p5_chmod, "mode=o,a=o,b=o,c=o,d=o,e=o,f=o");
  potion_method(P->lobby, "chown", p5_chown, "uid=o,gid=o,a=o,b=o,c=o,d=o,e=o,f=o");
  potion_method(P->lobby, "utime", p5_utime, "at=o,mt=o,a=o,b=o,c=o,d=o,e=o,f=o");
  potion_method(P->lobby, "stat", p5_stat, "file=o");
  potion_method(P->lobby, "lstat", p5_lstat, "file=o");
  potion_method(P->lobby, "opendir", p5_opendir, "path=o");
  potion_method(file_vt, "p5readdir", p5_readdir, 0);
  potion_method(file_vt, "p5readdirall", p5_readdirall, 0);
  potion_method(file_vt, "p5rewinddir", p5_rewinddir, 0);
  potion_method(file_vt, "p5closedir", p5_closedir, 0);
  p5_std_in(P);
}
