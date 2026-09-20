#include "ase/json.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *key;
    AseJsonValue *value;
} JsonObjectEntry;

struct AseJsonValue {
    AseJsonType type;
    union {
        bool boolean;
        double number;
        char *string;
        struct {
            AseJsonValue **items;
            size_t count;
            size_t capacity;
        } array;
        struct {
            JsonObjectEntry *entries;
            size_t count;
            size_t capacity;
        } object;
    } as;
};

static AseJsonValue *json_alloc(AseJsonType type) {
    AseJsonValue *v = (AseJsonValue *)calloc(1, sizeof(AseJsonValue));
    if (v != NULL) {
        v->type = type;
    }
    return v;
}

AseJsonValue *ase_json_null(void) {
    return json_alloc(ASE_JSON_NULL);
}

AseJsonValue *ase_json_bool(bool value) {
    AseJsonValue *v = json_alloc(ASE_JSON_BOOL);
    if (v != NULL) {
        v->as.boolean = value;
    }
    return v;
}

AseJsonValue *ase_json_number(double value) {
    AseJsonValue *v = json_alloc(ASE_JSON_NUMBER);
    if (v != NULL) {
        v->as.number = value;
    }
    return v;
}

AseJsonValue *ase_json_string(const char *value) {
    AseJsonValue *v = json_alloc(ASE_JSON_STRING);
    if (v == NULL) {
        return NULL;
    }
    size_t len = strlen(value) + 1;
    v->as.string = (char *)malloc(len);
    if (v->as.string == NULL) {
        free(v);
        return NULL;
    }
    memcpy(v->as.string, value, len);
    return v;
}

AseJsonValue *ase_json_array(void) {
    return json_alloc(ASE_JSON_ARRAY);
}

AseJsonValue *ase_json_object(void) {
    return json_alloc(ASE_JSON_OBJECT);
}

void ase_json_destroy(AseJsonValue *value) {
    if (value == NULL) {
        return;
    }

    switch (value->type) {
    case ASE_JSON_STRING:
        free(value->as.string);
        break;
    case ASE_JSON_ARRAY:
        for (size_t i = 0; i < value->as.array.count; i++) {
            ase_json_destroy(value->as.array.items[i]);
        }
        free(value->as.array.items);
        break;
    case ASE_JSON_OBJECT:
        for (size_t i = 0; i < value->as.object.count; i++) {
            free(value->as.object.entries[i].key);
            ase_json_destroy(value->as.object.entries[i].value);
        }
        free(value->as.object.entries);
        break;
    default:
        break;
    }

    free(value);
}

bool ase_json_array_append(AseJsonValue *array, AseJsonValue *item) {
    if (array == NULL || array->type != ASE_JSON_ARRAY || item == NULL) {
        ase_json_destroy(item);
        return false;
    }

    if (array->as.array.count == array->as.array.capacity) {
        size_t new_cap = array->as.array.capacity == 0 ? 4 : array->as.array.capacity * 2;
        AseJsonValue **grown = (AseJsonValue **)realloc(array->as.array.items, new_cap * sizeof(AseJsonValue *));
        if (grown == NULL) {
            ase_json_destroy(item);
            return false;
        }
        array->as.array.items = grown;
        array->as.array.capacity = new_cap;
    }

    array->as.array.items[array->as.array.count++] = item;
    return true;
}

bool ase_json_object_set(AseJsonValue *object, const char *key, AseJsonValue *value) {
    if (object == NULL || object->type != ASE_JSON_OBJECT || key == NULL || value == NULL) {
        ase_json_destroy(value);
        return false;
    }

    for (size_t i = 0; i < object->as.object.count; i++) {
        if (strcmp(object->as.object.entries[i].key, key) == 0) {
            ase_json_destroy(object->as.object.entries[i].value);
            object->as.object.entries[i].value = value;
            return true;
        }
    }

    if (object->as.object.count == object->as.object.capacity) {
        size_t new_cap = object->as.object.capacity == 0 ? 4 : object->as.object.capacity * 2;
        JsonObjectEntry *grown =
            (JsonObjectEntry *)realloc(object->as.object.entries, new_cap * sizeof(JsonObjectEntry));
        if (grown == NULL) {
            ase_json_destroy(value);
            return false;
        }
        object->as.object.entries = grown;
        object->as.object.capacity = new_cap;
    }

    size_t key_len = strlen(key) + 1;
    char *key_copy = (char *)malloc(key_len);
    if (key_copy == NULL) {
        ase_json_destroy(value);
        return false;
    }
    memcpy(key_copy, key, key_len);

    object->as.object.entries[object->as.object.count].key = key_copy;
    object->as.object.entries[object->as.object.count].value = value;
    object->as.object.count++;
    return true;
}

