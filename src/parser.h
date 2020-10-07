#ifndef PARSER_H_
#define PARSER_H_

#include <json-c/json.h>
#include "str.h"

struct json_parser {
  json_tokener *tok;
  json_object *json;
  enum json_tokener_error jerr;
};

struct json_parser* json_parser_create(void);
int json_parser_init(struct json_parser *j_parser);
int json_parser_destroy(struct json_parser *j_parser);
json_object* json_parser_get_json(struct json_parser *j_parser);

#endif  // PARSER_H_
