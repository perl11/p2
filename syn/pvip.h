#ifndef PVIP_H_
#define PVIP_H_

#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PVIP_VERSION_STRING "0.1.0"

#define PVIP_FALSE 0
#define PVIP_TRUE  1

typedef int PVIP_BOOL;

typedef enum {
    PVIP_NODE_UNDEF,
    PVIP_NODE_RANGE,
    PVIP_NODE_REDUCE,
    PVIP_NODE_INT,
    PVIP_NODE_NUMBER,
    PVIP_NODE_STATEMENTS,
    PVIP_NODE_DIV,
    PVIP_NODE_MUL,
    PVIP_NODE_ADD,
    PVIP_NODE_SUB,
    PVIP_NODE_IDENT,
    PVIP_NODE_FUNCALL,
    PVIP_NODE_ARGS,
    PVIP_NODE_STRING,
    PVIP_NODE_MOD,
    PVIP_NODE_VARIABLE,
    PVIP_NODE_MY,
    PVIP_NODE_OUR,
    PVIP_NODE_BIND,
    PVIP_NODE_STRING_CONCAT,
    PVIP_NODE_IF,
    PVIP_NODE_EQV,
    PVIP_NODE_EQ,
    PVIP_NODE_NE,
    PVIP_NODE_LT,
    PVIP_NODE_LE,
    PVIP_NODE_GT,
    PVIP_NODE_GE,
    PVIP_NODE_ARRAY,
    PVIP_NODE_ATPOS,
    PVIP_NODE_METHODCALL,
    PVIP_NODE_FUNC,
    PVIP_NODE_PARAMS,
    PVIP_NODE_RETURN,
    PVIP_NODE_ELSE,
    PVIP_NODE_WHILE,
    PVIP_NODE_DIE,
    PVIP_NODE_ELSIF,
    PVIP_NODE_LIST,
    PVIP_NODE_FOR,
    PVIP_NODE_UNLESS,
    PVIP_NODE_NOT,
    PVIP_NODE_CONDITIONAL,
    PVIP_NODE_NOP,
    PVIP_NODE_STREQ,
    PVIP_NODE_STRNE,
    PVIP_NODE_STRGT,
    PVIP_NODE_STRGE,
    PVIP_NODE_STRLT,
    PVIP_NODE_STRLE,
    PVIP_NODE_POW,
    PVIP_NODE_CLARGS,
    PVIP_NODE_HASH,
    PVIP_NODE_PAIR,
    PVIP_NODE_ATKEY,
    PVIP_NODE_LOGICAL_AND,
    PVIP_NODE_LOGICAL_OR,
    PVIP_NODE_LOGICAL_XOR,
    PVIP_NODE_DOR, /* '//' */
    PVIP_NODE_BIN_AND,
    PVIP_NODE_BIN_OR,
    PVIP_NODE_BIN_XOR,
    PVIP_NODE_BLOCK,
    PVIP_NODE_LAMBDA,
    PVIP_NODE_USE,
    PVIP_NODE_MODULE,
    PVIP_NODE_CLASS,
    PVIP_NODE_METHOD,
    PVIP_NODE_UNARY_PLUS,
    PVIP_NODE_UNARY_MINUS,
    PVIP_NODE_IT_METHODCALL,
    PVIP_NODE_LAST,
    PVIP_NODE_NEXT,
    PVIP_NODE_REDO,
    PVIP_NODE_POSTINC,
    PVIP_NODE_POSTDEC,
    PVIP_NODE_PREINC,
    PVIP_NODE_PREDEC,
    PVIP_NODE_UNARY_BITWISE_NEGATION,
    PVIP_NODE_BRSHIFT,
    PVIP_NODE_BLSHIFT,
    PVIP_NODE_CHAIN,
    PVIP_NODE_INPLACE_ADD,
    PVIP_NODE_INPLACE_SUB,
    PVIP_NODE_INPLACE_MUL,
    PVIP_NODE_INPLACE_DIV,
    PVIP_NODE_INPLACE_POW,
    PVIP_NODE_INPLACE_MOD,
    PVIP_NODE_INPLACE_BIN_OR,
    PVIP_NODE_INPLACE_BIN_AND,
    PVIP_NODE_INPLACE_BIN_XOR,
    PVIP_NODE_INPLACE_BLSHIFT,
    PVIP_NODE_INPLACE_BRSHIFT,
    PVIP_NODE_INPLACE_CONCAT_S,
    PVIP_NODE_REPEAT_S,
    PVIP_NODE_INPLACE_REPEAT_S,
    PVIP_NODE_STRINGIFY, /* prefix:<~> */
    PVIP_NODE_TRY,
    PVIP_NODE_REF,
    PVIP_NODE_MULTI,
    PVIP_NODE_UNARY_BOOLEAN, /* ? */
    PVIP_NODE_UNARY_UPTO, /* ^ */
    PVIP_NODE_STDOUT, /* $*OUT */
    PVIP_NODE_STDERR, /* $*ERR */
    PVIP_NODE_SCALAR_DEREF, /* $$var */
    PVIP_NODE_TW_INC, /* @*INC */
    PVIP_NODE_META_METHOD_CALL, /* $foo.^methods */
    PVIP_NODE_REGEXP,
    PVIP_NODE_SMART_MATCH, /* ~~ */
    PVIP_NODE_NOT_SMART_MATCH, /* !~~ */
    PVIP_NODE_PERL5_REGEXP, /* m:P5/./ */
    PVIP_NODE_TRUE,
    PVIP_NODE_FALSE,
    PVIP_NODE_TW_VM,  /* $*VM */
    PVIP_NODE_HAS,
    PVIP_NODE_ATTRIBUTE_VARIABLE,  /* $!var, $.var, @.var */
    PVIP_NODE_FUNCREF,           /* &var */
    PVIP_NODE_PATH, /* qp{}, IO::Path literal */
    PVIP_NODE_TW_PACKAGE, /* $?PACKAGE */
    PVIP_NODE_TW_CLASS, /* $?CLASS */
    PVIP_NODE_TW_MODULE, /* $?MODULE */
    PVIP_NODE_TW_OS, /* $*OS */
    PVIP_NODE_TW_PID, /* $*PID */
    PVIP_NODE_TW_PERLVER, /* $*PPERLVER */
    PVIP_NODE_TW_OSVER, /* $*OSVER */
    PVIP_NODE_TW_CWD, /* $*CWD */
    PVIP_NODE_TW_EXECUTABLE_NAME, /* $*EXECUTABLE_NAME */
    PVIP_NODE_TW_ROUTINE, /* &?ROUTINE */
    PVIP_NODE_SLANGS, /* $~MAIN */
    PVIP_NODE_LOGICAL_ANDTHEN, /* andthen operator */
    PVIP_NODE_VALUE_IDENTITY, /* '===' operator in S03-operators/value_equivalence.t */
    PVIP_NODE_CMP, /* 'cmp' operator */
    PVIP_NODE_SPECIAL_VARIABLE_REGEXP_MATCH, /* $/ - regex match */
    PVIP_NODE_SPECIAL_VARIABLE_EXCEPTIONS, /* $! - exceptions */
    PVIP_NODE_ENUM,
    PVIP_NODE_NUM_CMP, /* <=> */
    PVIP_NODE_UNARY_FLATTEN_OBJECT, /* unary | */
    PVIP_NODE_COMPLEX, /* 2i */
    PVIP_NODE_ROLE,
    PVIP_NODE_IS,
    PVIP_NODE_DOES,
    PVIP_NODE_JUNCTIVE_AND, /* & */
    PVIP_NODE_JUNCTIVE_SAND, /* S& */
    PVIP_NODE_JUNCTIVE_OR, /* | */
    PVIP_NODE_UNICODE_CHAR, /* \c[] */
    PVIP_NODE_STUB, /* ... */
    PVIP_NODE_EXPORT, /* is export */
    PVIP_NODE_PARAM,
    PVIP_NODE_BITWISE_OR,  /* ~| */
    PVIP_NODE_BITWISE_AND, /* ~& */
    PVIP_NODE_BITWISE_XOR, /* ~^ */
    PVIP_NODE_VARGS, /* sub foo (*@a) { } */
    PVIP_NODE_WHATEVER, /* * */
    PVIP_NODE_TW_ENV, /* %*ENV */
    PVIP_NODE_ARRAY_DEREF, /* @$v */
    PVIP_NODE_RAND, /* rand */
    PVIP_NODE_END, /* END { } */
    PVIP_NODE_BEGIN, /* BEGIN { } */
    PVIP_NODE_IS_DIVISIBLE_BY, /* %% */
    PVIP_NODE_NOT_DIVISIBLE_BY, /* !%% */
    PVIP_NODE_CONTAINER_IDENTITY, /* =:= */
    PVIP_NODE_Z, /* Z operator */
    PVIP_NODE_SUBMETHOD, /* submethod */
    PVIP_NODE_BINDAND_MAKE_READONLY, /* ::= */
    PVIP_NODE_LIST_ASSIGNMENT, /* = */
    PVIP_NODE_TW_A, /* $^a */
    PVIP_NODE_TW_B, /* $^b */
    PVIP_NODE_TW_C, /* $^c */
    PVIP_NODE_SO, /* so */
    PVIP_NODE_GCD, /* gcd */
    PVIP_NODE_KEEP, /* KEEP */
    PVIP_NODE_UNDO, /* UNDO */
    PVIP_NODE_NOW, /* now */
    PVIP_NODE_UNTIL, /* until */
    PVIP_NODE_TIME, /* time */
    PVIP_NODE_AUGMENT, /* augment */
    PVIP_NODE_IS_COPY, /* is copy */
    PVIP_NODE_LEG, /* leg operator */
    PVIP_NODE_NEED, /* need */
    PVIP_NODE_INTEGER_DIVISION, /* div */
    PVIP_NODE_LCM, /* lcm */
    PVIP_NODE_PACKAGE, /* package */
    PVIP_NODE_MINMAX, /* minmax */
    PVIP_NODE_SEQUENCE, /* ... */
    PVIP_NODE_CONTEXTUALIZER_SCALAR, /* $() */
    PVIP_NODE_CONTEXTUALIZER_ARRAY, /* @() */
    PVIP_NODE_CONTEXTUALIZER_HASH, /* %() */
    PVIP_NODE_TW_TMPDIR, /* $*TMPDIR */
    PVIP_NODE_IS_RW, /* is rw */
    PVIP_NODE_IS_REF, /* is ref */
    PVIP_NODE_PI, /* PI */
    PVIP_NODE_E, /* e */
} PVIP_node_type_t;