AseJsonType ase_json_type(const AseJsonValue *value) {
    return value == NULL ? ASE_JSON_NULL : value->type;
}

bool ase_json_get_bool(const AseJsonValue *value, bool fallback) {
    return (value != NULL && value->type == ASE_JSON_BOOL) ? value->as.boolean : fallback;
}

double ase_json_get_number(const AseJsonValue *value, double fallback) {
    return (value != NULL && value->type == ASE_JSON_NUMBER) ? value->as.number : fallback;
}

const char *ase_json_get_string(const AseJsonValue *value) {
    return (value != NULL && value->type == ASE_JSON_STRING) ? value->as.string : NULL;
}

size_t ase_json_array_size(const AseJsonValue *array) {
    return (array != NULL && array->type == ASE_JSON_ARRAY) ? array->as.array.count : 0;
}

AseJsonValue *ase_json_array_get(const AseJsonValue *array, size_t index) {
    if (array == NULL || array->type != ASE_JSON_ARRAY || index >= array->as.array.count) {
        return NULL;
    }
    return array->as.array.items[index];
}

AseJsonValue *ase_json_object_get(const AseJsonValue *object, const char *key) {
    if (object == NULL || object->type != ASE_JSON_OBJECT || key == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < object->as.object.count; i++) {
        if (strcmp(object->as.object.entries[i].key, key) == 0) {
            return object->as.object.entries[i].value;
        }
    }
    return NULL;
}

size_t ase_json_object_size(const AseJsonValue *object) {
    if (object == NULL || object->type != ASE_JSON_OBJECT) {
        return 0;
    }
    return object->as.object.count;
}

const char *ase_json_object_key(const AseJsonValue *object, size_t index) {
    if (object == NULL || object->type != ASE_JSON_OBJECT || index >= object->as.object.count) {
        return NULL;
    }
    return object->as.object.entries[index].key;
}

AseJsonValue *ase_json_object_value(const AseJsonValue *object, size_t index) {
    if (object == NULL || object->type != ASE_JSON_OBJECT || index >= object->as.object.count) {
        return NULL;
    }
    return object->as.object.entries[index].value;
}

/* ---------------------------------------------------------------- parser */

typedef struct {
    const char *data;
    size_t len;
    size_t pos;
    bool error;
} JsonParser;

static void skip_whitespace(JsonParser *p) {
    while (p->pos < p->len) {
        char c = p->data[p->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            p->pos++;
        } else {
            break;
        }
    }
}

static int peek(const JsonParser *p) {
    return p->pos < p->len ? (unsigned char)p->data[p->pos] : -1;
}

static bool match_literal(JsonParser *p, const char *literal) {
    size_t len = strlen(literal);
    if (p->pos + len > p->len || memcmp(p->data + p->pos, literal, len) != 0) {
        return false;
    }
    p->pos += len;
    return true;
}

static bool append_byte(char **buf, size_t *len, size_t *cap, char byte) {
    if (*len + 2 > *cap) {
        size_t new_cap = (*cap == 0) ? 16 : *cap * 2;
        char *grown = (char *)realloc(*buf, new_cap);
        if (grown == NULL) {
            return false;
        }
        *buf = grown;
        *cap = new_cap;
    }
    (*buf)[(*len)++] = byte;
    return true;
}

