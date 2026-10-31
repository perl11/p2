/** Compile Proto instructions to C functions, using the ordinary Potion object ABI.
 * Metadata is rebuilt by generated C; no parser, bytecode dispatch or JIT is used.
 * (c) 2026 perl11.com */
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <math.h>
#include "compile-c.h"
#include "table.h"
#include "opcodes.h"
#include "asm.h"

/* Walk a flat signature without constructing temporary signature tuples.
 * Used by potion_c_call and by potion_call_var (internal.c): a call that
 * omits optional arguments must have the tail filled from the signature
 * before it reaches a callee that reads a fixed number of parameters. */
void potion_c_defaults(PN sig, int from, int to, PN *args) {
  PN_SIZE j = 0, n = PN_IS_TUPLE(sig) ? PN_TUPLE_LEN(sig) : 0;
  int index = 0;
  for (int i = from; i < to; i++) args[i - from] = PN_NIL;
  while (j < n) {
    PN typ = PN_NIL, val = PN_NIL;
    int has_default = 0;
    PN name = PN_TUPLE_AT(sig, j++);
    if (!PN_IS_STR(name)) continue;
    if (j < n && PN_IS_INT(PN_TUPLE_AT(sig, j))) {
      typ = PN_TUPLE_AT(sig, j++);
      if (typ == PN_NUM(':') && j < n) {
        val = PN_TUPLE_AT(sig, j++);
        has_default = 1;
      }
    }
    if (index >= from && index < to)
      args[index - from] = has_default ? val : potion_type_default(PN_IS_INT(typ) ? PN_INT(typ) : 0);
    if (++index >= to) break;
  }
}

PN potion_c_call(Potion *P, PN cl, PN_SIZE argc, PN *argv) {
  int arity = PN_CLOSURE(cl)->arity;
  PN_SIZE given = argc ? argc - 1 : 0;
  PN_C_F fn = PN_PROTO(PN_CLOSURE(cl)->data[0])->cfunc;
  PN self = argc ? argv[0] : cl;
  if (PN_IS_TUPLE(PN_CLOSURE(cl)->sig) && given && given < PN_CLOSURE(cl)->minargs)
    return potion_error(P, potion_str(P, "Not enough arguments to C-compiled closure"), 0, 0, 0);
#ifndef P2
  if (PN_IS_TUPLE(PN_CLOSURE(cl)->sig) && given > arity)
    return potion_error(P, potion_str(P, "Too many arguments to C-compiled closure"), 0, 0, 0);
#endif
  if (given >= arity) return fn(P, cl, self, argc ? argv + 1 : NULL);
  PN args[arity ? arity : 1];
  for (PN_SIZE i = 0; i < given; i++) args[i] = argv[i + 1];
  potion_c_defaults(PN_CLOSURE(cl)->sig, given, arity, args + given);
  return fn(P, cl, self, args);
}

/* Language-level method dispatchers use the same argument-vector entry. */
PN potion_c_method(Potion *P, PN closure, PN recv, PN args) {
  PN_SIZE n = PN_IS_TUPLE(args) ? PN_TUPLE_LEN(args) : (args != PN_NIL);
  PN argv[n + 1];
  argv[0] = recv;
  for (PN_SIZE i = 0; i < n; i++)
    argv[i + 1] = PN_IS_TUPLE(args) ? PN_TUPLE_AT(args, i) : args;
  return potion_c_call(P, closure, n + 1, argv);
}

/* A closure of a C-compiled proto. The generated code must not take the address of potion_c_proto
 * itself: in a windows program that address is an import thunk, not the one potion_call compares. */
PN potion_c_closure(Potion *P, PN proto, PN_SIZE upc) {
  PN cl = potion_closure_new(P, (PN_F)potion_c_proto, PN_PROTO(proto)->sig, upc + 1);
  PN_CLOSURE(cl)->data[0] = proto;
  PN_TOUCH(cl);
  return cl;
}

PN potion_c_run(Potion *P, PN proto, PN self, PN args, PN_SIZE upc, PN *upvals) {
  PN cl = potion_c_closure(P, proto, upc);
  for (PN_SIZE i = 0; i < upc; i++) {
    PN_CLOSURE(cl)->data[i + 1] = upvals[i];
    PN_TOUCH(cl);
  }
  return potion_c_method(P, cl, self, args);
}

/* Direct C callers use PN_F, as for potion_vm_proto. potion_call bypasses the
 * varargs bridge, so generated calls support arbitrary arity on every ABI. */
