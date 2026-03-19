#ifndef PVIP_PRIVATE_H_
#define PVIP_PRIVATE_H_

/*
 * This file contains private functins in PVIP.
 */

typedef struct {
    const char  *buf;
    size_t len;
    size_t pos;
} PVIPParserStringState;

typedef struct {
    int line_number;
    int *line_number_stack;
    size_t line_number_stack_size;
    PVIPNode *root;
    int is_string; /* Parsing from string or file pointer. */
    PVIPParserStringState *str;
    PVIPNode *literal_str; /* temporary space for string literal */
    FILE *fp;
    pvip_t* pvip;
} PVIPParserContext;

/* node */
PVIPNode* PVIP_node_new_children( PVIPParserContext*parser, PVIP_node_type_t type);
PVIPNode* PVIP_node_new_children1(PVIPParserContext*parser, PVIP_node_type_t type, PVIPNode* n1);
PVIPNode* PVIP_node_new_children2(PVIPParserContext*parser, PVIP_node_type_t type, PVIPNode* n1, PVIPNode *n2);
PVIPNode* PVIP_node_new_children3(PVIPParserContext*parser, PVIP_node_type_t type, PVIPNode* n1, PVIPNode *n2, PVIPNode *n3);
PVIPNode* PVIP_node_new_children4(PVIPParserContext*parser, PVIP_node_type_t type, PVIPNode* n1, PVIPNode *n2, PVIPNode *n3, PVIPNode *n4);
PVIPNode* PVIP_node_new_children5(PVIPParserContext*parser, PVIP_node_type_t type, PVIPNode* n1, PVIPNode *n2, PVIPNode *n3, PVIPNode *n4, PVIPNode *n5);
PVIPNode* PVIP_node_new_int(PVIPParserContext* parser, PVIP_node_type_t type, int64_t n);
PVIPNode* PVIP_node_new_intf(PVIPParserContext* parser, PVIP_node_type_t type, const char *str, size_t len, int base);
PVIPNode* PVIP_node_new_string(PVIPParserContext* parser, PVIP_node_type_t type, const char* str, size_t len);
PVIPNode* PVIP_node_new_number(PVIPParserContext* parser, PVIP_node_type_t type, const char *str, size_t len);

void PVIP_node_push_child(PVIPNode* node, PVIPNode* child);

PVIPNode* PVIP_node_append_string(PVIPParserContext *parser, PVIPNode *node, const char* str, size_t len);
PVIPNode* PVIP_node_append_string_from_hex(PVIPParserContext *parser, PVIPNode * node, const char *str, size_t len);
PVIPNode* PVIP_node_append_string_from_oct(PVIPParserContext *parser, PVIPNode * node, const char *str, size_t len);
PVIPNode* PVIP_node_append_string_from_dec(PVIPParserContext *parser, PVIPNode * node, const char *str, size_t len);
PVIPNode* PVIP_node_append_string_node(PVIPParserContext *parser, PVIPNode*node, PVIPNode*stuff);

#endif /* PVIP_PRIVATE_H_ */
