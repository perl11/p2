# -*- mode: antlr; tab-width:8 -*-
# LE Grammar for LE Grammars
# 
# Copyright (c) 2007 by Ian Piumarta
# Copyright (c) 2011 by Amos Wenger nddrylliog@gmail.com
# Copyright (c) 2013 by perl11 org
# All rights reserved.
# 
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the 'Software'),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, provided that the above copyright notice(s) and this
# permission notice appear in all copies of the Software.  Acknowledgement
# of the use of this Software in supporting documentation would be
# appreciated but is not required.
# 
# THE SOFTWARE IS PROVIDED 'AS IS'.  USE ENTIRELY AT YOUR OWN RISK.
# 
# Last edited: 2013-04-11 15:58:07 rurban

%{
#define _GREG_ONLY
#include <unistd.h>
#include <libgen.h>
#include <assert.h>

typedef struct Header Header;

struct Header {
    char   *text;
    Header *next;
};

int verboseFlag= 0;

static char	*trailer= 0;
static Header	*headers= 0;

#define YY_LOCAL(T)	static T
#define YY_RULE(T)	static T
#define YY_INPUT(buf, result, max_size)		\
  {							\
    int yyc= fgetc(G->input);				\
    if ('\n' == yyc) ++G->lineno;                       \
    result= (EOF == yyc) ? 0 : (*(buf)= yyc, 1);	\
  }

struct _GREG;
void makeHeader(struct _GREG *G, char *text);
void makeTrailer(struct _GREG *G, char *text);

%}

# Hierarchical syntax

grammar=	- ( declaration | definition )+ trailer? end-of-file

declaration=	'%{' < ( !'%}' . )* > RPERCENT		{ makeHeader(G,yytext); }						#{YYACCEPT}

trailer=	'%%' < .* >				{ makeTrailer(G,yytext); }					#{YYACCEPT}

definition=	s:identifier 				{ if (push(beginRule(findRule(G,yytext,1)))->rule.expression)
							    fprintf(stderr, "rule '%s' redefined\n", yytext); }
		EQUAL expression			{ Node *e= pop();  Rule_setExpression(G,pop(), e); }
		SEMICOLON?												#{YYACCEPT}

expression=	sequence (BAR sequence			{ Node *f= pop();  push(Alternate_append(G,pop(), f)); }
			    )*

sequence=	prefix (prefix				{ Node *f= pop();  push(Sequence_append(G,pop(), f)); }
			  )*

prefix=		AND action				{ push(makePredicate(G,yytext)); }
|		AND suffix				{ push(makePeekFor(G,pop())); }
|		NOT suffix				{ push(makePeekNot(G,pop())); }
|		    suffix

suffix=		primary (QUESTION			{ push(makeQuery(G,pop())); }
                        | STAR			        { push(makeStar(G,pop())); }
			| PLUS			        { push(makePlus(G,pop())); }
			)?

primary=	(
                identifier				{ push(makeVariable(G,yytext)); }
		COLON identifier !EQUAL			{ Node *name= makeName(G,findRule(G,yytext,0));  name->name.variable= pop();  push(name); }
|		identifier !EQUAL			{ push(makeName(G,findRule(G,yytext,0))); }
|		OPEN expression CLOSE
|		literal					{ push(makeString(G,yytext)); }
|		class					{ push(makeClass(G,yytext)); }
|		DOT					{ push(makeDot(G)); }
|		action					{ push(makeAction(G,yytext)); }
|		BEGIN					{ push(makePredicate(G,"YY_BEGIN")); }
|		END					{ push(makePredicate(G,"YY_END")); }
                ) (errblock { Node *node = pop(); ((struct Any *) node)->errblock = YY_STRDUP(G,yytext); push(node); })?

# Lexical syntax

identifier=	< [-a-zA-Z_][-a-zA-Z_0-9]* > -