PN potion_c_proto(Potion *P, PN cl, PN self, ...) {
  int n = PN_CLOSURE(cl)->arity;
  PN args[n + 1];
  va_list ap;
  args[0] = self;
  va_start(ap, self);
  for (int i = 0; i < n; i++) args[i + 1] = va_arg(ap, PN);
  va_end(ap);
  return potion_c_call(P, cl, n + 1, args);
}

PN potion_c_invoke(Potion *P, PN *reg, int a, int b, PNCNamed *named, int count) {
  PN target = reg[a];
  if (PN_TYPE(target) == PN_TVTABLE) {
    reg[a + 1] = potion_object_new(P, PN_NIL, target);
    target = ((struct PNVtable *)target)->ctor;
  } else if (!PN_IS_CLOSURE(target)) {
    reg[a + 1] = target;
    target = potion_obj_get_call(P, target);
  }
  if (!PN_IS_CLOSURE(target)) return target;
  int given = b - a - 1, arity = PN_CLOSURE(target)->arity;
  int named_args = 0;
  for (int i = 0; i < count; i++)
    if (named[i].active && named[i].base == a) named_args++;
  int positional = given - named_args;
  if (PN_IS_TUPLE(PN_CLOSURE(target)->sig) && given && given < PN_CLOSURE(target)->minargs)
    return potion_error(P, potion_str(P, "Not enough arguments"), 0, 0, 0);
#ifndef P2
  if (PN_IS_TUPLE(PN_CLOSURE(target)->sig) && given > arity)
    return potion_error(P, potion_str(P, "Too many arguments"), 0, 0, 0);
#endif
  if (given >= arity && !named_args)
    return potion_call(P, target, given + 1, reg + a + 1);
  int n = given > arity ? given : arity;
  PN args[n + 1];
  args[0] = reg[a + 1];
  for (int i = 0; i < positional; i++) args[i + 1] = reg[a + i + 2];
  potion_c_defaults(PN_CLOSURE(target)->sig, positional, n, args + positional + 1);
  for (int i = 0; i < count; i++) {
    if (named[i].active && named[i].base == a) {
      args[named[i].index + 1] = named[i].value;
      named[i].active = 0;
    }
  }
  return potion_call(P, target, n + 1, args);
}

/* Emitter output is a GC-managed bytes object. Never keep its raw pointer (or
 * a pointer into a constant) across pn_printf, which can collect and move it. */
typedef struct { Potion *P; PN out; int serial; int failed; } CEmitter;
#define OUT(...) pn_printf(e->P, e->out, __VA_ARGS__)

static void c_string(CEmitter *e, PN str) {
  OUT("\"");
  for (PN_SIZE i = 0; i < PN_STR_LEN(str); i++) {
    unsigned char c = PN_STR_PTR(str)[i];
    if (c >= 32 && c < 127 && c != '"' && c != '\\' && c != '?') OUT("%c", c);
    else OUT("\\%03o", c);
  }
  OUT("\"");
}

/* Materialize one constant into a named, conservatively rooted C local. */
static int c_value(CEmitter *e, PN value) {
  Potion *P = e->P;
  int id = e->serial++;
  OUT("  PN v%d = ", id);
  if (!PN_IS_PTR(value)) OUT("(PN)(intptr_t)%" PRIdPTR ";\n", (intptr_t)value);
  else if (PN_IS_STR(value) || PN_TYPE(value) == PN_TBYTES) {
    OUT("potion_str2(P, "); c_string(e, value); OUT(", %u);\n", PN_STR_LEN(value));
  } else if (PN_IS_BIG(value)) {
    PN str = potion_big_string(P, value, 10);
    OUT("potion_big_from_str(P, "); c_string(e, str); OUT(", 10);\n");
  } else if (PN_IS_DBL(value)) {
    double d = PN_DBL(value);
    if (isnan(d)) OUT("potion_double(P, NAN);\n");
    else if (isinf(d)) OUT("potion_double(P, %sINFINITY);\n", d < 0 ? "-" : "");
    else OUT("potion_double(P, %a);\n", d);
  } else if (PN_IS_TUPLE(value)) {
    PN_SIZE n = PN_TUPLE_LEN(value);
    OUT("potion_tuple_with_size(P, %u);\n", n);
    for (PN_SIZE i = 0; i < n; i++) {
      int child = c_value(e, PN_TUPLE_AT(value, i));
      OUT("  PN_TUPLE_AT(v%d, %u) = v%d; PN_TOUCH(v%d);\n", id, i, child, id);
    }
  } else {
    /* Values emitted by the compiler must be reconstructible, not silently nil. */
    OUT("PN_NIL;\n");
    e->failed = 1;
  }
  return id;
}

