#pragma once

#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define GRE_MAX_GROUPS 10
#define GRE_MAX_REPEAT 255

typedef enum {
    GRE_LITERAL,
    GRE_ANY,
    GRE_CLASS,
    GRE_BOL,
    GRE_EOL,
    GRE_GROUP,
    GRE_BACKREF,
} gre_type;

typedef struct gre_node gre_node;

struct gre_node {
    gre_type      type;
    gre_node*     next;
    unsigned char literal;
    unsigned char class_bits[32];
    int           class_negated;
    int           group_index;
    int           group_last;
    int           backref_index;
    gre_node*     child;
    int           min;
    int           max;
};

typedef struct {
    gre_node* root;
    int       groups;
    int       insensitive;
} gre_regex;

typedef struct {
    const char* start;
    const char* end;
} gre_capture;

typedef struct {
    gre_capture caps[GRE_MAX_GROUPS];
    int         insensitive;
    const char* line_start;
    const char* line_end;
} gre_ctx;

typedef struct {
    const char* p;
    gre_regex*  regex;
    int         error;
} gre_parser;

typedef enum {
    GRE_CONT_SEQUENCE,
    GRE_CONT_REPEAT,
    GRE_CONT_GROUP,
} gre_cont_kind;

typedef struct gre_cont {
    gre_cont_kind          kind;
    const gre_node*        node;
    const struct gre_cont* up;
    int                    count;
    const char*            previous;
} gre_cont;

static int       gre_match_cont(const gre_cont* cont, const char* text, gre_ctx* ctx);
static int       gre_match_sequence(const gre_node* node, const char* text, gre_ctx* ctx,
                                    const gre_cont* cont);
static int       gre_match_repeat(const gre_node* node, const char* text, gre_ctx* ctx,
                                  int count, const char* previous, const gre_cont* cont);
static int       gre_match_atom(const gre_node* node, const char* text, gre_ctx* ctx,
                                const gre_cont* cont);
static gre_node* gre_parse_sequence(gre_parser* parser, int in_group);

static void gre_free_sequence(gre_node* node) {
    while (node != 0) {
        gre_node* next = node->next;

        if (node->type == GRE_GROUP)
            gre_free_sequence(node->child);

        free(node);

        node = next;
    }
}

static void gre_free(gre_regex* regex) {
    gre_free_sequence(regex->root);

    regex->root   = 0;
    regex->groups = 0;
}

static gre_node* gre_new_node(gre_parser* parser, gre_type type) {
    gre_node* node = (gre_node*) calloc(1, sizeof *node);
    if (node == 0) {
        parser->error = 1;

        return 0;
    }

    node->type = type;
    node->min  = 1;
    node->max  = 1;

    return node;
}

static int gre_class_has(const gre_node* node, unsigned char value) {
    return (node->class_bits[value >> 3] & (1u << (value & 7u))) != 0;
}

static void gre_class_set(gre_node* node, unsigned char value) {
    node->class_bits[value >> 3] |= (unsigned char) (1u << (value & 7u));
}

static void gre_class_add_range(gre_node* node, unsigned char first, unsigned char last) {
    for (unsigned int value = first; value <= last; ++value) {
        gre_class_set(node, (unsigned char) value);
    }
}

static int gre_class_predicate(const char* name, size_t length, unsigned char value) {
    if (length == 5 && memcmp(name, "alnum", 5) == 0)
        return isalnum(value) != 0;

    if (length == 5 && memcmp(name, "alpha", 5) == 0)
        return isalpha(value) != 0;

    if (length == 5 && memcmp(name, "blank", 5) == 0)
        return value == ' ' || value == '\t';

    if (length == 5 && memcmp(name, "cntrl", 5) == 0)
        return iscntrl(value) != 0;

    if (length == 5 && memcmp(name, "digit", 5) == 0)
        return isdigit(value) != 0;

    if (length == 5 && memcmp(name, "graph", 5) == 0)
        return isgraph(value) != 0;

    if (length == 5 && memcmp(name, "lower", 5) == 0)
        return islower(value) != 0;

    if (length == 5 && memcmp(name, "print", 5) == 0)
        return isprint(value) != 0;

    if (length == 5 && memcmp(name, "punct", 5) == 0)
        return ispunct(value) != 0;

    if (length == 5 && memcmp(name, "space", 5) == 0)
        return isspace(value) != 0;

    if (length == 5 && memcmp(name, "upper", 5) == 0)
        return isupper(value) != 0;

    if (length == 6 && memcmp(name, "xdigit", 6) == 0)
        return isxdigit(value) != 0;

    return -1;
}

