#pragma once

#include <stddef.h>

struct pthread_atfork_record;

struct astia_atfork_context {
    struct pthread_atfork_record* first;
    struct pthread_atfork_record* last;
    size_t                        count;
};

void __astia_atfork_prepare(struct astia_atfork_context* context);
void __astia_atfork_parent(const struct astia_atfork_context* context);
void __astia_atfork_child(const struct astia_atfork_context* context);
void __astia_atfork_abort(struct astia_atfork_context* context);
