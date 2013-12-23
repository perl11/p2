/* Copyright (c) 2007 by Ian Piumarta
 * Copyright (c) 2011 by Amos Wenger nddrylliog@gmail.com
 * Copyright (c) 2013 by perl11 org
 * All rights reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the 'Software'),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, provided that the above copyright notice(s) and this
 * permission notice appear in all copies of the Software.  Acknowledgement
 * of the use of this Software in supporting documentation would be
 * appreciated but is not required.
 * 
 * THE SOFTWARE IS PROVIDED 'AS IS'.  USE ENTIRELY AT YOUR OWN RISK.
 * 
 * Last edited: 2013-04-11 11:10:34 rurban
 */

#include <stdio.h>

#define GREG_MAJOR	0
#define GREG_MINOR	4
#define GREG_LEVEL	6

// start potion custom
#ifdef GREG_GC
#include "p2.h"
#include "internal.h"

#define YY_XTYPE Potion *
#ifdef DEBUG
# ifndef YY_DEBUG
#  define YY_DEBUG
# endif
#endif

#if 1
# include <stddef.h>
# define STRUCT_OFFSET(s,m)  offsetof(s,m)
#else
# define STRUCT_OFFSET(s,m)  (size_t)(&(((s *)0)->m))
#endif

#undef YY_XTYPE
#define YY_XTYPE Potion *
// offset from ptr->data back to ptr
#define _BYTES_OFF(ptr) ((struct PNObject * volatile)((char *)(ptr) - STRUCT_OFFSET(struct PNWeakRef, data)))
#define YY_ALLOC(C, D)  (char*)PN_DATA((PN)potion_gc_alloc((Potion*)D, PN_TWEAK, C))
#define YY_CALLOC(N, S, D) YY_ALLOC(N*S, D)
#define YY_REALLOC(V, C, D) (char*)PN_DATA((PN)potion_gc_realloc((Potion*)D, PN_TWEAK, _BYTES_OFF(V), sizeof(struct PNWeakRef)+C))
#define YY_STRDUP(G, S) ({ \
      char *x = YY_ALLOC(strlen(S), (Potion*)G->data);  \
      PN_MEMCPY_N(x, S, char, strlen(S)); x; })
#define YY_FREE
#define YY_INITDATA POTION_INIT_STACK(sp); data = potion_create(sp)

#endif /* GREG_GC */
// end potion custom

#ifndef YY_ALLOC
#define YY_ALLOC(N, D) malloc(N)
#endif
#ifndef YY_CALLOC
#define YY_CALLOC(N, S, D) calloc(N, S)
#endif
#ifndef YY_REALLOC
#define YY_REALLOC(B, N, D) realloc(B, N)
#endif
#ifndef YY_STRDUP
#define YY_STRDUP(G, S) strdup(S)
#endif
#ifndef YY_FREE
#define YY_FREE free
#endif
#ifndef YYSTYPE
#define YYSTYPE	int
#endif
#ifndef YY_XTYPE
#define YY_XTYPE void *
#endif
#ifndef YY_XVAR
#define YY_XVAR yyxvar
#endif
#ifndef YY_PART
#define yydata G->data
#define yy G->ss
#endif

#ifndef YY_STACK_SIZE
#define YY_STACK_SIZE 1024
#endif
#ifndef YY_BUFFER_START_SIZE
#define YY_BUFFER_START_SIZE 16384
#endif

// end of definable section

struct _yythunk; // forward declaration
typedef struct _GREG {
  char *buf;
  int   buflen;
  int   offset;
  int   pos;
  int   limit;
  char *text;
  int	textlen;
  int	begin;
  int	end;
  struct _yythunk *thunks;
  int	thunkslen;
  int   thunkpos;
  int	lineno;
  char	*filename;
  FILE  *input;
  YYSTYPE ss;
  YYSTYPE *val;
  YYSTYPE *vals;
  int valslen;
  YY_XTYPE data;
#ifdef YY_DEBUG
  int debug;
#endif
} GREG;