typedef enum {
    PVIP_CATEGORY_UNKNOWN,
    PVIP_CATEGORY_STRING,
    PVIP_CATEGORY_INT,
    PVIP_CATEGORY_NUMBER,
    PVIP_CATEGORY_CHILDREN
} PVIP_category_t;

/* bit flags for `sub ($x is rw) { }` etc. */
typedef enum {
    PVIP_FUNC_ATTR_IS_COPY = 1,
    PVIP_FUNC_ATTR_IS_RW   = 2,
    PVIP_FUNC_ATTR_IS_REF  = 4,
} PVIP_func_attr_t;

typedef struct {
    char *buf;
    size_t len;
    size_t buflen;
} PVIPString;

typedef struct _PVIPNode {
    PVIP_node_type_t type;
    int line_number;
    union {
        int64_t iv;
        double nv;
        PVIPString *pv;
        struct {
            int size;
            struct _PVIPNode **nodes;
        } children;
    };
} PVIPNode;

// memory pool
struct pvip_t;
typedef struct pvip_t pvip_t;
struct pvip_t* pvip_new();
void pvip_free(struct pvip_t* pvip);

/* parser related public apis */
PVIPNode * PVIP_parse_string(struct pvip_t* pvip, const char *string, int len, int debug, PVIPString **error);
PVIPNode * PVIP_parse_fp(struct pvip_t* pvip, FILE *fp, int debug, PVIPString **error);


/* node related public apis */
const char* PVIP_node_name(PVIP_node_type_t t);
PVIP_category_t PVIP_node_category(PVIP_node_type_t type);
void PVIP_node_as_sexp(PVIPNode * node, PVIPString *buf);

void PVIP_node_change_type(PVIPNode *node, PVIP_node_type_t type);

void PVIP_node_dump_sexp(PVIPNode * node);

/* string */
PVIPString *PVIP_string_new();
void PVIP_string_destroy(PVIPString *str);
PVIP_BOOL PVIP_string_concat(PVIPString *str, const char *src, size_t len);
PVIP_BOOL PVIP_string_concat_int(PVIPString *str, int64_t n);
PVIP_BOOL PVIP_string_concat_number(PVIPString *str, double n);
PVIP_BOOL PVIP_string_concat_char(PVIPString *str, char n);
void PVIP_string_say(PVIPString *str);
PVIP_BOOL PVIP_string_vprintf(PVIPString *str, const char*format, va_list ap);
PVIP_BOOL PVIP_string_printf(PVIPString *str, const char*format, ...);
const char * PVIP_string_c_str(PVIPString *str);

#ifdef __cplusplus
};
#endif

#endif /* PVIP_H_ */
