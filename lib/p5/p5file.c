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
#include "p5.h"

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
static PN p5_open3(Potion *P, PN cl, PN self, PN mode, PN path) {
  const char *m = PN_STR_PTR(mode);
  struct PNFile *file;
  int fd, flags;
  while (*m == ' ') m++;
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
  while (s[n] == '+' || s[n] == '<' || s[n] == '>') n++;
  if (!n) return p5_open3(P, cl, self, PN_STR("<"), PN_STR(s));
  p = s + n;
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
    else if (PN_STR_LEN(rs) < sizeof sepb) { memcpy(sepb, PN_STR_PTR(rs), PN_STR_LEN(rs)); m = PN_STR_LEN(rs); }
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
  PN line = p5_readline1(P, cl, self);
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

/**\memberof PNFile
  p5 print FH LIST: write the (already joined) string
  \return true, or false on a closed or failing handle */
static PN p5_write(Potion *P, PN self, PN str) {
  struct PNFile *f = (struct PNFile *)self;
  PN s = p5_tostr(P, str);
  const char *p = PN_STR_PTR(s);
  long left = PN_STR_LEN(s);
  if (f->fd < 0) return PN_FALSE;
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
  if (!strchr(e, '*')) { memcpy(b, p, PN_STR_LEN(path)); memcpy(b + PN_STR_LEN(path), e, PN_STR_LEN(ext)); n = PN_STR_LEN(path) + PN_STR_LEN(ext); }
  else {
    int slash = strchr(e, '/') != NULL;
    if (!slash) { memcpy(b, p, dirlen); n = dirlen; }
    for (i = 0; i < (long)PN_STR_LEN(ext); i++) {
      if (e[i] == '*') { memcpy(b + n, base, strlen(base)); n += strlen(base); }
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

void p5_file_init(Potion *P) {
  potion_define_global(P, PN_STR("$/"), PN_STR("\n")); /* the input record separator */
  potion_define_global(P, PN_STR("*ARGV"), PN_NIL);
  potion_define_global(P, PN_STR("$ARGV"), PN_NIL);
  potion_define_global(P, PN_STR("$P5::ARGV_LIST"), PN_NIL);
  potion_define_global(P, PN_STR("*P5::SELECTED"), PN_NIL);
  potion_method(P->lobby, "select", p5_select, "fh=o,b=o,c=o,d=o");
  potion_method(P->lobby, "p5data", p5_data, "text=o");
  potion_method(P->lobby, "p5argvread", p5_argv_read, "argv=o");
  potion_method(P->lobby, "p5argvall", p5_argv_all, "argv=o");
  PN file_vt = PN_VTABLE(PN_TFILE);
  potion_method(P->lobby, "p5open2", p5_open2, "spec=o");
  potion_method(P->lobby, "p5open3", p5_open3, "mode=o,path=o");
  potion_method(file_vt, "p5readline", p5_readline, 0);
  potion_method(file_vt, "p5readall", p5_read_all, 0);
  potion_method(file_vt, "p5eof", p5_eof, 0);
  potion_method(file_vt, "p5print", p5_print, "str=o");
  potion_method(file_vt, "p5say", p5_say, "str=o");
  potion_method(file_vt, "p5close", p5_close, 0);
  p5_std_in(P);
}