static void c_field(CEmitter *e, PN value, const char *field) {
  int id = c_value(e, value);
  OUT("  PN_PROTO(f)->%s = v%d; PN_TOUCH(f);\n", field, id);
}

static int c_function(CEmitter *e, PN proto) {
  Potion *P = e->P;
  int id = e->serial++, nregs = PN_INT(PN_PROTO(proto)->stack);
  PN_SIZE nops = PN_OP_LEN(PN_PROTO(proto)->asmb);
  int named_count = 0, named_index = 0;
  for (PN_SIZE i = 0; i < nops; i++)
    if (PN_OP_AT(PN_PROTO(proto)->asmb, i).code == OP_NAMED) named_count++;
  PN children = PN_TUP0();
  PN_TUPLE_EACH(PN_PROTO(proto)->protos, i, child, {
    int sub = c_function(e, child);
    children = PN_PUSH(children, PN_NUM(sub));
  });
  OUT("\nstatic PN code_%d(Potion *P, PN cl, PN self, PN *args) {\n", id);
  OUT("  PN r[%d] = {0};\n", nregs + 2);
  if (PN_PROTO(proto)->localsize)
    OUT("  PN l[%u] = {0};\n", PN_PROTO(proto)->localsize);
  if (named_count) OUT("  PNCNamed named[%d] = {0};\n", named_count);
  OUT("  r[0] = self;\n#define F PN_PROTO(PN_CLOSURE(cl)->data[0])\n#define U(i) PN_CLOSURE(cl)->data[(i)+1]\n");
  PN sig = PN_PROTO(proto)->sig;
  int arg = 0;
  if (PN_IS_TUPLE(sig)) PN_TUPLE_EACH(sig, i, name, {
    if (PN_IS_STR(name) && !(i > 0 && PN_TUPLE_AT(sig, i - 1) == PN_NUM(':'))) {
      PN_SIZE loc = PN_GET(PN_PROTO(proto)->locals, name);
      if (loc != PN_NONE) OUT("  l[%u] = args[%d];\n", loc, arg);
      arg++;
    }
  });
  for (PN_SIZE pc = 0; pc < nops; pc++) {
    PN_OP op = PN_OP_AT(PN_PROTO(proto)->asmb, pc);
    int a = op.a, b = op.b;
    OUT("L%u: ;\n", pc);
    switch (op.code) {
      case OP_NONE: case OP_DEBUG: break;
      case OP_MOVE: OUT("  r[%d] = r[%d];\n", a, b); break;
      case OP_LOADK: OUT("  r[%d] = PN_TUPLE_AT(F->values, %d);\n", a, b); break;
      case OP_LOADPN: OUT("  r[%d] = (PN)(intptr_t)%d;\n", a, b); break;
      case OP_SELF: OUT("  r[%d] = self;\n", a); break;
      case OP_GETLOCAL: OUT("  r[%d] = PN_IS_REF(l[%d]) ? PN_DEREF(l[%d]) : l[%d];\n", a,b,b,b); break;
      case OP_SETLOCAL: OUT("  if (PN_IS_REF(l[%d])) { PN_DEREF(l[%d]) = r[%d]; PN_TOUCH(l[%d]); } else l[%d] = r[%d];\n",b,b,a,b,b,a); break;
      case OP_NEWLOCAL: OUT("  l[%d] = r[%d];\n", b,a); break;
      case OP_GETUPVAL: OUT("  r[%d] = PN_DEREF(U(%d));\n",a,b); break;
      case OP_SETUPVAL: OUT("  PN_DEREF(U(%d)) = r[%d]; PN_TOUCH(U(%d));\n",b,a,b); break;
      case OP_GLOBAL: OUT("  potion_define_global(P, r[%d], r[%d]); r[%d] = r[%d];\n",a,b,a,b); break;
      case OP_NEWTUPLE: OUT("  r[%d] = PN_TUP0();\n",a); break;
      case OP_GETTUPLE:
        if (b & ASM_TPL_IMM) OUT("  r[%d] = PN_TUPLE_AT(r[%d], PN_INT(r[%d]));\n",a,a,b-ASM_TPL_IMM);
        else OUT("  r[%d] = PN_TUPLE_AT(r[%d], %d);\n",a,a,b);
        break;
      case OP_SETTUPLE: OUT("  r[%d] = potion_tuple_push_skipnil(P, r[%d], r[%d]);\n",a,a,b); break;
      case OP_GETTABLE: OUT("  r[%d] = potion_table_at(P, PN_NIL, r[%d], r[%d]);\n",a,a,b); break;
      case OP_SETTABLE: OUT("  potion_table_set(P, r[%d], r[%d], r[%d]);\n",a,a+1,b); break;
      case OP_NEWLICK:
        OUT("  r[%d] = potion_lick(P, r[%d], ", a, a);
        if (b > a) OUT("r[%d]", a + 1); else OUT("PN_NIL");
        OUT(", ");
        if (b > a + 1) OUT("r[%d]", b); else OUT("PN_NIL");
        OUT(");\n"); break;
      case OP_GETPATH: OUT("  r[%d] = potion_obj_get(P, PN_NIL, r[%d], r[%d]);\n",a,a,b); break;
      case OP_SETPATH: OUT("  potion_obj_set(P, PN_NIL, r[%d], r[%d], r[%d]);\n",a,a+1,b); break;
      case OP_ADD: case OP_SUB: case OP_MULT:
        OUT("  r[%d] = pn_c_%s(P, r[%d], r[%d]);\n",a,op.code==OP_ADD?"add":op.code==OP_SUB?"sub":"mult",a,b); break;
      case OP_DIV: case OP_REM: case OP_POW: case OP_BITL: case OP_BITR:
        OUT("  r[%d] = potion_obj_%s(P, r[%d], r[%d]);\n",a,op.code==OP_DIV?"div":op.code==OP_REM?"rem":op.code==OP_POW?"pow":op.code==OP_BITL?"bitl":"bitr",a,b); break;
      case OP_BITN: OUT("  r[%d] = potion_obj_bitn(P, r[%d]);\n",a,b); break;
      case OP_NOT:
#ifdef P2
        OUT("  r[%d] = PN_BOOL(!PN_TEST1(r[%d]));\n",a,a);
#else
        OUT("  r[%d] = PN_BOOL(!PN_TEST(r[%d]));\n",a,a);
#endif
        break;
      case OP_CMP: case OP_EQ: case OP_NEQ: {
        const char *names[] = {"cmp", "eq", "neq"};
        OUT("  r[%d] = potion_vm_%s(P, r[%d], r[%d]);\n", a, names[op.code-OP_CMP], a, b); break;
      }
      case OP_LT: case OP_LTE: case OP_GT: case OP_GTE: {
        const char *names[] = {"lt", "lte", "gt", "gte"};
        const char *ops[] = {"<", "<=", ">", ">="};
        OUT("  r[%d] = PN_IS_INT(r[%d]) && PN_IS_INT(r[%d]) ? PN_BOOL(PN_INT(r[%d]) %s PN_INT(r[%d])) : potion_vm_%s(P, r[%d], r[%d]);\n", a,a,b,a,ops[op.code-OP_LT],b,names[op.code-OP_LT],a,b); break;
      }
      case OP_DEF: OUT("  r[%d] = potion_def_method(P, PN_NIL, r[%d], r[%d], r[%d]);\n",a,a,a+1,b); break;
      case OP_BIND: case OP_MSG: OUT("  r[%d] = potion_%s(P, r[%d], r[%d]);\n",a,op.code==OP_BIND?"bind":"message",b,a); break;
      case OP_TEST: OUT("  r[%d] = PN_BOOL(PN_TEST(r[%d]));\n",a,a); break;
      case OP_JMP: case OP_TESTJMP: case OP_NOTJMP: {
        long dest = (long)pc + 1 + (op.code == OP_JMP ? a : b);
        if (dest < 0 || dest > nops) { e->failed = 1; break; }
        if (op.code == OP_JMP) OUT("  goto L%ld;\n",dest);
        else OUT("  if (%sPN_TEST1(r[%d])) goto L%ld;\n",op.code==OP_NOTJMP?"!":"",a,dest);
        break;
      }
      case OP_NAMED:
        OUT("  { int x = potion_sig_find(P, r[%d], r[%d]); if (x < 0) potion_fatal(\"named parameter not found in signature\"); named[%d] = (PNCNamed){1, %d, x, r[%d]}; }\n", a, b-1, named_index++, a, b); break;
      case OP_CALL:
        OUT("  r[%d] = potion_c_invoke(P, r, %d, %d, %s, %d); if (PN_TYPE(r[%d]) == PN_TERROR) return r[%d];\n", a,a,b,named_count?"named":"NULL",named_count,a,a); break;
      case OP_CALLSET: OUT("  r[%d] = potion_obj_get_callset(P, r[%d]);\n",a,b); break;
      case OP_RETURN: OUT("  return r[%d];\n",a); break;
      case OP_PROTO: {
        PN child = PN_TUPLE_AT(PN_PROTO(proto)->protos,b);
        PN_SIZE upc = PN_TUPLE_LEN(PN_PROTO(child)->upvals);
        OUT("  { PN sub = PN_TUPLE_AT(F->protos, %d); PN closure = potion_c_closure(P, sub, %u);\n",b,(unsigned)upc);
        for (PN_SIZE i = 0; i < upc; i++) {
          if (++pc >= nops) { e->failed = 1; break; }
          PN_OP capture = PN_OP_AT(PN_PROTO(proto)->asmb,pc);
          if (capture.code == OP_GETLOCAL) {
            OUT("    l[%d] = potion_ref(P, l[%d]); PN_CLOSURE(closure)->data[%u] = l[%d]; PN_TOUCH(closure);\n",capture.b,capture.b,i+1,capture.b);
          } else if (capture.code == OP_GETUPVAL) {
            OUT("    PN_CLOSURE(closure)->data[%u] = U(%d); PN_TOUCH(closure);\n",i+1,capture.b);
          } else e->failed = 1;
        }
        OUT("    r[%d] = closure; }\n",a); break;
      }
      case OP_CLASS: OUT("  r[%d] = potion_vm_class(P, r[%d], r[%d]);\n",a,b,a); break;
      case OP_TAILCALL: default: e->failed = 1; break;
    }
  }
  OUT("L%u: ;\n  return r[0];\n#undef F\n#undef U\n}\n",nops);
  OUT("static PN build_%d(Potion *P, PN parent) {\n  PN f = (PN)potion_gc_alloc(P, PN_TPROTO, sizeof(struct PNProto));\n  memset((char *)f + sizeof(struct PNObject), 0, sizeof(struct PNProto) - sizeof(struct PNObject));\n",id);
  c_field(e, PN_PROTO(proto)->sig,"sig");
  c_field(e, PN_PROTO(proto)->stack,"stack");
  c_field(e, PN_PROTO(proto)->paths,"paths");
  c_field(e, PN_PROTO(proto)->locals,"locals");
  c_field(e, PN_PROTO(proto)->upvals,"upvals");
  c_field(e, PN_PROTO(proto)->values,"values");
  c_field(e, PN_PROTO(proto)->name,"name");
  OUT("  { PN empty = (PN)potion_asm_new(P); PN_PROTO(f)->asmb = empty; PN_TOUCH(f); }\n");
  if (PN_IS_STR(PN_PROTO(proto)->source)) c_field(e, PN_PROTO(proto)->source,"source");
  else OUT("  PN_PROTO(f)->source = parent; PN_TOUCH(f);\n");
  OUT("  PN subs = potion_tuple_with_size(P, %u);\n  PN_PROTO(f)->protos = subs; PN_TOUCH(f);\n",PN_TUPLE_LEN(children));
  PN_TUPLE_EACH(children,i,sub,{
    OUT("  { PN child = build_%d(P, f); PN_TUPLE_AT(subs, %u) = child; PN_TOUCH(subs); }\n",(int)PN_INT(sub),i);
  });
  OUT("  PN_PROTO(f)->localsize = %u; PN_PROTO(f)->upvalsize = %u; PN_PROTO(f)->pathsize = %u;\n  PN_PROTO(f)->arity = %d; PN_PROTO(f)->cfunc = code_%d;\n  return f;\n}\n",PN_PROTO(proto)->localsize,PN_PROTO(proto)->upvalsize,PN_PROTO(proto)->pathsize,arg,id);
  return id;
}

