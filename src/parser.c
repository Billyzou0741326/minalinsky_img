#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <json-c/json.h>
#include "parser.h"

struct json_parser*
json_parser_create(void) {
    struct json_parser *j_parser = malloc(sizeof(struct json_parser));

    if (NULL == j_parser)
        return NULL;

    memset(j_parser, 0, sizeof(struct json_parser));
    json_parser_init(j_parser);
    j_parser->tok = json_tokener_new();
    return j_parser;
}

int
json_parser_init(struct json_parser *j_parser) {
    assert(NULL != j_parser);
    if (NULL == j_parser)
        return -1;

    memset(j_parser, 0, sizeof(struct json_parser));
    return 0;
}

int
json_parser_destroy(struct json_parser *j_parser) {
    if (NULL == j_parser)
        return 0;

    json_tokener_free(j_parser->tok);
    json_object_put(j_parser->json);
    free(j_parser);
    return 0;
}

json_object*
json_parser_get_json(struct json_parser *j_parser) {
    if (NULL == j_parser || NULL == j_parser->json)
        return NULL;

    return json_object_get(j_parser->json);
}