literal=	['] < ( !['] char )* > ['] -
|		["] < ( !["] char )* > ["] -

class=		'[' < ( !']' range )* > ']' -

range=		char '-' char | char

char=		'\\' [abefnrtv'"\[\]\\]
|		'\\' [0-3][0-7][0-7]
|		'\\' [0-7][0-7]?
|		!'\\' .


errblock=       '~{' < braces* > '}' -
action=		'{' < braces* > '}' -

braces=		'{' (!'}' .)* '}'
|		!'}' .

EQUAL=		'=' -
COLON=		':' -
SEMICOLON=	';' -
BAR=		'|' -
AND=		'&' -
NOT=		'!' -
QUESTION=	'?' -
STAR=		'*' -
PLUS=		'+' -
OPEN=		'(' -
CLOSE=		')' -
DOT=		'.' -
BEGIN=		'<' -
END=		'>' -
RPERCENT=	'%}' -

-=		(space | comment)*
space=		' ' | '\t' | end-of-line
comment=	'#' (!end-of-line .)* end-of-line
end-of-line=	'\r\n' | '\n' | '\r'
end-of-file=	!.

%%
void makeHeader(GREG *G, char *text)
{
  Header *header= (Header *)YY_ALLOC(sizeof(Header), G->data);
  header->text= YY_STRDUP(G, text);
  header->next= headers;
  headers= header;
}

void makeTrailer(GREG *G, char *text)
{
  trailer= YY_STRDUP(G, text);
}

static void version(char *name)
{
  printf("%s version %d.%d.%d\n", name, GREG_MAJOR, GREG_MINOR, GREG_LEVEL);
}

static void usage(char *name)
{
  version(name);
  fprintf(stderr, "usage: %s [<option>...] [<file>...]\n", name);
  fprintf(stderr, "where <option> can be\n");
  fprintf(stderr, "  -h          print this help information\n");
  fprintf(stderr, "  -o <ofile>  write output to <ofile>\n");
  fprintf(stderr, "  -v          be verbose\n");
#ifdef YY_DEBUG
  fprintf(stderr, "  -vv         be more verbose\n");
#endif
  fprintf(stderr, "  -V          print version number and exit\n");
  fprintf(stderr, "if no <file> is given, input is read from stdin\n");
  fprintf(stderr, "if no <ofile> is given, output is written to stdout\n");
  exit(1);
}

int main(int argc, char **argv)
{
  GREG *G;
  Node *n;
  int   c;
  YY_XTYPE data = NULL;

  FILE *input= stdin;
  output= stdout;

  while (-1 != (c= getopt(argc, argv, "Vho:v")))
    {
      switch (c)
	{
	case 'V':
	  version(basename(argv[0]));
	  exit(0);

	case 'h':
	  usage(basename(argv[0]));
	  break;

	case 'o':
	  if (!(output= fopen(optarg, "w")))
	    {
	      perror(optarg);
	      exit(1);
	    }
	  break;

	case 'v':
	  verboseFlag++;
	  break;

	default:
	  fprintf(stderr, "for usage try: %s -h\n", argv[0]);
	  exit(1);
	}
    }
  argc -= optind;
  argv += optind;

  YY_INITDATA;
  G = yyparse_new(data);
#ifdef YY_DEBUG
  if (verboseFlag > 0) {
    yydebug = YYDEBUG_PARSE;
    if (verboseFlag > 1)
      yydebug = YYDEBUG_PARSE + YYDEBUG_VERBOSE;
  }
#endif

  if (argc)
    {
      for (;  argc;  --argc, ++argv)
	{
	  if (strcmp(*argv, "-"))
	    {
	      if (!(input= fopen(*argv, "r")))
		{
		  perror(*argv);
		  exit(1);
		}
	      G->filename= *argv;
	      G->input= input;
	    }
	  if (!yyparse(G))
	    YY_ERROR("syntax error");
	  if (input != stdin)
	    fclose(input);
	}
    }
  else
    if (!yyparse(G))
      YY_ERROR("syntax error");

  if (verboseFlag)
    for (n= rules;  n;  n= n->any.next)
      Rule_print(n);

  Rule_compile_c_header();

  while (headers) {
    Header *tmp = headers;
    fprintf(output, "%s\n", headers->text);
    YY_FREE(headers->text);
    tmp= headers->next;
    YY_FREE(headers);
    headers= tmp;
  }

  if (rules) {
    Rule_compile_c(rules);
  }

  yyparse_free(G);
  if (trailer) {
    fprintf(output, "%s\n", trailer);
    YY_FREE(trailer);
  }

  return 0;
}
