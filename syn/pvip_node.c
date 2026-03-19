#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <inttypes.h>
#include "pvip.h"
#include "pvip_private.h"

#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif
#ifndef MAX
#define MAX(a,b) (((a)>(b))?(a):(b))
#endif

#define PVIP_ARENA_SIZE 1024

#define ALLOC_CHECK(p) \
    do { \
        if (!p) { \
            fprintf(stderr, "[PVIP] Cannot allocate memory"); \
            abort(); \
        } \
    } while (0)

struct pvip_arena {
    struct pvip_arena* next;
    int idx;
    PVIPNode nodes[PVIP_ARENA_SIZE];
};

typedef struct pvip_t {
    struct pvip_arena* arena;
} pvip_t;

struct pvip_t* pvip_new() {
    pvip_t* pvip = malloc(sizeof(pvip_t));
    ALLOC_CHECK(pvip);
    pvip->arena = malloc(sizeof(struct pvip_arena));
    memset(pvip->arena, 0, sizeof(struct pvip_arena));
    return pvip;
}

// basic memory pool
PVIPNode* pvip_node_alloc(struct pvip_t* pvip) {
    if (pvip->arena->idx==PVIP_ARENA_SIZE) {
        struct pvip_arena* arena = malloc(sizeof(struct pvip_arena));
        ALLOC_CHECK(arena);
        arena->next = pvip->arena;
        pvip->arena = arena;
    }
    return &(pvip->arena->nodes[pvip->arena->idx++]);
}

void pvip_free(struct pvip_t* pvip) {
    struct pvip_arena* arena = pvip->arena;
    while (arena) {
        int i;
        for (i=0; i<arena->idx; ++i) {
            PVIPNode* node = &(arena->nodes[i]);
            switch (PVIP_node_category(node->type)) {
            case PVIP_CATEGORY_CHILDREN:
                free(node->children.nodes);
                break;
            case PVIP_CATEGORY_STRING:
                PVIP_string_destroy(node->pv);
                break;
            case PVIP_CATEGORY_UNKNOWN:
            case PVIP_CATEGORY_INT:
            case PVIP_CATEGORY_NUMBER:
                break; // nop
            }
        }
        struct pvip_arena* next =  arena->next;
        free(arena);
        arena = next;
    }
    free(pvip);
}

PVIPNode * PVIP_node_new_int(PVIPParserContext* parser, PVIP_node_type_t type, int64_t n) {
    PVIPNode *node = pvip_node_alloc(parser->pvip);
    assert(PVIP_node_category(type) == PVIP_CATEGORY_INT);
    node->type = type;
    node->iv = n;
    node->line_number = 0;
    return node;
}

PVIPNode * PVIP_node_new_intf(PVIPParserContext* parser, PVIP_node_type_t type, const char *str, size_t len, int base) {
    char * buf = malloc(len+1);
    char *bufp = buf;
    int i;
    for (i=0; i<len; i++) {
        if (str[i] != '_') {
            *bufp++ = str[i];
        }
    }
    *bufp++ = '\0';
    int64_t n = strtoll(buf, NULL, base);
    free(buf);
    return PVIP_node_new_int(parser, type, n);
}

PVIPNode * PVIP_node_new_string(PVIPParserContext* parser, PVIP_node_type_t type, const char* str, size_t len) {
    PVIPNode *node = pvip_node_alloc(parser->pvip);
    assert(
         type != PVIP_NODE_IDENT
      || type != PVIP_NODE_VARIABLE
      || type != PVIP_NODE_STRING
    );
    node->type = type;
    node->pv = PVIP_string_new();
    node->line_number = 0;
    PVIP_string_concat(node->pv, str, len);
    return node;
}

PVIPNode* PVIP_node_append_string(PVIPParserContext *parser, PVIPNode *node, const char* txt, size_t length) {
    if (node->type == PVIP_NODE_STRING_CONCAT) {
        if (node->children.nodes[node->children.size-1]->type == PVIP_NODE_STRING) {
            PVIP_string_concat(node->children.nodes[node->children.size-1]->pv, txt, length);
            return node;
        } else {
            PVIPNode *s = PVIP_node_new_string(parser, PVIP_NODE_STRING, txt, length);
            return PVIP_node_new_children2(parser, PVIP_NODE_STRING_CONCAT, node, s);
        }
    }

    assert(PVIP_node_category(node->type) == PVIP_CATEGORY_STRING);
    PVIP_string_concat(node->pv, txt, length);
    return node;
}

