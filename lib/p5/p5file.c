/**\file lib/p5/p5file.c
  p5 file handles on top of the core PNFile (unbuffered fd IO): 2/3-arg open,
  <FH> readline, eof, print/say FH LIST and close.

  The core PNFile has no read buffer; p5 readline keeps its pending bytes in a
  side table indexed by file descriptor, released by p5close or when the fd is
  reopened.

  (c) 2026 perl11 org */
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include "p5.h"

struct p5buf { char *b; long len, pos; };
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
  if (!PN_IS_STR(path)) path = potion_send(path, PN_string);
  if ((fd = open(PN_STR_PTR(path), flags, 0666)) == -1) {
    potion_define_global(P, PN_STR("$!"), PN_STR(strerror(errno)));
    return PN_NIL;
  }
  p5_buf(fd, 1);
  file = (struct PNFile *)potion_object_new(P, PN_NIL, PN_VTABLE(PN_TFILE));
  file->fd = fd;
  file->path = path;
  file->mode = flags;
  return (PN)file;
}

/**\memberof Lobby
  p5 2-arg open(FH, "<path"), ">path", ">>path", "path" (read).
  \return a new PNFile, or PN_NIL on error */
static PN p5_open2(Potion *P, PN cl, PN self, PN spec) {
  const char *s = PN_STR_PTR(spec);
  long n = 0;
  while (*s == ' ') s++;
  while (s[n] == '+' || s[n] == '<' || s[n] == '>') n++;
  if (!n) return p5_open3(P, cl, self, PN_STR("<"), PN_STR(s));
  {
    const char *p = s + n;
    while (*p == ' ') p++;
    return p5_open3(P, cl, self, PN_STRN((char *)s, n), PN_STR(p));
  }
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
static PN p5_readline(Potion *P, PN cl, PN self) {
  struct PNFile *f = (struct PNFile *)self;
  long scan = 0;
  if (f->fd < 0) return PN_NIL;
  for (;;) {
    struct p5buf *pb = p5_buf(f->fd, 0);
    char *nl = pb->b ? memchr(pb->b + pb->pos + scan, '\n', pb->len - pb->pos - scan) : NULL;
    if (nl) {
      long n = nl - (pb->b + pb->pos) + 1;
      PN s = PN_STRN(pb->b + pb->pos, n);
      pb->pos += n;
      return s;
    }
    scan = pb->len - pb->pos;
    if (!p5_fill(f)) {
      pb = p5_buf(f->fd, 0);
      if (pb->len > pb->pos) {
        PN s = PN_STRN(pb->b + pb->pos, pb->len - pb->pos);
        pb->pos = pb->len;
        return s;
      }
      return PN_NIL;
    }
  }
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
static PN p5_print(Potion *P, PN cl, PN self, PN str) {
  struct PNFile *f = (struct PNFile *)self;
  PN s = PN_IS_STR(str) ? str : potion_send(str, PN_string);
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

/**\memberof PNFile
  p5 say FH LIST */
static PN p5_say(Potion *P, PN cl, PN self, PN str) {
  PN r = p5_print(P, cl, self, str);
  if (r == PN_TRUE) r = p5_print(P, cl, self, PN_STR("\n"));
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

void p5_file_init(Potion *P) {
  PN file_vt = PN_VTABLE(PN_TFILE);
  potion_method(P->lobby, "p5open2", p5_open2, "spec=o");
  potion_method(P->lobby, "p5open3", p5_open3, "mode=o,path=o");
  potion_method(file_vt, "p5readline", p5_readline, 0);
  potion_method(file_vt, "p5eof", p5_eof, 0);
  potion_method(file_vt, "p5print", p5_print, "str=o");
  potion_method(file_vt, "p5say", p5_say, "str=o");
  potion_method(file_vt, "p5close", p5_close, 0);
}