static int gre_class_add_named(gre_node* node, const char* name, size_t length) {
    int matched = 0;

    for (unsigned int value = 0; value < 256; ++value) {
        int present = gre_class_predicate(name, length, (unsigned char) value);
        if (present < 0)
            return -1;

        if (present != 0) {
            gre_class_set(node, (unsigned char) value);

            matched = 1;
        }
    }

    return matched;
}

static gre_node* gre_parse_class(gre_parser* parser) {
    const char* p = parser->p + 1;

    gre_node* node = gre_new_node(parser, GRE_CLASS);
    if (node == 0)
        return 0;

    if (*p == '^') {
        node->class_negated = 1;

        ++p;
    }

    int first  = 1;
    int listed = 0;

    while (*p != '\0') {
        if (*p == ']' && first == 0)
            break;

        first = 0;

        if (p[0] == '[' && p[1] == ':') {
            const char* name   = p + 2;
            const char* finish = name;

            while (*finish != '\0' && !(finish[0] == ':' && finish[1] == ']')) {
                ++finish;
            }

            if (*finish == '\0' ||
                gre_class_add_named(node, name, (size_t) (finish - name)) < 0)
                goto invalid;

            p      = finish + 2;
            listed = 1;

            continue;
        }

        if (p[0] == '[' && (p[1] == '.' || p[1] == '=')) {
            char delimiter = p[1];

            const char* finish = p + 2;

            while (*finish != '\0' && !(finish[0] == delimiter && finish[1] == ']')) {
                ++finish;
            }

            if (*finish == '\0' || finish - (p + 2) != 1)
                goto invalid;

            gre_class_set(node, (unsigned char) p[2]);

            p      = finish + 2;
            listed = 1;

            continue;
        }

        unsigned char value = (unsigned char) *p++;

        if (*p == '-' && p[1] != ']' && p[1] != '\0') {
            unsigned char last = (unsigned char) p[1];
            if (last < value)
                goto invalid;

            gre_class_add_range(node, value, last);

            p += 2;
        }
        else {
            gre_class_set(node, value);
        }

        listed = 1;
    }

    if (*p != ']' || listed == 0)
        goto invalid;

    parser->p = p + 1;

    return node;

invalid:

    parser->error = 1;

    free(node);

    return 0;
}

static int gre_parse_count(const char** text, int* value) {
    const char* p = *text;

    if (!isdigit((unsigned char) *p))
        return -1;

    int result = 0;

    while (isdigit((unsigned char) *p)) {
        result = result * 10 + (*p - '0');
        if (result > GRE_MAX_REPEAT)
            return -1;

        ++p;
    }

    *text  = p;
    *value = result;

    return 0;
}

static void gre_parse_quantifier(gre_parser* parser, gre_node* node) {
    const char* p = parser->p;

    if (*p == '*') {
        node->min = 0;
        node->max = -1;

        parser->p = p + 1;

        return;
    }

    if (p[0] != '\\' || p[1] != '{')
        return;

    p += 2;

    int minimum = 0;

    if (gre_parse_count(&p, &minimum) < 0) {
        parser->error = 1;

        return;
    }

    int maximum = minimum;

    if (*p == ',') {
        ++p;

        if (p[0] == '\\' && p[1] == '}') {
            maximum = -1;
            p += 2;
        }
        else {
            if (gre_parse_count(&p, &maximum) < 0 || maximum < minimum || p[0] != '\\' ||
                p[1] != '}') {
                parser->error = 1;

                return;
            }

            p += 2;
        }
    }
    else if (p[0] == '\\' && p[1] == '}') {
        p += 2;
    }
    else {
        parser->error = 1;

        return;
    }

    node->min = minimum;
    node->max = maximum;

    parser->p = p;
}