PVIPNode* PVIP_node_append_string_from_hex(PVIPParserContext *parser, PVIPNode *node, const char* str, size_t len) {
    assert(PVIP_node_category(node->type) == PVIP_CATEGORY_STRING);
    assert(len==2);

    char buf[3];
    buf[0] = str[0];
    buf[1] = str[1];
    buf[2] = '\0';
    char c = strtol(buf, NULL, 16);
    return PVIP_node_append_string(parser, node, &c, 1);
}

PVIPNode* PVIP_node_append_string_from_dec(PVIPParserContext *parser, PVIPNode *node, const char* str, size_t len) {
    assert(PVIP_node_category(node->type) == PVIP_CATEGORY_STRING);
    assert(len==2);

    char buf[3];
    buf[0] = str[0];
    buf[1] = str[1];
    buf[2] = '\0';
    char c = strtol(buf, NULL, 10);
    return PVIP_node_append_string(parser, node, &c, 1);
}

PVIPNode* PVIP_node_append_string_from_oct(PVIPParserContext *parser, PVIPNode *node, const char* str, size_t len) {
    assert(PVIP_node_category(node->type) == PVIP_CATEGORY_STRING);
    assert(len==2);

    char buf[3];
    buf[0] = str[0];
    buf[1] = str[1];
    buf[2] = '\0';
    char c = strtol(buf, NULL, 8);
    return PVIP_node_append_string(parser, node, &c, 1);
}

PVIPNode* PVIP_node_append_string_node(PVIPParserContext *parser, PVIPNode*node, PVIPNode*stuff) {
    if (node->type == PVIP_NODE_STRING) {
        return PVIP_node_new_children2(parser, PVIP_NODE_STRING_CONCAT, node, stuff);
    } else if (node->type == PVIP_NODE_STRING_CONCAT) {
        return PVIP_node_new_children2(parser, PVIP_NODE_STRING_CONCAT, node, stuff);
    } else {
        abort();
    }
}


void PVIP_node_change_type(PVIPNode *node, PVIP_node_type_t type) {
    assert(PVIP_node_category(node->type) == PVIP_node_category(type));
    node->type = type;
}

PVIPNode* PVIP_node_new_number(PVIPParserContext* parser, PVIP_node_type_t type, const char *str, size_t len) {
    PVIPNode *node = pvip_node_alloc(parser->pvip);
    assert(PVIP_node_category(type) == PVIP_CATEGORY_NUMBER);
    node->type = type;
    node->nv = strtod(str, NULL);
    node->line_number = 0;
    return node;
}

PVIPNode* PVIP_node_new_children(PVIPParserContext *parser, PVIP_node_type_t type) {
    PVIPNode *node = pvip_node_alloc(parser->pvip);
    memset(node, 0, sizeof(PVIPNode));
    assert(type != PVIP_NODE_NUMBER);
    assert(type != PVIP_NODE_INT);
    node->type = type;
    node->children.size  = 0;
    node->children.nodes = NULL;
    if (parser->line_number_stack_size > 0) {
        node->line_number = parser->line_number_stack[parser->line_number_stack_size-1];
    } else {
        node->line_number = parser->line_number;
    }
    return node;
}
PVIPNode* PVIP_node_new_children1(PVIPParserContext* parser, PVIP_node_type_t type, PVIPNode* n1) {
    assert(n1);

    PVIPNode* node = PVIP_node_new_children(parser, type);
    PVIP_node_push_child(node, n1);
    return node;
}

PVIPNode* PVIP_node_new_children2(PVIPParserContext* parser, PVIP_node_type_t type, PVIPNode* n1, PVIPNode *n2) {
    assert(n1);
    assert(n2);

    PVIPNode* node = PVIP_node_new_children(parser, type);
    PVIP_node_push_child(node, n1);
    PVIP_node_push_child(node, n2);
    return node;
}

PVIPNode* PVIP_node_new_children3(PVIPParserContext* parser, PVIP_node_type_t type, PVIPNode* n1, PVIPNode *n2, PVIPNode *n3) {
    assert(n1);
    assert(n2);
    assert(n3);

    PVIPNode* node = PVIP_node_new_children(parser, type);
    PVIP_node_push_child(node, n1);
    PVIP_node_push_child(node, n2);
    PVIP_node_push_child(node, n3);
    return node;
}

PVIPNode* PVIP_node_new_children4(PVIPParserContext* parser, PVIP_node_type_t type, PVIPNode* n1, PVIPNode *n2, PVIPNode *n3, PVIPNode *n4) {
    assert(n1);
    assert(n2);
    assert(n3);
    assert(n4);

    PVIPNode* node = PVIP_node_new_children(parser, type);
    PVIP_node_push_child(node, n1);
    PVIP_node_push_child(node, n2);
    PVIP_node_push_child(node, n3);
    PVIP_node_push_child(node, n4);
    return node;
}