static bool append_utf8(char **buf, size_t *len, size_t *cap, uint32_t codepoint) {
    char utf8[4];
    int n;

    if (codepoint <= 0x7F) {
        utf8[0] = (char)codepoint;
        n = 1;
    } else if (codepoint <= 0x7FF) {
        utf8[0] = (char)(0xC0 | (codepoint >> 6));
        utf8[1] = (char)(0x80 | (codepoint & 0x3F));
        n = 2;
    } else if (codepoint <= 0xFFFF) {
        utf8[0] = (char)(0xE0 | (codepoint >> 12));
        utf8[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        utf8[2] = (char)(0x80 | (codepoint & 0x3F));
        n = 3;
    } else {
        utf8[0] = (char)(0xF0 | (codepoint >> 18));
        utf8[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        utf8[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        utf8[3] = (char)(0x80 | (codepoint & 0x3F));
        n = 4;
    }

    for (int i = 0; i < n; i++) {
        if (!append_byte(buf, len, cap, utf8[i])) {
            return false;
        }
    }
    return true;
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static bool parse_hex4(JsonParser *p, uint32_t *out) {
    if (p->pos + 4 > p->len) {
        return false;
    }
    uint32_t value = 0;
    for (int i = 0; i < 4; i++) {
        int d = hex_digit(p->data[p->pos + (size_t)i]);
        if (d < 0) {
            return false;
        }
        value = value * 16 + (uint32_t)d;
    }
    p->pos += 4;
    *out = value;
    return true;
}

/* Assumes p->pos is at the opening quote. Returns a malloc'd, NUL-terminated
 * UTF-8 string, or NULL (and sets p->error) on malformed input. */
static char *parse_string_raw(JsonParser *p) {
    if (peek(p) != '"') {
        p->error = true;
        return NULL;
    }
    p->pos++;

    char *buf = NULL;
    size_t len = 0;
    size_t cap = 0;

    for (;;) {
        if (p->pos >= p->len) {
            p->error = true;
            free(buf);
            return NULL;
        }
        char c = p->data[p->pos];

        if (c == '"') {
            p->pos++;
            if (!append_byte(&buf, &len, &cap, '\0')) {
                free(buf);
                return NULL;
            }
            return buf;
        }

        if ((unsigned char)c < 0x20) {
            p->error = true;
            free(buf);
            return NULL;
        }

        if (c != '\\') {
            if (!append_byte(&buf, &len, &cap, c)) {
                free(buf);
                return NULL;
            }
            p->pos++;
            continue;
        }

        p->pos++;
        if (p->pos >= p->len) {
            p->error = true;
            free(buf);
            return NULL;
        }
        char esc = p->data[p->pos];
        p->pos++;

        bool ok = true;
        switch (esc) {
        case '"':
            ok = append_byte(&buf, &len, &cap, '"');
            break;
        case '\\':
            ok = append_byte(&buf, &len, &cap, '\\');
            break;
        case '/':
            ok = append_byte(&buf, &len, &cap, '/');
            break;
        case 'b':
            ok = append_byte(&buf, &len, &cap, '\b');
            break;
        case 'f':
            ok = append_byte(&buf, &len, &cap, '\f');
            break;
        case 'n':
            ok = append_byte(&buf, &len, &cap, '\n');
            break;
        case 'r':
            ok = append_byte(&buf, &len, &cap, '\r');
            break;
        case 't':
            ok = append_byte(&buf, &len, &cap, '\t');
            break;
        case 'u': {
            uint32_t code;
            if (!parse_hex4(p, &code)) {
                p->error = true;
                free(buf);
                return NULL;
            }
            if (code >= 0xD800 && code <= 0xDBFF) {
                /* high surrogate: require a following \uXXXX low surrogate */
                if (p->pos + 2 > p->len || p->data[p->pos] != '\\' || p->data[p->pos + 1] != 'u') {
                    p->error = true;
                    free(buf);
                    return NULL;
                }
                p->pos += 2;
                uint32_t low;
                if (!parse_hex4(p, &low) || low < 0xDC00 || low > 0xDFFF) {
                    p->error = true;
                    free(buf);
                    return NULL;
                }
                code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
            }
            ok = append_utf8(&buf, &len, &cap, code);
            break;
        }
        default:
            p->error = true;
            free(buf);
            return NULL;
        }

        if (!ok) {
            free(buf);
            return NULL;
        }
    }
}

static AseJsonValue *parse_value(JsonParser *p);

static AseJsonValue *parse_string_value(JsonParser *p) {
    char *s = parse_string_raw(p);
    if (s == NULL) {
        return NULL;
    }
    AseJsonValue *v = json_alloc(ASE_JSON_STRING);
    if (v == NULL) {
        free(s);
        return NULL;
    }
    v->as.string = s;
    return v;
}

static AseJsonValue *parse_number(JsonParser *p) {
    size_t start = p->pos;
    if (peek(p) == '-') {
        p->pos++;
    }
    if (peek(p) < '0' || peek(p) > '9') {
        p->error = true;
        return NULL;
    }
    while (peek(p) >= '0' && peek(p) <= '9') {
        p->pos++;
    }
    if (peek(p) == '.') {
        p->pos++;
        if (peek(p) < '0' || peek(p) > '9') {
            p->error = true;
            return NULL;
        }
        while (peek(p) >= '0' && peek(p) <= '9') {
            p->pos++;
        }
    }
    if (peek(p) == 'e' || peek(p) == 'E') {
        p->pos++;
        if (peek(p) == '+' || peek(p) == '-') {
            p->pos++;
        }
        if (peek(p) < '0' || peek(p) > '9') {
            p->error = true;
            return NULL;
        }
        while (peek(p) >= '0' && peek(p) <= '9') {
            p->pos++;
        }
    }

    size_t len = p->pos - start;
    char buf[64];
    if (len >= sizeof(buf)) {
        p->error = true;
        return NULL;
    }
    memcpy(buf, p->data + start, len);
    buf[len] = '\0';

    return ase_json_number(strtod(buf, NULL));
}

static AseJsonValue *parse_array(JsonParser *p) {
    p->pos++; /* '[' */
    AseJsonValue *arr = ase_json_array();
    if (arr == NULL) {
        p->error = true;
        return NULL;
    }

    skip_whitespace(p);
    if (peek(p) == ']') {
        p->pos++;
        return arr;
    }

    for (;;) {
        skip_whitespace(p);
        AseJsonValue *item = parse_value(p);
        if (item == NULL || !ase_json_array_append(arr, item)) {
            ase_json_destroy(arr);
            return NULL;
        }

        skip_whitespace(p);
        int c = peek(p);
        if (c == ',') {
            p->pos++;
            continue;
        }
        if (c == ']') {
            p->pos++;
            break;
        }
        p->error = true;
        ase_json_destroy(arr);
        return NULL;
    }
    return arr;
}

static AseJsonValue *parse_object(JsonParser *p) {
    p->pos++; /* '{' */
    AseJsonValue *obj = ase_json_object();
    if (obj == NULL) {
        p->error = true;
        return NULL;
    }

    skip_whitespace(p);
    if (peek(p) == '}') {
        p->pos++;
        return obj;
    }

    for (;;) {
        skip_whitespace(p);
        if (peek(p) != '"') {
            p->error = true;
            ase_json_destroy(obj);
            return NULL;
        }
        char *key = parse_string_raw(p);
        if (key == NULL) {
            ase_json_destroy(obj);
            return NULL;
        }

        skip_whitespace(p);
        if (peek(p) != ':') {
            p->error = true;
            free(key);
            ase_json_destroy(obj);
            return NULL;
        }
        p->pos++;
        skip_whitespace(p);

        AseJsonValue *value = parse_value(p);
        if (value == NULL) {
            free(key);
            ase_json_destroy(obj);
            return NULL;
        }

        bool ok = ase_json_object_set(obj, key, value);
        free(key);
        if (!ok) {
            ase_json_destroy(obj);
            return NULL;
        }

        skip_whitespace(p);
        int c = peek(p);
        if (c == ',') {
            p->pos++;
            continue;
        }
        if (c == '}') {
            p->pos++;
            break;
        }
        p->error = true;
        ase_json_destroy(obj);
        return NULL;
    }
    return obj;
}

static AseJsonValue *parse_value(JsonParser *p) {
    skip_whitespace(p);
    int c = peek(p);

    switch (c) {
    case '{':
        return parse_object(p);
    case '[':
        return parse_array(p);
    case '"':
        return parse_string_value(p);
    case 't':
        if (match_literal(p, "true")) {
            return ase_json_bool(true);
        }
        p->error = true;
        return NULL;
    case 'f':
        if (match_literal(p, "false")) {
            return ase_json_bool(false);
        }
        p->error = true;
        return NULL;
    case 'n':
        if (match_literal(p, "null")) {
            return ase_json_null();
        }
        p->error = true;
        return NULL;
    default:
        if (c == '-' || (c >= '0' && c <= '9')) {
            return parse_number(p);
        }
        p->error = true;
        return NULL;
    }
}

AseJsonValue *ase_json_parse(const char *text, size_t len) {
    if (text == NULL) {
        return NULL;
    }

    JsonParser p;
    p.data = text;
    p.len = len;
    p.pos = 0;
    p.error = false;

    AseJsonValue *value = parse_value(&p);
    if (value == NULL || p.error) {
        ase_json_destroy(value);
        return NULL;
    }

    skip_whitespace(&p);
    if (p.pos != p.len) {
        ase_json_destroy(value);
        return NULL;
    }

    return value;
}

/* ---------------------------------------------------------------- writer */

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} JsonWriter;

static bool writer_reserve(JsonWriter *w, size_t extra) {
    if (w->len + extra + 1 <= w->cap) {
        return true;
    }
    size_t new_cap = (w->cap == 0) ? 64 : w->cap * 2;
    while (new_cap < w->len + extra + 1) {
        new_cap *= 2;
    }
    char *grown = (char *)realloc(w->buf, new_cap);
    if (grown == NULL) {
        return false;
    }
    w->buf = grown;
    w->cap = new_cap;
    return true;
}

static bool writer_append(JsonWriter *w, const char *s, size_t n) {
    if (!writer_reserve(w, n)) {
        return false;
    }
    memcpy(w->buf + w->len, s, n);
    w->len += n;
    return true;
}

static bool writer_append_str(JsonWriter *w, const char *s) {
    return writer_append(w, s, strlen(s));
}

static bool writer_append_char(JsonWriter *w, char c) {
    return writer_append(w, &c, 1);
}

static bool write_json_string(JsonWriter *w, const char *s) {
    if (!writer_append_char(w, '"')) {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)s; *p != '\0'; p++) {
        switch (*p) {
        case '"':
            if (!writer_append_str(w, "\\\"")) return false;
            break;
        case '\\':
            if (!writer_append_str(w, "\\\\")) return false;
            break;
        case '\n':
            if (!writer_append_str(w, "\\n")) return false;
            break;
        case '\r':
            if (!writer_append_str(w, "\\r")) return false;
            break;
        case '\t':
            if (!writer_append_str(w, "\\t")) return false;
            break;
        case '\b':
            if (!writer_append_str(w, "\\b")) return false;
            break;
        case '\f':
            if (!writer_append_str(w, "\\f")) return false;
            break;
        default:
            if (*p < 0x20) {
                char esc[8];
                snprintf(esc, sizeof(esc), "\\u%04x", *p);
                if (!writer_append_str(w, esc)) return false;
            } else {
                if (!writer_append_char(w, (char)*p)) return false;
            }
        }
    }
    return writer_append_char(w, '"');
}

static bool write_value(JsonWriter *w, const AseJsonValue *value) {
    if (value == NULL) {
        return writer_append_str(w, "null");
    }

    switch (value->type) {
    case ASE_JSON_NULL:
        return writer_append_str(w, "null");
    case ASE_JSON_BOOL:
        return writer_append_str(w, value->as.boolean ? "true" : "false");
    case ASE_JSON_NUMBER: {
        char buf[64];
        double n = value->as.number;
        if (n == (double)(long long)n) {
            snprintf(buf, sizeof(buf), "%lld", (long long)n);
        } else {
            snprintf(buf, sizeof(buf), "%.17g", n);
        }
        return writer_append_str(w, buf);
    }
    case ASE_JSON_STRING:
        return write_json_string(w, value->as.string);
    case ASE_JSON_ARRAY:
        if (!writer_append_char(w, '[')) {
            return false;
        }
        for (size_t i = 0; i < value->as.array.count; i++) {
            if (i > 0 && !writer_append_char(w, ',')) {
                return false;
            }
            if (!write_value(w, value->as.array.items[i])) {
                return false;
            }
        }
        return writer_append_char(w, ']');
    case ASE_JSON_OBJECT:
        if (!writer_append_char(w, '{')) {
            return false;
        }
        for (size_t i = 0; i < value->as.object.count; i++) {
            if (i > 0 && !writer_append_char(w, ',')) {
                return false;
            }
            if (!write_json_string(w, value->as.object.entries[i].key)) {
                return false;
            }
            if (!writer_append_char(w, ':')) {
                return false;
            }
            if (!write_value(w, value->as.object.entries[i].value)) {
                return false;
            }
        }
        return writer_append_char(w, '}');
    }

    return false;
}

char *ase_json_write(const AseJsonValue *value) {
    JsonWriter w;
    w.buf = NULL;
    w.len = 0;
    w.cap = 0;

    if (!write_value(&w, value)) {
        free(w.buf);
        return NULL;
    }
    if (!writer_reserve(&w, 0)) {
        free(w.buf);
        return NULL;
    }
    w.buf[w.len] = '\0';
    return w.buf;
}