PN potion_source_emitc(Potion *P, PN cl, PN self, PN options) {
  PN proto = self;
  if (PN_TYPE(proto) == PN_TSOURCE) proto = potion_source_compile(P, cl, proto, PN_NIL, PN_NIL);
  if (!PN_IS_PROTO(proto)) return potion_type_error_want(P, "self", self, "Source or Proto");
  if (PN_PROTO(proto)->cfunc)
    return potion_error(P, potion_str(P, "compile/c needs a bytecode prototype or source tree"), 0, 0, 0);
  CEmitter e0 = { P, potion_bytes(P, 0), 0, 0 }, *e = &e0;
#ifdef P2
  OUT("#ifndef P2\n#define P2 1\n#endif\n");
#endif
  OUT("/* Generated by compile/c. Compile with the matching runtime and -Icore. */\n#include \"compile-c.h\"\n");
  int root = c_function(e, proto);
  if (e->failed) return potion_error(P, potion_str(P,"compile/c: unsupported constant or invalid instruction"),0,0,0);
  /* A module's name is a C token, never interpolated unchecked into source. */
  PN source = PN_PROTO(proto)->source;
  const char *base = PN_IS_STR(source) ? PN_STR_PTR(source) : "compiled";
  const char *slash = strrchr(base,'/');
  if (slash) base = slash + 1;
  const char *dot = strrchr(base, '.');
  size_t len = dot ? (size_t)(dot - base) : strlen(base);
  if (PN_IS_STR(options)) {
    const char *opt = PN_STR_PTR(options);
    while (*opt) {
      const char *end = strchr(opt, ',');
      size_t n = end ? (size_t)(end - opt) : strlen(opt);
      if (n >= 7 && !strncmp(opt, "module=", 7)) {
        base = opt + 7;
        len = n - 7;
      }
      if (!end) break;
      opt = end + 1;
    }
  }
  char *module = malloc(len + 2);
  if (!module) potion_allocation_error();
  for (size_t i = 0; i < len; i++) {
    unsigned char c = base[i];
    module[i] = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                 || (c >= '0' && c <= '9') || c == '_') ? c : '_';
  }
  module[len] = 0;
  OUT("\n#ifndef POTION_MODULE_NAME\n#define POTION_MODULE_NAME %s%s\n#endif\n#define PN_C_CAT_(a,b) a##b\n#define PN_C_CAT(a,b) PN_C_CAT_(a,b)\n",!len || isdigit((unsigned char)module[0])?"_":"",module);
  free(module);