static gre_node* gre_parse_atom(gre_parser* parser, int sequence_start, int in_group) {
    const char* p = parser->p;

    if (*p == '\\') {
        char escaped = p[1];
        if (escaped == '\0') {
            parser->error = 1;

            return 0;
        }

        if (escaped == '(') {
            parser->p += 2;

            if (parser->regex->groups + 1 >= GRE_MAX_GROUPS) {
                parser->error = 1;

                return 0;
            }

            int index = ++parser->regex->groups;

            gre_node* child = gre_parse_sequence(parser, 1);

            if (parser->error != 0) {
                gre_free_sequence(child);

                return 0;
            }

            if (parser->p[0] != '\\' || parser->p[1] != ')') {
                parser->error = 1;

                gre_free_sequence(child);

                return 0;
            }

            parser->p += 2;

            gre_node* node = gre_new_node(parser, GRE_GROUP);
            if (node == 0) {
                gre_free_sequence(child);

                return 0;
            }

            node->group_index = index;
            node->group_last  = parser->regex->groups;
            node->child       = child;

            return node;
        }

        if (escaped >= '1' && escaped <= '9') {
            int index = escaped - '0';

            if (index > parser->regex->groups) {
                parser->error = 1;

                return 0;
            }

            parser->p += 2;

            gre_node* node = gre_new_node(parser, GRE_BACKREF);
            if (node == 0)
                return 0;

            node->backref_index = index;

            return node;
        }

        parser->p += 2;

        gre_node* node = gre_new_node(parser, GRE_LITERAL);
        if (node == 0)
            return 0;

        node->literal = (unsigned char) escaped;

        return node;
    }

    if (*p == '[')
        return gre_parse_class(parser);

    if (*p == '.') {
        parser->p += 1;

        return gre_new_node(parser, GRE_ANY);
    }

    if (*p == '^' && sequence_start != 0) {
        parser->p += 1;

        return gre_new_node(parser, GRE_BOL);
    }

    if (*p == '$') {
        int at_end = p[1] == '\0' || (in_group != 0 && p[1] == '\\' && p[2] == ')');
        if (at_end != 0) {
            parser->p += 1;

            return gre_new_node(parser, GRE_EOL);
        }
    }

    ++parser->p;

    gre_node* node = gre_new_node(parser, GRE_LITERAL);
    if (node == 0)
        return 0;

    node->literal = (unsigned char) *p;

    return node;
}

static gre_node* gre_parse_sequence(gre_parser* parser, int in_group) {
    gre_node*  head = 0;
    gre_node** tail = &head;

    int at_start = 1;

    while (*parser->p != '\0') {
        if (in_group != 0 && parser->p[0] == '\\' && parser->p[1] == ')')
            break;

        gre_node* node = gre_parse_atom(parser, at_start, in_group);
        if (node == 0)
            break;

        if (node->type != GRE_BOL && node->type != GRE_EOL)
            gre_parse_quantifier(parser, node);

        if (parser->error != 0) {
            free(node);

            break;
        }

        *tail    = node;
        tail     = &node->next;
        at_start = 0;
    }

    return head;
}

static int gre_compile(gre_regex* regex, const char* pattern, int insensitive) {
    memset(regex, 0, sizeof *regex);

    regex->insensitive = insensitive;

    gre_parser parser;

    parser.p     = pattern;
    parser.regex = regex;
    parser.error = 0;

    regex->root = gre_parse_sequence(&parser, 0);

    if (parser.error != 0 || *parser.p != '\0') {
        gre_free(regex);

        return -1;
    }

    return 0;
}

static int gre_match_char(unsigned char actual, unsigned char expected, int insensitive) {
    if (actual == expected)
        return 1;

    return insensitive != 0 && tolower(actual) == tolower(expected);
}

static int gre_match_class(const gre_node* node, unsigned char value, int insensitive) {
    int present = gre_class_has(node, value);

    if (present == 0 && insensitive != 0)
        present = gre_class_has(node, (unsigned char) tolower(value)) ||
                  gre_class_has(node, (unsigned char) toupper(value));

    return node->class_negated != 0 ? present == 0 : present != 0;
}

static int gre_match_cont(const gre_cont* cont, const char* text, gre_ctx* ctx) {
    if (cont == 0)
        return 1;

    switch (cont->kind) {
    case GRE_CONT_SEQUENCE:
        return gre_match_sequence(cont->node, text, ctx, cont->up);
    case GRE_CONT_REPEAT:
        return gre_match_repeat(cont->node, text, ctx, cont->count, cont->previous,
                                cont->up);
    case GRE_CONT_GROUP: {
        gre_capture* capture = &ctx->caps[cont->node->group_index];
        gre_capture  saved   = *capture;

        capture->end = text;

        if (gre_match_cont(cont->up, text, ctx) != 0)
            return 1;

        *capture = saved;

        return 0;
    }
    }

    return 0;
}