PVIPNode* PVIP_node_new_children5(PVIPParserContext* parser, PVIP_node_type_t type, PVIPNode* n1, PVIPNode *n2, PVIPNode *n3, PVIPNode *n4, PVIPNode *n5) {
    assert(n1);
    assert(n2);
    assert(n3);
    assert(n4);
    assert(n5);

    PVIPNode* node = PVIP_node_new_children(parser, type);
    PVIP_node_push_child(node, n1);
    PVIP_node_push_child(node, n2);
    PVIP_node_push_child(node, n3);
    PVIP_node_push_child(node, n4);
    PVIP_node_push_child(node, n5);
    return node;
}

void PVIP_node_push_child(PVIPNode* node, PVIPNode* child) {
    assert(child);

    node->children.nodes = (PVIPNode**)realloc(node->children.nodes, sizeof(PVIPNode*)*(node->children.size+1));
    assert(node->children.nodes);
    node->children.nodes[node->children.size] = child;
    node->children.size++;
}

PVIP_category_t PVIP_node_category(PVIP_node_type_t type) {
    switch (type) {
    case PVIP_NODE_STRING:
    case PVIP_NODE_VARIABLE:
    case PVIP_NODE_IDENT:
    case PVIP_NODE_REGEXP:
    case PVIP_NODE_PERL5_REGEXP:
    case PVIP_NODE_ATTRIBUTE_VARIABLE:
    case PVIP_NODE_PATH:
    case PVIP_NODE_SLANGS:
    case PVIP_NODE_UNICODE_CHAR:
        return PVIP_CATEGORY_STRING;
    case PVIP_NODE_INT:
        return PVIP_CATEGORY_INT;
    case PVIP_NODE_NUMBER:
    case PVIP_NODE_COMPLEX:
        return PVIP_CATEGORY_NUMBER;
    default:
        return PVIP_CATEGORY_CHILDREN;
    }
}

static void _PVIP_node_as_sexp(PVIPNode * node, PVIPString *buf, int indent) {
    assert(node);

    PVIP_string_concat(buf, "(", 1);
    const char *name = PVIP_node_name(node->type);
    PVIP_string_concat(buf, name, strlen(name));
    switch (PVIP_node_category(node->type)) {
    case PVIP_CATEGORY_STRING: {
        int i;
        PVIP_string_concat(buf, " ", 1);
        PVIP_string_concat(buf, "\"", 1);
        for (i=0; i<node->pv->len; i++) {
            char c = node->pv->buf[i];
            switch (c) {
            case '\\': PVIP_string_concat(buf, "\\\\",    2); break;
            case '"':  PVIP_string_concat(buf, "\\\"",    2); break;
            case '/':  PVIP_string_concat(buf, "\\/",     2); break;
            case '\b': PVIP_string_concat(buf, "\\b",     2); break;
            case '\f': PVIP_string_concat(buf, "\\f",     2); break;
            case '\n': PVIP_string_concat(buf, "\\n",     2); break;
            case '\r': PVIP_string_concat(buf, "\\r",     2); break;
            case '\t': PVIP_string_concat(buf, "\\t",     2); break;
            case '\a': PVIP_string_concat(buf, "\\u0007", 6); break;
            case '\0': PVIP_string_concat(buf, "\\0",     2); break;
            default:   PVIP_string_concat(buf, &c,        1); break;
            }
        }
        PVIP_string_concat(buf, "\"", 1);
        break;
    }
    case PVIP_CATEGORY_INT:
        PVIP_string_concat(buf, " ", 1);
        PVIP_string_concat_int(buf, node->iv);
        break;
    case PVIP_CATEGORY_NUMBER:
        PVIP_string_concat(buf, " ", 1);
        PVIP_string_concat_number(buf, node->nv);
        break;
    case PVIP_CATEGORY_CHILDREN: {
        if (node->children.size > 0) {
            PVIP_string_concat(buf, " ", 1);
            int i=0;
            for (i=0; i<node->children.size; i++) {
                _PVIP_node_as_sexp(node->children.nodes[i], buf, indent+1);
                if (i!=node->children.size-1) {
                    PVIP_string_concat(buf, " ", 1);
                }
            }
        }
        break;
    }
    case PVIP_CATEGORY_UNKNOWN:
        abort();
    }
    PVIP_string_concat(buf, ")", 1);
}

void PVIP_node_as_sexp(PVIPNode * node, PVIPString *buf) {
    assert(node);
    _PVIP_node_as_sexp(node, buf, 0);
}

void PVIP_node_dump_sexp(PVIPNode * node) {
    PVIPString*buf = PVIP_string_new();
    PVIP_node_as_sexp(node, buf);
    PVIP_string_say(buf);
    PVIP_string_destroy(buf);
}