#ifdef P2
  int p6 = (P->flags & MAX_SYNTAX) >= MODE_P6 && (P->flags & MAX_SYNTAX) < MODE_P6 + (1<<EXEC_BITS);
  OUT("static void init_runtime(Potion *P) {\n  PN p5 = potion_bind(P, P->lobby, PN_STR(\"p5args\"));\n  if (!PN_IS_CLOSURE(p5)) PN_C_INIT_RT(P, \"libp5\", Potion_Init_libp5);\n");
  if (p6)
    OUT("  PN p6 = potion_bind(P, P->lobby, PN_STR(\"p6_pi\"));\n  if (!PN_IS_CLOSURE(p6)) PN_C_INIT_RT(P, \"libp6\", Potion_Init_libp6);\n");
  OUT("}\n");
#endif
  OUT("static PN run_program(Potion *P) {\n");
#ifdef P2
  OUT("  init_runtime(P);\n");
#endif
  OUT("  PN f = build_%d(P, PN_NIL);\n  return potion_c_run(P, f, P->lobby, PN_NIL, 0, NULL);\n}\n", root);
  OUT("DLLEXPORT PN potion_compiled_run(Potion *P) { return run_program(P); }\nDLLEXPORT void PN_C_CAT(Potion_Init_, POTION_MODULE_NAME)(Potion *P) {\n  PN result = run_program(P);\n  if (PN_TYPE(result) == PN_TERROR) potion_p(P, result);\n}\n#ifndef POTION_MODULE\nint main(int argc, char **argv) {\n  POTION_INIT_STACK(sp);\n");