static int gre_match_sequence(const gre_node* node, const char* text, gre_ctx* ctx,
                              const gre_cont* cont) {
    if (node == 0)
        return gre_match_cont(cont, text, ctx);

    if (node->type == GRE_BOL)
        return text == ctx->line_start ? gre_match_sequence(node->next, text, ctx, cont)
                                       : 0;

    if (node->type == GRE_EOL)
        return text == ctx->line_end ? gre_match_sequence(node->next, text, ctx, cont)
                                     : 0;

    gre_cont next;

    next.kind     = GRE_CONT_SEQUENCE;
    next.node     = node->next;
    next.up       = cont;
    next.count    = 0;
    next.previous = 0;

    if (node->min != 1 || node->max != 1)
        return gre_match_repeat(node, text, ctx, 0, text, &next);

    return gre_match_atom(node, text, ctx, &next);
}

static int gre_match_repeat(const gre_node* node, const char* text, gre_ctx* ctx,
                            int count, const char* previous, const gre_cont* cont) {
    if (count < node->min) {
        if (node->max >= 0 && count >= node->max)
            return 0;

        gre_cont next;

        next.kind     = GRE_CONT_REPEAT;
        next.node     = node;
        next.up       = cont;
        next.count    = count + 1;
        next.previous = text;

        return gre_match_atom(node, text, ctx, &next);
    }

    if ((node->max < 0 || count < node->max) && !(text == previous && count > 0)) {
        gre_cont next;

        next.kind     = GRE_CONT_REPEAT;
        next.node     = node;
        next.up       = cont;
        next.count    = count + 1;
        next.previous = text;

        if (gre_match_atom(node, text, ctx, &next) != 0)
            return 1;
    }

    return gre_match_cont(cont, text, ctx);
}

static int gre_match_atom(const gre_node* node, const char* text, gre_ctx* ctx,
                          const gre_cont* cont) {
    switch (node->type) {
    case GRE_LITERAL:
        if (text >= ctx->line_end ||
            gre_match_char((unsigned char) *text, node->literal, ctx->insensitive) == 0)
            return 0;

        return gre_match_cont(cont, text + 1, ctx);
    case GRE_ANY:
        if (text >= ctx->line_end)
            return 0;

        return gre_match_cont(cont, text + 1, ctx);
    case GRE_CLASS:
        if (text >= ctx->line_end ||
            gre_match_class(node, (unsigned char) *text, ctx->insensitive) == 0)
            return 0;

        return gre_match_cont(cont, text + 1, ctx);
    case GRE_GROUP: {
        gre_capture saved[GRE_MAX_GROUPS];

        memcpy(saved, ctx->caps, sizeof saved);

        for (int index = node->group_index + 1; index <= node->group_last; ++index) {
            ctx->caps[index].start = 0;
            ctx->caps[index].end   = 0;
        }

        gre_capture* capture = &ctx->caps[node->group_index];

        capture->start = text;
        capture->end   = 0;

        gre_cont next;

        next.kind     = GRE_CONT_GROUP;
        next.node     = node;
        next.up       = cont;
        next.count    = 0;
        next.previous = 0;

        if (gre_match_sequence(node->child, text, ctx, &next) != 0)
            return 1;

        memcpy(ctx->caps, saved, sizeof saved);

        return 0;
    }
    case GRE_BACKREF: {
        const gre_capture* capture = &ctx->caps[node->backref_index];

        if (capture->start == 0 || capture->end == 0)
            return 0;

        size_t length = (size_t) (capture->end - capture->start);

        if ((size_t) (ctx->line_end - text) < length)
            return 0;

        for (size_t i = 0; i < length; ++i) {
            if (gre_match_char((unsigned char) text[i], (unsigned char) capture->start[i],
                               ctx->insensitive) == 0)
                return 0;
        }

        return gre_match_cont(cont, text + length, ctx);
    }
    case GRE_BOL:
    case GRE_EOL:
        break;
    }

    return 0;
}

static int gre_match(const gre_regex* regex, const char* line, size_t length) {
    const char* end = line + length;

    if (length != 0 && line[length - 1] == '\n')
        end = line + length - 1;

    gre_ctx ctx;

    memset(&ctx, 0, sizeof ctx);

    ctx.insensitive = regex->insensitive;
    ctx.line_start  = line;
    ctx.line_end    = end;

    for (const char* start = line; start <= end; ++start) {
        memset(ctx.caps, 0, sizeof ctx.caps);

        if (gre_match_sequence(regex->root, start, &ctx, 0) != 0)
            return 1;
    }

    return 0;
}
