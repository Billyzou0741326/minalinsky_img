#ifndef STR_H_
#define STR_H_

#include <stddef.h>

struct string {
  size_t len;
  char *cstr;
  int ref_count;
};

struct string* string_create(void);
struct string* string_get(struct string *s);
int string_put(struct string *s);

#endif  // STR_H_
