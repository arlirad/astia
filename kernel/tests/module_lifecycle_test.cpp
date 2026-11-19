#include "../src/mod/module.hpp"
#include "support/test.hpp"
#include <sys/mman.h>

#include <cstdint>
#include <cstring>

extern unsigned char module_lifecycle_blob[];
extern unsigned int  module_lifecycle_blob_len;
extern unsigned char module_lifecycle_no_fini_blob[];
extern unsigned int  module_lifecycle_no_fini_blob_len;
extern unsigned char module_lifecycle_priority_blob[];
extern unsigned int  module_lifecycle_priority_blob_len;

#if defined(__clang__)
#define ASTIA_NO_SANITIZE_FUNCTION __attribute__((no_sanitize("function")))
#else
#define ASTIA_NO_SANITIZE_FUNCTION
#endif

namespace {

    struct loader_state {
        unsigned mapped          = 0;
        unsigned unmapped        = 0;
        bool     resolve_enabled = true;
    };

    struct observation {
        astia::mod::loaded_module const* current = nullptr;
        unsigned                         events[8]{};
        unsigned                         event_count      = 0;
        bool                             callbacks_pinned = true;
    };

    observation* active_observation = nullptr;

    void lifecycle_record(int event);

    void* map_image(void* context, astia::u64, astia::usize size, astia::usize,
                    bool fixed) {
        if (fixed) {
            return nullptr;
        }
        auto* state = static_cast<loader_state*>(context);
        void* image = mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (image == MAP_FAILED) {
            return nullptr;
        }
        ++state->mapped;
        return image;
    }

    void unmap_image(void* context, void* image, astia::usize size) {
        auto* state = static_cast<loader_state*>(context);
        munmap(image, size);
        ++state->unmapped;
    }

    astia::u64 resolve_image(void* context, const char* name) {
        auto* state = static_cast<loader_state*>(context);
        if (!state->resolve_enabled || std::strcmp(name, "lifecycle_record") != 0) {
            return 0;
        }
        return reinterpret_cast<std::uintptr_t>(&lifecycle_record);
    }

    void lifecycle_record(int event) {
        if (active_observation == nullptr) {
            return;
        }
        active_observation->events[active_observation->event_count++] =
            static_cast<unsigned>(event);
        if (active_observation->current != nullptr) {
            active_observation->callbacks_pinned =
                active_observation->callbacks_pinned && astia::mod::count() == 1 &&
                astia::mod::at(0) == active_observation->current &&
                active_observation->current->running;
        }
    }

    ASTIA_NO_SANITIZE_FUNCTION int invoke_entry(int (*entry)()) {
        return entry();
    }

    ASTIA_NO_SANITIZE_FUNCTION void invoke_initializer(astia::elf::init_fn initializer) {
        initializer();
    }

    ASTIA_NO_SANITIZE_FUNCTION void invoke_finalizer(astia::elf::init_fn finalizer) {
        finalizer();
    }

    astia::elf::env environment(loader_state& state) {
        astia::elf::env env{};
        env.ctx     = &state;
        env.map     = map_image;
        env.unmap   = unmap_image;
        env.resolve = resolve_image;
        return env;
    }

    astia::span<const astia::u8> blob(unsigned char* data, unsigned length) {
        return {data, length};
    }

}

namespace astia::mod::host_test {

    int invoke_entry(int (*entry)()) {
        return ::invoke_entry(entry);
    }

    void invoke_initializer(elf::init_fn initializer) {
        ::invoke_initializer(initializer);
    }

    void invoke_finalizer(elf::init_fn finalizer) {
        ::invoke_finalizer(finalizer);
    }

}