#ifdef _GREG_ONLY

typedef enum { Freed = -1, Unknown= 0, Rule, Variable, Name, Dot, Character, String, Class, Action, Predicate, Alternate, Sequence, PeekFor, PeekNot, Query, Star, Plus, Any } NodeType;

enum {
  RuleUsed	= 1<<0,
  RuleReached	= 1<<1,
};

typedef union Node Node;

#define NODE_COMMON NodeType type;  Node *next; char *errblock
struct Rule	 { NODE_COMMON; char *name; Node *variables;  Node *expression;  int id;  int flags;	};
struct Variable	 { NODE_COMMON; char *name; Node *value;  int offset;					};
struct Name	 { NODE_COMMON; Node *rule; Node *variable;						};
struct Dot	 { NODE_COMMON;										};
struct Character { NODE_COMMON; char *value;								};
struct String	 { NODE_COMMON; char *value;								};
struct Class	 { NODE_COMMON; unsigned char *value;							};
struct Action	 { NODE_COMMON; char *text;  Node *list;  char *name;  Node *rule;			};
struct Predicate { NODE_COMMON; char *text;								};
struct Alternate { NODE_COMMON; Node *first;  Node *last;						};
struct Sequence	 { NODE_COMMON; Node *first;  Node *last;						};
struct PeekFor	 { NODE_COMMON; Node *element;								};
struct PeekNot	 { NODE_COMMON; Node *element;								};
struct Query	 { NODE_COMMON; Node *element;								};
struct Star	 { NODE_COMMON; Node *element;								};
struct Plus	 { NODE_COMMON; Node *element;								};
struct Any	 { NODE_COMMON;										};
#undef NODE_COMMON

union Node
{
  NodeType		type;
  struct Rule		rule;
  struct Variable	variable;
  struct Name		name;
  struct Dot		dot;
  struct Character	character;
  struct String		string;
  struct Class		cclass;
  struct Action		action;
  struct Predicate	predicate;
  struct Alternate	alternate;
  struct Sequence	sequence;
  struct PeekFor	peekFor;
  struct PeekNot	peekNot;
  struct Query		query;
  struct Star		star;
  struct Plus		plus;
  struct Any		any;
};

extern Node *actions;
extern Node *rules;
extern Node *start;

extern int   ruleCount;

extern FILE *output;

extern Node *makeRule(GREG *G, char *name, int starts);
extern Node *findRule(GREG *G, char *name, int starts);
extern Node *beginRule(Node *rule);
extern void  Rule_setExpression(GREG *G, Node *rule, Node *expression);
extern Node *Rule_beToken(GREG *G, Node *rule);
extern Node *makeVariable(GREG *G, char *name);
extern Node *makeName(GREG *G, Node *rule);
extern Node *makeDot(GREG *G);
extern Node *makeCharacter(GREG *G, char *text);
extern Node *makeString(GREG *G, char *text);
extern Node *makeClass(GREG *G, char *text);
extern Node *makeAction(GREG *G, char *text);
extern Node *makePredicate(GREG *G, char *text);
extern Node *makeAlternate(GREG *G, Node *e);
extern Node *Alternate_append(GREG *G, Node *e, Node *f);
extern Node *makeSequence(GREG *G, Node *e);
extern Node *Sequence_append(GREG *G, Node *e, Node *f);
extern Node *makePeekFor(GREG *G, Node *e);
extern Node *makePeekNot(GREG *G, Node *e);
extern Node *makeQuery(GREG *G, Node *e);
extern Node *makeStar(GREG *G, Node *e);
extern Node *makePlus(GREG *G, Node *e);
extern Node *push(Node *node);
extern Node *top(void);
extern Node *pop(void);

extern void  Rule_compile_c_header(void);
extern void  Rule_compile_c(Node *node);

extern void  Node_print(Node *node);
extern void  Rule_print(Node *node);
extern void  Rule_free(Node *node);
extern void  freeRules(void);

#endif /* _GREG_ONLY */
