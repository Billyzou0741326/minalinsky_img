#include <stdlib.h>
#include "str.h"

struct string*
string_create(void) {
    struct string *s = NULL;
    s = malloc(sizeof(struct string));
    if (NULL == s)
        return NULL;

    s->len = 0;
    s->ref_count = 1;
    s->cstr = malloc(s->len + 1);

    if (NULL == s->cstr) {
        string_put(s);
        return NULL;
    }

    s->cstr[s->len] = '\0';
    return s;
}

struct string*
string_get(struct string* s) {
    if (NULL == s)
        return NULL;

    ++s->ref_count;
    return s;
}

int
string_put(struct string* s) {
    if (NULL == s)
        return 0;

    if (s->ref_count > 0) {
        --s->ref_count;
    }
    if (s->ref_count <= 0) {
        free(s->cstr);
        free(s);
        return 1;
    }
    return 0;
}