#ifdef P2
  OUT("  potion_set_argv0(argv[0]);\n");
#endif
  OUT("  Potion *P = potion_create(sp);\n");
#ifdef P2
  OUT("  init_runtime(P);\n");
  if (p6) OUT("  P->flags = (Potion_Flags)MODE_P6;\n");
#endif
  OUT("  PN av = PN_TUP0();\n  for (int i = %d; i < argc; i++) { PN s = potion_str(P, argv[i]); av = PN_PUSH(av, s); }\n", 
#ifdef P2
      1
#else
      0
#endif
  );
  OUT("  potion_define_global(P, potion_str(P, \"%s\"), av);\n",
#ifdef P2
      "@ARGV"
#else
      "argv"
#endif
  );
  OUT("  PN result = run_program(P);\n  int status = PN_TYPE(result) == PN_TERROR;\n  if (status) potion_p(P, result);\n  potion_destroy(P);\n  return status;\n}\n#endif\n");
  return potion_fwd(e->out);
}

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
/* ---- --compile=exe: the serialized bytecode as a byte array, loaded and run by a small main.
 * Not meant to be read; the array loads without parsing. */
PN potion_source_emitexe(Potion *P, PN cl, PN self, PN options) {
  PN proto = self;
  if (PN_TYPE(proto) == PN_TSOURCE) proto = potion_source_compile(P, cl, proto, PN_NIL, PN_NIL);
  if (!PN_IS_PROTO(proto)) return potion_type_error_want(P, "self", self, "Source or Proto");
  PN bc = potion_source_dumpbc(P, cl, proto, PN_NIL);
  if (PN_TYPE(bc) == PN_TERROR) return bc;
  size_t len = PN_STR_LEN(bc);
  unsigned char *raw = malloc(len ? len : 1);
  char *text = malloc(len * 4 + len / 32 + 2);
  if (!raw || !text) potion_allocation_error();
  memcpy(raw, PN_STR_PTR(bc), len);
  char *t = text;
  for (size_t i = 0; i < len; i++) {
    t += sprintf(t, "%u,", raw[i]);
    if (i % 32 == 31) *t++ = '\n';
  }
  *t = '\0';
  free(raw);
  CEmitter e0 = { P, potion_bytes(P, 0), 0, 0 }, *e = &e0;
  const char *rt = "potion";
#ifdef P2
  int p6 = (P->flags & MAX_SYNTAX) >= MODE_P6 && (P->flags & MAX_SYNTAX) < MODE_P6 + (1<<EXEC_BITS);
  rt = p6 ? "p6" : "p5";
#endif
  OUT("/*runtime:%s*/\n", rt);
#ifdef P2
  OUT("#ifndef P2\n#define P2 1\n#endif\n");
#endif
  OUT("#include <stdio.h>\n#include <string.h>\n#include \"compile-c.h\"\n");
  OUT("static const unsigned char bc[] = {\n%s\n};\n", text);
  free(text);
#ifdef P2
  OUT("static void init_runtime(Potion *P) {\n  PN_C_INIT_RT(P, \"libp5\", Potion_Init_libp5);\n");
  if (p6) OUT("  PN_C_INIT_RT(P, \"libp6\", Potion_Init_libp6);\n");
  OUT("}\n");
#endif
  OUT("int main(int argc, char **argv) {\n  POTION_INIT_STACK(sp);\n");
#ifdef P2
  OUT("  potion_set_argv0(argv[0]);\n");
#endif
  OUT("  Potion *P = potion_create(sp);\n");
#ifdef P2
  OUT("  init_runtime(P);\n");
  if (p6) OUT("  P->flags = (Potion_Flags)MODE_P6;\n");
#endif
  OUT("  PN av = PN_TUP0();\n  for (int i = %d; i < argc; i++) { PN s = potion_str(P, argv[i]); av = PN_PUSH(av, s); }\n  potion_define_global(P, potion_str(P, \"%s\"), av);\n",
#ifdef P2
      1, "@ARGV"
#else
      0, "argv"
#endif
  );
  OUT("  PN buf = potion_bytes(P, sizeof(bc));\n  memcpy(PN_STR_PTR(buf), bc, sizeof(bc));\n  PN_STR_PTR(buf)[sizeof(bc)] = 0;\n");
  OUT("  PN code = potion_source_load(P, PN_NIL, buf);\n  if (!PN_IS_PROTO(code)) { fprintf(stderr, \"** invalid embedded bytecode\\n\"); return 1; }\n");
  OUT("  PN result = potion_run(P, code, POTION_JIT);\n  int status = PN_TYPE(result) == PN_TERROR;\n  if (status) potion_p(P, result);\n  potion_destroy(P);\n  return status;\n}\n");
  return potion_fwd(e->out);
}