ASTIA_TEST(module_lifecycle_runs_loaded_image_and_unloads_after_exit) {
    loader_state state{};
    observation  observation{};
    active_observation                      = &observation;
    const astia::elf::env            env    = environment(state);
    const astia::usize               before = astia::mod::count();
    const astia::mod::loaded_module* module = astia::mod::load_module(
        blob(module_lifecycle_blob, module_lifecycle_blob_len), env);

    ASTIA_CHECK(module != nullptr);
    if (module == nullptr) {
        active_observation = nullptr;
        return;
    }
    observation.current = module;
    ASTIA_CHECK(module->init_count == 2);
    ASTIA_CHECK(module->fini_count == 2);
    ASTIA_CHECK(!module->running);
    ASTIA_CHECK(!module->initialized);
    ASTIA_CHECK(observation.event_count == 0);
    ASTIA_CHECK(astia::mod::run_module(module) == 37);
    ASTIA_CHECK(observation.event_count == 3);
    ASTIA_CHECK(observation.events[0] == 101);
    ASTIA_CHECK(observation.events[1] == 102);
    ASTIA_CHECK(observation.events[2] == 103);
    ASTIA_CHECK(module->running);
    ASTIA_CHECK(module->initialized);
    ASTIA_CHECK(astia::mod::run_module(module) == -1);
    ASTIA_CHECK(!astia::mod::unload_module(module));
    ASTIA_CHECK(state.unmapped == 0);
    ASTIA_CHECK(astia::mod::exit_module(module));
    ASTIA_CHECK(observation.event_count == 5);
    ASTIA_CHECK(observation.events[3] == 202);
    ASTIA_CHECK(observation.events[4] == 201);
    ASTIA_CHECK(observation.callbacks_pinned);
    ASTIA_CHECK(!module->running);
    ASTIA_CHECK(!module->initialized);
    ASTIA_CHECK(!astia::mod::exit_module(module));
    ASTIA_CHECK(astia::mod::unload_module(module));
    ASTIA_CHECK(astia::mod::count() == before);
    ASTIA_CHECK(state.mapped == 1);
    ASTIA_CHECK(state.unmapped == 1);
    active_observation = nullptr;
}

ASTIA_TEST(module_lifecycle_allows_optional_exit_array) {
    loader_state                     state{};
    const astia::elf::env            env    = environment(state);
    const astia::mod::loaded_module* module = astia::mod::load_module(
        blob(module_lifecycle_no_fini_blob, module_lifecycle_no_fini_blob_len), env);

    ASTIA_CHECK(module != nullptr);
    if (module == nullptr) {
        return;
    }
    ASTIA_CHECK(module->init_count == 0);
    ASTIA_CHECK(module->fini_count == 0);
    ASTIA_CHECK(astia::mod::run_module(module) == 17);
    ASTIA_CHECK(astia::mod::exit_module(module));
    ASTIA_CHECK(astia::mod::unload_module(module));
    ASTIA_CHECK(state.unmapped == 1);
}

ASTIA_TEST(module_lifecycle_sorts_compiler_priority_sections) {
    loader_state state{};
    observation  observation{};
    active_observation                      = &observation;
    const astia::elf::env            env    = environment(state);
    const astia::mod::loaded_module* module = astia::mod::load_module(
        blob(module_lifecycle_priority_blob, module_lifecycle_priority_blob_len), env);

    ASTIA_CHECK(module != nullptr);
    if (module == nullptr) {
        active_observation = nullptr;
        return;
    }
    observation.current = module;
    ASTIA_CHECK(module->init_count == 3);
    ASTIA_CHECK(module->fini_count == 3);
    ASTIA_CHECK(astia::mod::run_module(module) == 47);
    ASTIA_CHECK(observation.event_count == 4);
    ASTIA_CHECK(observation.events[0] == 101);
    ASTIA_CHECK(observation.events[1] == 102);
    ASTIA_CHECK(observation.events[2] == 199);
    ASTIA_CHECK(observation.events[3] == 103);
    ASTIA_CHECK(astia::mod::exit_module(module));
    ASTIA_CHECK(observation.event_count == 7);
    ASTIA_CHECK(observation.events[4] == 299);
    ASTIA_CHECK(observation.events[5] == 202);
    ASTIA_CHECK(observation.events[6] == 201);
    ASTIA_CHECK(observation.callbacks_pinned);
    ASTIA_CHECK(astia::mod::unload_module(module));
    ASTIA_CHECK(state.mapped == 1);
    ASTIA_CHECK(state.unmapped == 1);
    active_observation = nullptr;
}

ASTIA_TEST(module_lifecycle_rolls_back_failed_load) {
    loader_state state{};
    state.resolve_enabled        = false;
    const astia::elf::env env    = environment(state);
    const astia::usize    before = astia::mod::count();

    ASTIA_CHECK(
        astia::mod::load_module(blob(module_lifecycle_blob, module_lifecycle_blob_len),
                                env) == nullptr);
    ASTIA_CHECK(astia::mod::count() == before);
    ASTIA_CHECK(state.mapped == 1);
    ASTIA_CHECK(state.unmapped == 1);
}