/* quote one word for the shell that system() runs: sh (single quotes) or cmd.exe (double quotes) */
static void sh_quote(char **dst, const char *s) {
#ifdef _WIN32
  *(*dst)++ = '"';
  for (; *s; s++) *(*dst)++ = *s == '/' ? '\\' : *s; /* cmd.exe: a path has no quote; use backslashes */
  *(*dst)++ = '"';
#else
  *(*dst)++ = '\'';
  for (; *s; s++) {
    if (*s == '\'') { memcpy(*dst, "'\\''", 4); *dst += 4; }
    else *(*dst)++ = *s;
  }
  *(*dst)++ = '\'';
#endif
}

/** Compile the C source emitted by potion_source_emitexe to OUTPATH with POTION_CC, using a
 * temporary .c file that is always removed. Returns 0 on success, else prints the reason. */
int potion_exe_build(Potion *P, PN csrc, const char *outpath, const char *opts) {
  const char *src = PN_STR_PTR(csrc);
  int p2 = 0, p6 = 0, fd;
  if (!strncmp(src, "/*runtime:p5*/", 14)) p2 = 1;
  else if (!strncmp(src, "/*runtime:p6*/", 14)) p2 = p6 = 1;
  size_t n = strlen(outpath);
  char *tmp = malloc(n + 32);
  if (!tmp) potion_allocation_error();
  fd = -1;
  for (int attempt = 0; attempt < 100 && fd < 0; attempt++) { /* mkstemps is not in mingw */
    sprintf(tmp, "%s.%x%x.c", outpath, (unsigned)getpid(), (unsigned)(rand() ^ (attempt << 16)));
    fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_BINARY, 0600);
  }
  if (fd < 0) { fprintf(stderr, "** could not create a temporary file next to %s\n", outpath); free(tmp); return 1; }
  size_t len = PN_STR_LEN(csrc);
  FILE *f = fdopen(fd, "wb");
  if (!f || fwrite(PN_STR_PTR(csrc), 1, len, f) != len || fclose(f) != 0) {
    fprintf(stderr, "** could not write %s\n", tmp);
    unlink(tmp); free(tmp); return 1;
  }
  /* the build tree (core/potion.h next to lib/) or the installed prefix */
  int tree = access("core/potion.h", R_OK) == 0;
  const char *inc = tree ? "core" : POTION_PREFIX "/include/potion";
  char libdir[4096], p2dir[4100];
  if (tree) {
    char *cwd = getcwd(NULL, 0);
    snprintf(libdir, sizeof libdir, "%s/lib", cwd ? cwd : ".");
    free(cwd);
  } else snprintf(libdir, sizeof libdir, "%s", POTION_PREFIX "/lib");
  snprintf(p2dir, sizeof p2dir, "%s/p2", libdir);
  char cmd[16384], *c = cmd;
#ifdef _WIN32
  *c++ = '"'; /* cmd /c strips the outer pair of quotes of a command that starts with a quote */
#endif
  c += sprintf(c, "%s %s -I", POTION_CC, POTION_CFLAGS);
  sh_quote(&c, inc);
  *c++ = ' '; sh_quote(&c, tmp);
  c += sprintf(c, " -o "); sh_quote(&c, outpath);
#ifdef _WIN32 /* a -lname would find the static libpotion.a before the dll: link the dlls by path */
  {
    char dll[4200];
    if (p2) {
      snprintf(dll, sizeof dll, "%s/libp5.dll", p2dir); *c++ = ' '; sh_quote(&c, dll);
      if (p6) { snprintf(dll, sizeof dll, "%s/libp6.dll", p2dir); *c++ = ' '; sh_quote(&c, dll); }
    }
    snprintf(dll, sizeof dll, "%s/%s", libdir, p2 ? "libp2.dll" : "libpotion.dll");
    *c++ = ' '; sh_quote(&c, dll);
    c += sprintf(c, " -lm");
  }
#else
  if (p2) {
    c += sprintf(c, " -L"); sh_quote(&c, p2dir);
#ifdef __APPLE__ /* libp5 and libp6 are loadable bundles: the program loads them */
    (void)p6;
#else
    c += sprintf(c, p6 ? " -lp6 -lp5" : " -lp5");
#endif
    c += sprintf(c, " -Wl,-rpath,"); sh_quote(&c, p2dir);
  }
  c += sprintf(c, " -L"); sh_quote(&c, libdir);
  c += sprintf(c, " -Wl,-rpath,"); sh_quote(&c, libdir);
  c += sprintf(c, p2 ? " -lp2 -lm" : " -lpotion -lm");
#endif
  /* --compile=exe,ARG,ARG...: extra compiler arguments after ours, so a later -o wins. A two letter flag with
   * only -o=path becomes the two words "-o path"; every other argument (-DNAME=1, -L=x, -lm) keeps its text. */
  while (opts && *opts) {
    const char *end = strchr(opts, ',');
    size_t len = end ? (size_t)(end - opts) : strlen(opts);
    char word[1024];
    if (len && len < sizeof word && (size_t)(c - cmd) + 2 * len + 8 < sizeof cmd) {
      memcpy(word, opts, len); word[len] = '\0';
      *c++ = ' ';
      if (len > 3 && !strncmp(word, "-o=", 3)) {
        word[2] = '\0';
        sh_quote(&c, word); *c++ = ' '; sh_quote(&c, word + 3);
      } else sh_quote(&c, word);
    }
    opts = end ? end + 1 : NULL;
  }
#ifdef _WIN32
  *c++ = '"';
#endif
  *c = '\0';
  int rc = system(cmd);
  unlink(tmp);
  free(tmp);
  if (rc != 0) { fprintf(stderr, "** C compiler failed: %s\n", cmd); return 1; }
  return 0;
}
