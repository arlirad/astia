#include <astia/arch/cpu.hpp>
#include <astia/core/resume_queue.hpp>
#include <astia/dev/capabilities.hpp>
#include <astia/module.hpp>
#include <astia/resource_limits.hpp>

#include "../src/mod/module.hpp"
#include "support/interrupts_host.hpp"
#include "support/task_host.hpp"
#include "support/test.hpp"
#include <sys/mman.h>

#include <atomic>
#include <coroutine>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

extern unsigned char module_lifecycle_blob[];
extern unsigned int  module_lifecycle_blob_len;
extern unsigned char module_lifecycle_atexit_blob[];
extern unsigned int  module_lifecycle_atexit_blob_len;
extern unsigned char module_lifecycle_limit_blob[];
extern unsigned int  module_lifecycle_limit_blob_len;
extern unsigned char module_lifecycle_dso_blob[];
extern unsigned int  module_lifecycle_dso_blob_len;

extern "C" int       astia_test_cxa_atexit(void (*)(void*), void*, void*);
extern "C" void      astia_test_cxa_finalize(void*);
extern "C" int       astia_test_atexit(void (*)(void));
extern unsigned char module_lifecycle_no_fini_blob[];
extern unsigned int  module_lifecycle_no_fini_blob_len;
extern unsigned char module_lifecycle_priority_blob[];
extern unsigned int  module_lifecycle_priority_blob_len;
extern unsigned char module_lifecycle_wait_blob[];
extern unsigned int  module_lifecycle_wait_blob_len;
extern unsigned char module_lifecycle_reentrant_blob[];
extern unsigned int  module_lifecycle_reentrant_blob_len;
extern unsigned char module_lifecycle_module_api_blob[];
extern unsigned int  module_lifecycle_module_api_blob_len;
extern unsigned char module_lifecycle_reloc32_blob[];
extern unsigned int  module_lifecycle_reloc32_blob_len;

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
        void*    dso             = nullptr;
    };

    struct observation {
        astia::mod::loaded_module const* current = nullptr;
        unsigned                         events[128]{};
        unsigned                         event_count      = 0;
        bool                             callbacks_pinned = true;
    };

    observation* active_observation = nullptr;

    class owner_test_info final : public astia::hw::device_info {
        public:
        explicit owner_test_info(astia::u32 value) : value_(value) {}

        astia::hw::bus_kind bus() const override {
            return astia::hw::bus_kind::unknown;
        }

        bool equals(const astia::hw::device_info& other) const override {
            return value_ == static_cast<const owner_test_info&>(other).value_;
        }

        private:
        astia::u32 value_;
    };

    class owner_test_driver final : public astia::hw::driver {
        public:
        const char* name() const override {
            return "module-owner-test";
        }

        bool match(const astia::hw::device_info&) const override {
            return true;
        }

        astia::core::task<astia::i64> probe(const astia::hw::device_info&) override {
            co_return 0;
        }

        astia::core::task<astia::i64> start(const astia::hw::device_info&) override {
            co_return 0;
        }

        astia::core::task<void> stop(const astia::hw::device_info&) override {
            ++stop_calls;
            co_return astia::error_t::enone;
        }

        astia::core::task<void> remove(const astia::hw::device_info&) override {
            ++remove_calls;
            co_return astia::error_t::enone;
        }

        int stop_calls   = 0;
        int remove_calls = 0;
    };

    astia::dev::irq_route lifecycle_route{1};

    int  lifecycle_api_mode              = 0;
    bool lifecycle_claim_result          = false;
    bool lifecycle_release_result        = false;
    bool lifecycle_resident_result       = false;
    bool lifecycle_resident_in_interrupt = true;
    bool lifecycle_claim_in_interrupt    = true;
    bool lifecycle_handler_ran           = false;

    void lifecycle_irq_entry() {
        lifecycle_handler_ran = true;
    }

    int lifecycle_module_api_probe() {
        if (lifecycle_api_mode == 0) {
            lifecycle_resident_result = astia::module::make_resident();
        }
        else if (lifecycle_api_mode == 1) {
            lifecycle_claim_result = lifecycle_route.claim(lifecycle_irq_entry);
        }
        else if (lifecycle_api_mode == 2) {
            lifecycle_claim_result   = lifecycle_route.claim(lifecycle_irq_entry);
            lifecycle_release_result = lifecycle_route.release();
        }

        astia::arch::cpu::interrupt_enter();

        lifecycle_resident_in_interrupt = astia::module::make_resident();
        lifecycle_claim_in_interrupt    = lifecycle_route.claim(lifecycle_irq_entry);

        astia::arch::cpu::interrupt_exit();

        return 61;
    }

    std::atomic<bool>     lifecycle_wait_started{false};
    std::atomic<bool>     lifecycle_wait_release{false};
    std::atomic<bool>     lifecycle_outer_entered{false};
    std::atomic<bool>     lifecycle_outer_release{false};
    std::atomic<unsigned> lifecycle_outer_count{0};
    std::atomic<unsigned> lifecycle_inner_count{0};
    std::atomic<unsigned> lifecycle_external_count{0};

    void lifecycle_reentrant_inner_handler() {
        lifecycle_inner_count.fetch_add(1, std::memory_order_acq_rel);
    }

    void lifecycle_external_handler() {
        lifecycle_external_count.fetch_add(1, std::memory_order_acq_rel);
    }

    void lifecycle_reentrant_outer_handler() {
        lifecycle_outer_count.fetch_add(1, std::memory_order_acq_rel);
        astia_test_atexit(lifecycle_reentrant_inner_handler);
        astia_test_cxa_finalize(nullptr);
        lifecycle_outer_entered.store(true, std::memory_order_release);

        while (!lifecycle_outer_release.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    }

    void lifecycle_wait_for_release() {
        lifecycle_wait_started.store(true, std::memory_order_release);

        while (!lifecycle_wait_release.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    }

    void lifecycle_record(int event);

    void* map_image(void* context, astia::u64, astia::usize size, astia::usize,
                    bool fixed) {
        if (fixed)
            return nullptr;

        auto* state = static_cast<loader_state*>(context);
        void* image = mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (image == MAP_FAILED)
            return nullptr;

        ++state->mapped;

        state->dso = image;

        return image;
    }

    void unmap_image(void* context, void* image, astia::usize size) {
        auto* state = static_cast<loader_state*>(context);

        munmap(image, size);
        ++state->unmapped;
    }

    astia::u64 resolve_image(void* context, const char* name) {
        auto* state = static_cast<loader_state*>(context);

        if (!state->resolve_enabled)
            return 0;

        if (std::strcmp(name, "__dso_handle") == 0)
            return reinterpret_cast<std::uintptr_t>(state->dso);

        if (std::strcmp(name, "lifecycle_record") == 0)
            return reinterpret_cast<std::uintptr_t>(&lifecycle_record);

        if (std::strcmp(name, "lifecycle_wait_for_release") == 0)
            return reinterpret_cast<std::uintptr_t>(&lifecycle_wait_for_release);

        if (std::strcmp(name, "lifecycle_module_api_probe") == 0)
            return reinterpret_cast<std::uintptr_t>(&lifecycle_module_api_probe);

        if (std::strcmp(name, "lifecycle_reentrant_outer_handler") == 0)
            return reinterpret_cast<std::uintptr_t>(&lifecycle_reentrant_outer_handler);

        if (std::strcmp(name, "atexit") == 0)
            return reinterpret_cast<std::uintptr_t>(&astia_test_atexit);

        if (std::strcmp(name, "__cxa_atexit") == 0)
            return reinterpret_cast<std::uintptr_t>(&astia_test_cxa_atexit);

        if (std::strcmp(name, "__cxa_finalize") == 0)
            return reinterpret_cast<std::uintptr_t>(&astia_test_cxa_finalize);

        return 0;
    }

    void lifecycle_record(int event) {
        if (active_observation == nullptr)
            return;

        active_observation->events[active_observation->event_count++] =
            static_cast<unsigned>(event);

        if (active_observation->current != nullptr)
            active_observation->callbacks_pinned =
                active_observation->callbacks_pinned && astia::mod::count() == 1 &&
                astia::mod::at(0) == active_observation->current &&
                active_observation->current->running;
    }

    ASTIA_NO_SANITIZE_FUNCTION int invoke_entry(int (*entry)()) {
        return entry();
    }

    ASTIA_NO_SANITIZE_FUNCTION void
    invoke_initializer(astia::elf::loader::init_fn initializer) {
        initializer();
    }

    ASTIA_NO_SANITIZE_FUNCTION void
    invoke_finalizer(astia::elf::loader::init_fn finalizer) {
        finalizer();
    }

    astia::elf::loader::env environment(loader_state& state) {
        astia::elf::loader::env env{};

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

    void invoke_initializer(elf::loader::init_fn initializer) {
        ::invoke_initializer(initializer);
    }

    void invoke_finalizer(elf::loader::init_fn finalizer) {
        ::invoke_finalizer(finalizer);
    }
}

ASTIA_TEST(module_lifecycle_runs_loaded_image_and_unloads_after_exit) {
    loader_state state{};
    observation  observation{};

    active_observation = &observation;

    const astia::elf::loader::env    env    = environment(state);
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

ASTIA_TEST(module_lifecycle_runs_atexit_handlers_before_fini) {
    loader_state state{};
    observation  observation{};

    active_observation = &observation;

    const astia::elf::loader::env    env    = environment(state);
    const astia::mod::loaded_module* module = astia::mod::load_module(
        blob(module_lifecycle_atexit_blob, module_lifecycle_atexit_blob_len), env);

    ASTIA_CHECK(module != nullptr);

    if (module == nullptr) {
        active_observation = nullptr;

        return;
    }

    observation.current = module;

    ASTIA_CHECK(astia::mod::run_module(module) == 38);
    ASTIA_CHECK(observation.event_count == 1);
    ASTIA_CHECK(observation.events[0] == 300);
    ASTIA_CHECK(astia::mod::exit_module(module));
    ASTIA_CHECK(observation.event_count == 5);
    ASTIA_CHECK(observation.events[1] == 304);
    ASTIA_CHECK(observation.events[2] == 302);
    ASTIA_CHECK(observation.events[3] == 303);
    ASTIA_CHECK(observation.events[4] == 301);
    ASTIA_CHECK(astia::mod::unload_module(module));

    active_observation = nullptr;
}

ASTIA_TEST(module_lifecycle_enforces_atexit_capacity) {
    loader_state state{};
    observation  observation{};

    active_observation = &observation;

    const astia::elf::loader::env    env    = environment(state);
    const astia::mod::loaded_module* module = astia::mod::load_module(
        blob(module_lifecycle_limit_blob, module_lifecycle_limit_blob_len), env);

    ASTIA_CHECK(module != nullptr);

    if (module == nullptr) {
        active_observation = nullptr;

        return;
    }

    observation.current = module;

    const int limit_result = astia::mod::run_module(module);

    ASTIA_CHECK(limit_result == 0);
    ASTIA_CHECK(observation.event_count == 1);
    ASTIA_CHECK(observation.events[0] == 400);
    ASTIA_CHECK(astia::mod::exit_module(module));
    ASTIA_CHECK(astia::mod::unload_module(module));

    active_observation = nullptr;
}

ASTIA_TEST(module_lifecycle_filters_external_dso_finalize) {
    loader_state state{};
    observation  observation{};

    active_observation = &observation;

    const astia::elf::loader::env    env    = environment(state);
    const astia::mod::loaded_module* module = astia::mod::load_module(
        blob(module_lifecycle_dso_blob, module_lifecycle_dso_blob_len), env);

    ASTIA_CHECK(module != nullptr);

    if (module == nullptr) {
        active_observation = nullptr;

        return;
    }

    const astia::mod::loaded_module* second = astia::mod::load_module(
        blob(module_lifecycle_no_fini_blob, module_lifecycle_no_fini_blob_len), env);

    ASTIA_CHECK(second != nullptr);

    if (second != nullptr) {
        ASTIA_CHECK(module->dso_identity != nullptr);
        ASTIA_CHECK(module->dso_identity != second->dso_identity);
        ASTIA_CHECK(astia::mod::unload_module(second));
    }

    observation.current = module;

    ASTIA_CHECK(astia::mod::run_module(module) == 0);
    ASTIA_CHECK(observation.event_count == 2);
    ASTIA_CHECK(observation.events[0] == 411);
    ASTIA_CHECK(observation.events[1] == 410);
    ASTIA_CHECK(astia::mod::exit_module(module));
    ASTIA_CHECK(observation.event_count == 4);
    ASTIA_CHECK(observation.events[2] == 413);
    ASTIA_CHECK(observation.events[3] == 412);
    ASTIA_CHECK(astia::mod::unload_module(module));

    active_observation = nullptr;
}

ASTIA_TEST(module_lifecycle_allows_optional_exit_array) {
    loader_state                     state{};
    const astia::elf::loader::env    env    = environment(state);
    const astia::mod::loaded_module* module = astia::mod::load_module(
        blob(module_lifecycle_no_fini_blob, module_lifecycle_no_fini_blob_len), env);

    ASTIA_CHECK(module != nullptr);

    if (module == nullptr)
        return;

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

    active_observation = &observation;

    const astia::elf::loader::env    env    = environment(state);
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

    state.resolve_enabled = false;

    const astia::elf::loader::env env    = environment(state);
    const astia::usize            before = astia::mod::count();

    ASTIA_CHECK(
        astia::mod::load_module(blob(module_lifecycle_blob, module_lifecycle_blob_len),
                                env) == nullptr);
    ASTIA_CHECK(astia::mod::count() == before);
    ASTIA_CHECK(state.mapped == 1);
    ASTIA_CHECK(state.unmapped == 1);
}

ASTIA_TEST(module_lifecycle_async_open_publishes_after_complete_load) {
    loader_state                  state{};
    const astia::elf::loader::env env     = environment(state);
    auto                          opening = astia::mod::open(
        blob(module_lifecycle_no_fini_blob, module_lifecycle_no_fini_blob_len), env);

    ASTIA_CHECK(!opening.done());
    opening.resume();
    ASTIA_CHECK(opening.done());

    const astia::mod::module_open_result* result = opening.value();

    ASTIA_CHECK(result != nullptr);

    if (result == nullptr || !result->ok())
        return;

    astia::mod::module_info info{};

    ASTIA_CHECK(astia::mod::lookup(result->id, info) == astia::mod::module_error::ok);
    ASTIA_CHECK(info.state == astia::mod::module_state::loaded);
    ASTIA_CHECK(!info.running);
    ASTIA_CHECK(info.image_size != 0);
    ASTIA_CHECK(info.unloadable);
    ASTIA_CHECK(astia::mod::unload(result->id) == astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::lookup(result->id, info) ==
                astia::mod::module_error::stale_id);
    ASTIA_CHECK(state.unmapped == 1);
}

ASTIA_TEST(module_lifecycle_rolls_back_unpublished_dependency_failure) {
    loader_state                    state{};
    const astia::elf::loader::env   env = environment(state);
    astia::mod::module_id           stale{};
    astia::mod::module_load_options options{};
    const astia::usize              before = astia::mod::count();

    ASTIA_CHECK(astia::mod::load_image(blob(module_lifecycle_no_fini_blob,
                                            module_lifecycle_no_fini_blob_len),
                                       env, {}, stale) == astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::unload(stale) == astia::mod::module_error::ok);
    ASTIA_CHECK(state.mapped == 1);
    ASTIA_CHECK(state.unmapped == 1);

    options.dependencies = astia::span<const astia::mod::module_id>(&stale, 1);

    astia::mod::module_id unpublished{};

    ASTIA_CHECK(astia::mod::load_image(blob(module_lifecycle_no_fini_blob,
                                            module_lifecycle_no_fini_blob_len),
                                       env, options, unpublished) ==
                astia::mod::module_error::stale_id);
    ASTIA_CHECK(!unpublished.valid());
    ASTIA_CHECK(astia::mod::count() == before);
    ASTIA_CHECK(state.mapped == 2);
    ASTIA_CHECK(state.unmapped == 2);
}

ASTIA_TEST(module_lifecycle_rejects_stale_ids_after_slot_reuse) {
    loader_state                  state{};
    const astia::elf::loader::env env = environment(state);
    astia::mod::module_id         first{};
    astia::mod::module_id         second{};

    ASTIA_CHECK(astia::mod::load_image(blob(module_lifecycle_no_fini_blob,
                                            module_lifecycle_no_fini_blob_len),
                                       env, {}, first) == astia::mod::module_error::ok);
    ASTIA_CHECK(first.valid());
    ASTIA_CHECK(astia::mod::unload(first) == astia::mod::module_error::ok);

    astia::mod::module_info info{};

    ASTIA_CHECK(astia::mod::lookup(first, info) == astia::mod::module_error::stale_id);

    ASTIA_CHECK(astia::mod::load_image(blob(module_lifecycle_no_fini_blob,
                                            module_lifecycle_no_fini_blob_len),
                                       env, {}, second) == astia::mod::module_error::ok);
    ASTIA_CHECK(second.valid());
    ASTIA_CHECK(second.slot == first.slot);
    ASTIA_CHECK(second.generation != first.generation);
    ASTIA_CHECK(astia::mod::lookup(first, info) == astia::mod::module_error::stale_id);
    ASTIA_CHECK(astia::mod::acquire(first) == astia::mod::module_error::stale_id);
    ASTIA_CHECK(astia::mod::release(first) == astia::mod::module_error::stale_id);
    ASTIA_CHECK(astia::mod::unload(first) == astia::mod::module_error::stale_id);
    ASTIA_CHECK(astia::mod::add_dependency(first, second) ==
                astia::mod::module_error::stale_id);
    ASTIA_CHECK(astia::mod::unload(second) == astia::mod::module_error::ok);
}

ASTIA_TEST(module_lifecycle_resident_modules_are_not_unloadable) {
    loader_state                    state{};
    const astia::elf::loader::env   env = environment(state);
    astia::mod::module_load_options options{};

    options.resident = true;

    astia::mod::module_id id{};

    ASTIA_CHECK(astia::mod::load_image(blob(module_lifecycle_no_fini_blob,
                                            module_lifecycle_no_fini_blob_len),
                                       env, options, id) == astia::mod::module_error::ok);

    int result = 0;

    ASTIA_CHECK(astia::mod::run(id, result) == astia::mod::module_error::ok);
    ASTIA_CHECK(result == 17);

    astia::mod::module_info info{};

    ASTIA_CHECK(astia::mod::lookup(id, info) == astia::mod::module_error::ok);
    ASTIA_CHECK(info.resident);
    ASTIA_CHECK(!info.unloadable);
    ASTIA_CHECK(astia::mod::unload(id) == astia::mod::module_error::resident);

    auto closing = astia::mod::close(id);

    closing.resume();
    ASTIA_CHECK(closing.done());
    ASTIA_CHECK(closing.value() != nullptr &&
                *closing.value() == astia::mod::module_error::resident);

    ASTIA_CHECK(astia::mod::exit(id) == astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::unload(id) == astia::mod::module_error::resident);
    ASTIA_CHECK(astia::mod::make_resident(id) == astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::unload(id) == astia::mod::module_error::resident);
    ASTIA_CHECK(astia::mod::clear_resident_for_test(id) == astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::unload(id) == astia::mod::module_error::ok);
}

ASTIA_TEST(module_lifecycle_dependencies_block_unload_and_reject_cycles) {
    loader_state                  state{};
    const astia::elf::loader::env env = environment(state);
    astia::mod::module_id         dependent{};
    astia::mod::module_id         provider{};

    ASTIA_CHECK(astia::mod::load_image(blob(module_lifecycle_no_fini_blob,
                                            module_lifecycle_no_fini_blob_len),
                                       env, {},
                                       dependent) == astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::load_image(blob(module_lifecycle_no_fini_blob,
                                            module_lifecycle_no_fini_blob_len),
                                       env, {},
                                       provider) == astia::mod::module_error::ok);

    ASTIA_CHECK(astia::mod::add_dependency(dependent, provider) ==
                astia::mod::module_error::ok);

    astia::mod::module_info info{};

    ASTIA_CHECK(astia::mod::lookup(provider, info) == astia::mod::module_error::ok);
    ASTIA_CHECK(info.dependent_count == 1);
    ASTIA_CHECK(!info.unloadable);
    ASTIA_CHECK(astia::mod::lookup(dependent, info) == astia::mod::module_error::ok);
    ASTIA_CHECK(info.dependency_count == 1);

    astia::mod::module_id listed{};

    ASTIA_CHECK(astia::mod::dependency_at(dependent, 0, listed) ==
                astia::mod::module_error::ok);
    ASTIA_CHECK(listed == provider);
    ASTIA_CHECK(astia::mod::dependency_at(dependent, 1, listed) ==
                astia::mod::module_error::not_found);

    ASTIA_CHECK(astia::mod::add_dependency(provider, dependent) ==
                astia::mod::module_error::cycle);
    ASTIA_CHECK(astia::mod::add_dependency(dependent, dependent) ==
                astia::mod::module_error::invalid_argument);
    ASTIA_CHECK(astia::mod::unload(provider) == astia::mod::module_error::busy);

    ASTIA_CHECK(astia::mod::unload(dependent) == astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::unload(provider) == astia::mod::module_error::ok);
}

ASTIA_TEST(module_lifecycle_close_awaits_active_reference_drain) {
    loader_state                  state{};
    const astia::elf::loader::env env = environment(state);
    astia::mod::module_id         id{};

    ASTIA_CHECK(astia::mod::load_image(blob(module_lifecycle_no_fini_blob,
                                            module_lifecycle_no_fini_blob_len),
                                       env, {}, id) == astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::acquire(id) == astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::acquire(id) == astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::unload(id) == astia::mod::module_error::busy);

    astia::mod::module_info info{};

    ASTIA_CHECK(astia::mod::lookup(id, info) == astia::mod::module_error::ok);
    ASTIA_CHECK(info.active_refs == 2);

    auto closing = astia::mod::close(id);

    closing.resume();
    ASTIA_CHECK(!closing.done());
    ASTIA_CHECK(astia::mod::lookup(id, info) == astia::mod::module_error::ok);
    ASTIA_CHECK(info.quiescing);

    auto concurrent = astia::mod::close(id);

    concurrent.resume();
    ASTIA_CHECK(concurrent.done());
    ASTIA_CHECK(concurrent.value() != nullptr &&
                *concurrent.value() == astia::mod::module_error::busy);
    ASTIA_CHECK(astia::mod::acquire(id) == astia::mod::module_error::would_block);
    ASTIA_CHECK(astia::mod::release(id) == astia::mod::module_error::ok);
    ASTIA_CHECK(!closing.done());
    ASTIA_CHECK(astia::core::drain_resumes() == 0);
    ASTIA_CHECK(astia::mod::release(id) == astia::mod::module_error::ok);
    ASTIA_CHECK(!closing.done());
    ASTIA_CHECK(astia::core::drain_resumes() == 1);
    ASTIA_CHECK(closing.done());
    ASTIA_CHECK(closing.value() != nullptr &&
                *closing.value() == astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::lookup(id, info) == astia::mod::module_error::stale_id);
    ASTIA_CHECK(state.unmapped == 1);
}

ASTIA_TEST(module_lifecycle_close_awaits_dependent_detach) {
    loader_state                  state{};
    const astia::elf::loader::env env = environment(state);
    astia::mod::module_id         dependent{};
    astia::mod::module_id         provider{};

    ASTIA_CHECK(astia::mod::load_image(blob(module_lifecycle_no_fini_blob,
                                            module_lifecycle_no_fini_blob_len),
                                       env, {},
                                       dependent) == astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::load_image(blob(module_lifecycle_no_fini_blob,
                                            module_lifecycle_no_fini_blob_len),
                                       env, {},
                                       provider) == astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::add_dependency(dependent, provider) ==
                astia::mod::module_error::ok);

    auto closing = astia::mod::close(provider);

    closing.resume();
    ASTIA_CHECK(!closing.done());
    ASTIA_CHECK(astia::mod::remove_dependency(dependent, provider) ==
                astia::mod::module_error::ok);
    ASTIA_CHECK(!closing.done());
    ASTIA_CHECK(astia::core::drain_resumes() == 1);
    ASTIA_CHECK(closing.done());
    ASTIA_CHECK(closing.value() != nullptr &&
                *closing.value() == astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::unload(dependent) == astia::mod::module_error::ok);
}

ASTIA_TEST(module_lifecycle_cancel_close_reopens_module) {
    loader_state                  state{};
    const astia::elf::loader::env env = environment(state);
    astia::mod::module_id         id{};

    ASTIA_CHECK(astia::mod::load_image(blob(module_lifecycle_no_fini_blob,
                                            module_lifecycle_no_fini_blob_len),
                                       env, {}, id) == astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::acquire(id) == astia::mod::module_error::ok);

    auto closing = astia::mod::close(id);

    closing.resume();
    ASTIA_CHECK(!closing.done());
    ASTIA_CHECK(astia::mod::cancel_close(id) == astia::mod::module_error::ok);
    ASTIA_CHECK(!closing.done());
    ASTIA_CHECK(astia::core::drain_resumes() == 1);
    ASTIA_CHECK(closing.done());
    ASTIA_CHECK(closing.value() != nullptr &&
                *closing.value() == astia::mod::module_error::canceled);
    ASTIA_CHECK(astia::mod::acquire(id) == astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::release(id) == astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::release(id) == astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::unload(id) == astia::mod::module_error::ok);
}

ASTIA_TEST(module_lifecycle_destroying_parked_close_reopens_module) {
    loader_state                  state{};
    const astia::elf::loader::env env = environment(state);
    astia::mod::module_id         id{};

    ASTIA_CHECK(astia::mod::load_image(blob(module_lifecycle_no_fini_blob,
                                            module_lifecycle_no_fini_blob_len),
                                       env, {}, id) == astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::acquire(id) == astia::mod::module_error::ok);

    {
        auto closing = astia::mod::close(id);

        closing.resume();
        ASTIA_CHECK(!closing.done());
    }

    astia::mod::module_info info{};

    ASTIA_CHECK(astia::mod::lookup(id, info) == astia::mod::module_error::ok);
    ASTIA_CHECK(!info.quiescing);
    ASTIA_CHECK(astia::mod::acquire(id) == astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::release(id) == astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::release(id) == astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::unload(id) == astia::mod::module_error::ok);
}

ASTIA_TEST(module_lifecycle_snapshot_is_bounded_and_consistent) {
    loader_state                  state{};
    const astia::elf::loader::env env = environment(state);
    astia::mod::module_id         first{};
    astia::mod::module_id         second{};

    ASTIA_CHECK(astia::mod::load_image(blob(module_lifecycle_no_fini_blob,
                                            module_lifecycle_no_fini_blob_len),
                                       env, {}, first) == astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::load_image(blob(module_lifecycle_no_fini_blob,
                                            module_lifecycle_no_fini_blob_len),
                                       env, {}, second) == astia::mod::module_error::ok);

    const astia::usize total = astia::mod::count();

    ASTIA_CHECK(total >= 2);

    astia::mod::module_info bounded[1]{};

    ASTIA_CHECK(astia::mod::snapshot(astia::span<astia::mod::module_info>(bounded, 1)) ==
                1);
    ASTIA_CHECK(bounded[0].id.valid());

    astia::mod::module_info all[astia::mod::max_modules]{};
    const astia::usize      written = astia::mod::snapshot(
        astia::span<astia::mod::module_info>(all, astia::mod::max_modules));

    ASTIA_CHECK(written == total);

    bool saw_first  = false;
    bool saw_second = false;

    for (astia::usize i = 0; i < written; ++i) {
        if (all[i].id == first)
            saw_first = true;

        if (all[i].id == second)
            saw_second = true;
    }

    ASTIA_CHECK(saw_first);
    ASTIA_CHECK(saw_second);

    ASTIA_CHECK(astia::mod::unload(first) == astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::unload(second) == astia::mod::module_error::ok);
}

ASTIA_TEST(module_lifecycle_open_captures_temporary_env_and_options) {
    loader_state          state{};
    astia::mod::module_id provider{};

    ASTIA_CHECK(astia::mod::load_image(blob(module_lifecycle_no_fini_blob,
                                            module_lifecycle_no_fini_blob_len),
                                       environment(state), {},
                                       provider) == astia::mod::module_error::ok);

    astia::core::task<astia::mod::module_open_result> pending;

    {
        const astia::elf::loader::env   env             = environment(state);
        astia::mod::module_id           dependencies[1] = {provider};
        astia::mod::module_load_options options{};

        options.dependencies = astia::span<const astia::mod::module_id>(dependencies, 1);
        pending              = astia::mod::open(
            blob(module_lifecycle_no_fini_blob, module_lifecycle_no_fini_blob_len), env,
            options);
    }

    ASTIA_CHECK(!pending.done());
    pending.resume();
    ASTIA_CHECK(pending.done());

    const astia::mod::module_open_result* result = pending.value();

    ASTIA_CHECK(result != nullptr && result->ok());

    if (result == nullptr || !result->ok())
        return;

    astia::mod::module_id listed{};

    ASTIA_CHECK(astia::mod::dependency_at(result->id, 0, listed) ==
                astia::mod::module_error::ok);
    ASTIA_CHECK(listed == provider);
    ASTIA_CHECK(astia::mod::unload(result->id) == astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::unload(provider) == astia::mod::module_error::ok);
}

ASTIA_TEST(module_lifecycle_unload_cannot_bypass_pending_close) {
    loader_state                  state{};
    const astia::elf::loader::env env = environment(state);
    astia::mod::module_id         id{};

    ASTIA_CHECK(astia::mod::load_image(blob(module_lifecycle_no_fini_blob,
                                            module_lifecycle_no_fini_blob_len),
                                       env, {}, id) == astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::acquire(id) == astia::mod::module_error::ok);

    auto closing = astia::mod::close(id);

    closing.resume();
    ASTIA_CHECK(!closing.done());

    ASTIA_CHECK(astia::mod::release(id) == astia::mod::module_error::ok);

    astia::mod::module_info pending{};

    ASTIA_CHECK(astia::mod::lookup(id, pending) == astia::mod::module_error::ok);
    ASTIA_CHECK(pending.quiescing);
    ASTIA_CHECK(pending.active_refs == 0);
    ASTIA_CHECK(!pending.unloadable);
    ASTIA_CHECK(astia::mod::unload(id) == astia::mod::module_error::busy);
    ASTIA_CHECK(!closing.done());
    ASTIA_CHECK(astia::core::drain_resumes() == 1);
    ASTIA_CHECK(closing.done());
    ASTIA_CHECK(closing.value() != nullptr &&
                *closing.value() == astia::mod::module_error::ok);

    astia::mod::module_info info{};

    ASTIA_CHECK(astia::mod::lookup(id, info) == astia::mod::module_error::stale_id);
}

ASTIA_TEST(module_lifecycle_destroying_close_after_wake_is_safe) {
    loader_state                  state{};
    const astia::elf::loader::env env = environment(state);
    astia::mod::module_id         id{};

    ASTIA_CHECK(astia::mod::load_image(blob(module_lifecycle_no_fini_blob,
                                            module_lifecycle_no_fini_blob_len),
                                       env, {}, id) == astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::acquire(id) == astia::mod::module_error::ok);

    {
        auto closing = astia::mod::close(id);

        closing.resume();
        ASTIA_CHECK(!closing.done());
        ASTIA_CHECK(astia::mod::release(id) == astia::mod::module_error::ok);
        ASTIA_CHECK(!closing.done());
    }

    ASTIA_CHECK(astia::core::drain_resumes() == 0);

    astia::mod::module_info info{};

    ASTIA_CHECK(astia::mod::lookup(id, info) == astia::mod::module_error::ok);
    ASTIA_CHECK(!info.quiescing);
    ASTIA_CHECK(astia::mod::acquire(id) == astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::release(id) == astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::unload(id) == astia::mod::module_error::ok);
}

ASTIA_TEST(module_lifecycle_serializes_execution_and_finalization_context) {
    loader_state                  state{};
    const astia::elf::loader::env env = environment(state);
    observation                   observation{};

    active_observation = &observation;

    astia::mod::module_id first{};
    astia::mod::module_id second{};

    ASTIA_CHECK(astia::mod::load_image(
                    blob(module_lifecycle_atexit_blob, module_lifecycle_atexit_blob_len),
                    env, {}, first) == astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::load_image(
                    blob(module_lifecycle_atexit_blob, module_lifecycle_atexit_blob_len),
                    env, {}, second) == astia::mod::module_error::ok);

    std::atomic<bool> failed{false};
    auto              run_one = [&](astia::mod::module_id id) {
        while (true) {
            int result = 0;

            const auto error = astia::mod::run(id, result);

            if (error == astia::mod::module_error::busy) {
                std::this_thread::yield();
                continue;
            }

            if (error != astia::mod::module_error::ok || result != 38)
                failed.store(true, std::memory_order_release);

            return;
        }
    };
    std::thread first_runner(run_one, first);
    std::thread second_runner(run_one, second);

    first_runner.join();
    second_runner.join();
    ASTIA_CHECK(!failed.load(std::memory_order_acquire));
    ASTIA_CHECK(observation.event_count == 2);

    auto exit_one = [&](astia::mod::module_id id) {
        while (true) {
            const auto error = astia::mod::exit(id);

            if (error == astia::mod::module_error::busy) {
                std::this_thread::yield();
                continue;
            }

            if (error != astia::mod::module_error::ok)
                failed.store(true, std::memory_order_release);

            return;
        }
    };
    std::thread first_exiter(exit_one, first);
    std::thread second_exiter(exit_one, second);

    first_exiter.join();
    second_exiter.join();
    ASTIA_CHECK(!failed.load(std::memory_order_acquire));
    ASTIA_CHECK(observation.event_count == 10);

    unsigned counts[500] = {};

    for (unsigned i = 0; i < observation.event_count; ++i) {
        const unsigned event = observation.events[i];
        if (event < 500)
            ++counts[event];
    }

    ASTIA_CHECK(counts[300] == 2);
    ASTIA_CHECK(counts[304] == 2);
    ASTIA_CHECK(counts[302] == 2);
    ASTIA_CHECK(counts[303] == 2);
    ASTIA_CHECK(counts[301] == 2);
    ASTIA_CHECK(astia::mod::unload(first) == astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::unload(second) == astia::mod::module_error::ok);

    active_observation = nullptr;
}

ASTIA_TEST(module_lifecycle_c_abi_ownership_rejects_external_contexts) {
    loader_state                  state{};
    const astia::elf::loader::env env = environment(state);
    astia::mod::module_id         waiting{};
    astia::mod::module_id         reentrant{};

    lifecycle_wait_started.store(false, std::memory_order_release);
    lifecycle_wait_release.store(false, std::memory_order_release);
    lifecycle_outer_entered.store(false, std::memory_order_release);
    lifecycle_outer_release.store(false, std::memory_order_release);
    lifecycle_outer_count.store(0, std::memory_order_release);
    lifecycle_inner_count.store(0, std::memory_order_release);
    lifecycle_external_count.store(0, std::memory_order_release);

    ASTIA_CHECK(astia::mod::load_image(
                    blob(module_lifecycle_wait_blob, module_lifecycle_wait_blob_len), env,
                    {}, waiting) == astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::load_image(blob(module_lifecycle_reentrant_blob,
                                            module_lifecycle_reentrant_blob_len),
                                       env, {},
                                       reentrant) == astia::mod::module_error::ok);

    std::atomic<astia::mod::module_error> wait_error{astia::mod::module_error::ok};
    std::atomic<int>                      wait_result{0};

    std::thread waiter([&]() {
        int result = 0;

        wait_error.store(astia::mod::run(waiting, result), std::memory_order_release);
        wait_result.store(result, std::memory_order_release);
    });

    while (!lifecycle_wait_started.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    ASTIA_CHECK(astia_test_atexit(lifecycle_external_handler) == -1);
    astia_test_cxa_finalize(nullptr);
    ASTIA_CHECK(lifecycle_external_count.load(std::memory_order_acquire) == 0);

    int other_result = 0;

    ASTIA_CHECK(astia::mod::run(reentrant, other_result) ==
                astia::mod::module_error::busy);

    lifecycle_wait_release.store(true, std::memory_order_release);
    waiter.join();
    ASTIA_CHECK(wait_error.load(std::memory_order_acquire) ==
                astia::mod::module_error::ok);
    ASTIA_CHECK(wait_result.load(std::memory_order_acquire) == 19);
    ASTIA_CHECK(astia::mod::exit(waiting) == astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::unload(waiting) == astia::mod::module_error::ok);

    int run_result = -1;

    ASTIA_CHECK(astia::mod::run(reentrant, run_result) == astia::mod::module_error::ok);
    ASTIA_CHECK(run_result == 0);

    std::atomic<astia::mod::module_error> exit_error{astia::mod::module_error::ok};

    std::thread exiter([&]() {
        exit_error.store(astia::mod::exit(reentrant), std::memory_order_release);
    });

    while (!lifecycle_outer_entered.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    auto external_finalizer = []() {
        for (unsigned i = 0; i < 10; ++i) {
            astia_test_cxa_finalize(nullptr);
        }
    };
    std::atomic<bool> external_registration_succeeded{false};
    auto              external_registrar = [&]() {
        for (unsigned i = 0; i < 10; ++i) {
            if (astia_test_atexit(lifecycle_external_handler) != -1)
                external_registration_succeeded.store(true, std::memory_order_release);
        }
    };
    std::thread first_finalizer(external_finalizer);
    std::thread second_finalizer(external_finalizer);
    std::thread first_registrar(external_registrar);
    std::thread second_registrar(external_registrar);

    first_finalizer.join();
    second_finalizer.join();
    first_registrar.join();
    second_registrar.join();
    ASTIA_CHECK(!external_registration_succeeded.load(std::memory_order_acquire));
    ASTIA_CHECK(lifecycle_external_count.load(std::memory_order_acquire) == 0);

    lifecycle_outer_release.store(true, std::memory_order_release);
    exiter.join();
    ASTIA_CHECK(exit_error.load(std::memory_order_acquire) ==
                astia::mod::module_error::ok);
    ASTIA_CHECK(lifecycle_outer_count.load(std::memory_order_acquire) == 1);
    ASTIA_CHECK(lifecycle_inner_count.load(std::memory_order_acquire) == 1);
    ASTIA_CHECK(astia::mod::unload(reentrant) == astia::mod::module_error::ok);
}

ASTIA_TEST(module_lifecycle_concurrent_slot_reuse_is_generation_safe) {
    constexpr unsigned thread_count = 4;
    constexpr unsigned iterations   = 25;

    std::atomic<bool>                  failed{false};
    std::mutex                         id_mutex;
    std::vector<astia::mod::module_id> ids;
    std::vector<std::thread>           workers;

    for (unsigned thread_index = 0; thread_index < thread_count; ++thread_index) {
        workers.emplace_back([&]() {
            loader_state                  state{};
            const astia::elf::loader::env env = environment(state);

            for (unsigned i = 0; i < iterations; ++i) {
                astia::mod::module_id id{};
                const auto            loaded =
                    astia::mod::load_image(blob(module_lifecycle_no_fini_blob,
                                                module_lifecycle_no_fini_blob_len),
                                           env, {}, id);

                if (loaded != astia::mod::module_error::ok ||
                    astia::mod::acquire(id) != astia::mod::module_error::ok) {
                    failed.store(true, std::memory_order_release);

                    return;
                }

                if (astia::mod::release(id) != astia::mod::module_error::ok ||
                    astia::mod::unload(id) != astia::mod::module_error::ok) {
                    failed.store(true, std::memory_order_release);

                    return;
                }

                {
                    std::lock_guard<std::mutex> lock(id_mutex);

                    ids.push_back(id);
                }
            }
        });
    }

    for (std::thread& worker : workers) {
        worker.join();
    }

    ASTIA_CHECK(!failed.load(std::memory_order_acquire));
    ASTIA_CHECK(ids.size() == thread_count * iterations);

    for (const astia::mod::module_id& id : ids) {
        astia::mod::module_info info{};

        ASTIA_CHECK(astia::mod::lookup(id, info) == astia::mod::module_error::stale_id);
    }

    ASTIA_CHECK(astia::mod::count() == 0);
}

ASTIA_TEST(module_lifecycle_rejects_32bit_absolute_relocation_before_mapping) {
    loader_state                  state{};
    const astia::elf::loader::env env    = environment(state);
    const astia::usize            before = astia::mod::count();

    const astia::span<const astia::u8> image =
        blob(module_lifecycle_reloc32_blob, module_lifecycle_reloc32_blob_len);

    ASTIA_CHECK(astia::elf::loader::audit_relocatable(image) ==
                astia::elf::loader::relocatable_problem::incompatible_code_model);

    astia::mod::module_id id{};

    ASTIA_CHECK(astia::mod::load_image(image, env, {}, id) ==
                astia::mod::module_error::load_failed);
    ASTIA_CHECK(state.mapped == 0);
    ASTIA_CHECK(state.unmapped == 0);
    ASTIA_CHECK(astia::mod::count() == before);
}

ASTIA_TEST(module_lifecycle_module_api_rejects_outside_context) {
    loader_state                  state{};
    const astia::elf::loader::env env = environment(state);
    astia::mod::module_id         id{};

    ASTIA_CHECK(!astia::module::make_resident());
    ASTIA_CHECK(!lifecycle_route.claim(lifecycle_irq_entry));

    lifecycle_wait_started.store(false, std::memory_order_release);
    lifecycle_wait_release.store(false, std::memory_order_release);

    ASTIA_CHECK(astia::mod::load_image(
                    blob(module_lifecycle_wait_blob, module_lifecycle_wait_blob_len), env,
                    {}, id) == astia::mod::module_error::ok);

    std::atomic<astia::mod::module_error> run_error{astia::mod::module_error::ok};

    std::thread runner([&]() {
        int result = 0;

        run_error.store(astia::mod::run(id, result), std::memory_order_release);
    });

    while (!lifecycle_wait_started.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    ASTIA_CHECK(!astia::module::make_resident());
    ASTIA_CHECK(!lifecycle_route.claim(lifecycle_irq_entry));

    astia::arch::cpu::interrupt_enter();
    ASTIA_CHECK(!astia::module::make_resident());
    ASTIA_CHECK(!lifecycle_route.claim(lifecycle_irq_entry));
    astia::arch::cpu::interrupt_exit();

    lifecycle_wait_release.store(true, std::memory_order_release);
    runner.join();

    ASTIA_CHECK(run_error.load(std::memory_order_acquire) ==
                astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::exit(id) == astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::unload(id) == astia::mod::module_error::ok);
}

ASTIA_TEST(module_lifecycle_make_resident_marks_calling_module) {
    loader_state                  state{};
    const astia::elf::loader::env env = environment(state);
    astia::mod::module_id         id{};

    lifecycle_api_mode = 0;

    ASTIA_CHECK(astia::mod::load_image(blob(module_lifecycle_module_api_blob,
                                            module_lifecycle_module_api_blob_len),
                                       env, {}, id) == astia::mod::module_error::ok);

    int result = 0;

    ASTIA_CHECK(astia::mod::run(id, result) == astia::mod::module_error::ok);
    ASTIA_CHECK(result == 61);
    ASTIA_CHECK(lifecycle_resident_result);
    ASTIA_CHECK(!lifecycle_resident_in_interrupt);
    ASTIA_CHECK(astia::mod::exit(id) == astia::mod::module_error::ok);

    auto closing = astia::mod::close(id);

    closing.resume();
    ASTIA_CHECK(closing.done());
    ASTIA_CHECK(closing.value() != nullptr &&
                *closing.value() == astia::mod::module_error::resident);
    ASTIA_CHECK(state.unmapped == 0);
    ASTIA_CHECK(astia::mod::clear_resident_for_test(id) == astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::unload(id) == astia::mod::module_error::ok);
    ASTIA_CHECK(state.unmapped == 1);
}

ASTIA_TEST(module_lifecycle_claim_route_guards_and_releases) {
    loader_state                  state{};
    const astia::elf::loader::env env = environment(state);
    astia::mod::module_id         id{};

    lifecycle_api_mode    = 1;
    lifecycle_handler_ran = false;

    astia::arch::host_test::set_eoi_observer(nullptr);

    ASTIA_CHECK(astia::mod::load_image(blob(module_lifecycle_module_api_blob,
                                            module_lifecycle_module_api_blob_len),
                                       env, {}, id) == astia::mod::module_error::ok);

    int result = 0;

    ASTIA_CHECK(astia::mod::run(id, result) == astia::mod::module_error::ok);
    ASTIA_CHECK(result == 61);
    ASTIA_CHECK(lifecycle_claim_result);
    ASTIA_CHECK(!lifecycle_claim_in_interrupt);
    ASTIA_CHECK(!lifecycle_handler_ran);

    astia::arch::host_test::dispatch_irq(1);
    ASTIA_CHECK(lifecycle_handler_ran);

    ASTIA_CHECK(astia::mod::exit(id) == astia::mod::module_error::ok);

    auto closing = astia::mod::close(id);

    closing.resume();
    ASTIA_CHECK(!closing.done());

    lifecycle_handler_ran = false;

    astia::arch::host_test::dispatch_irq(1);
    ASTIA_CHECK(!lifecycle_handler_ran);

    ASTIA_CHECK(lifecycle_route.release());
    ASTIA_CHECK(astia::core::drain_resumes() == 1);
    ASTIA_CHECK(closing.done());
    ASTIA_CHECK(closing.value() != nullptr &&
                *closing.value() == astia::mod::module_error::ok);
    ASTIA_CHECK(state.unmapped == 1);
}

ASTIA_TEST(module_lifecycle_claim_rollback_leaves_line_unowned) {
    loader_state                  state{};
    const astia::elf::loader::env env = environment(state);
    astia::mod::module_id         id{};

    lifecycle_api_mode = 2;

    ASTIA_CHECK(astia::mod::load_image(blob(module_lifecycle_module_api_blob,
                                            module_lifecycle_module_api_blob_len),
                                       env, {}, id) == astia::mod::module_error::ok);

    int result = 0;

    ASTIA_CHECK(astia::mod::run(id, result) == astia::mod::module_error::ok);
    ASTIA_CHECK(result == 61);
    ASTIA_CHECK(lifecycle_claim_result);
    ASTIA_CHECK(lifecycle_release_result);
    ASTIA_CHECK(!lifecycle_claim_in_interrupt);
    ASTIA_CHECK(!astia::arch::host_test::irq_registered(1));
    ASTIA_CHECK(astia::arch::host_test::irq_masked(1));

    ASTIA_CHECK(astia::mod::exit(id) == astia::mod::module_error::ok);
    ASTIA_CHECK(astia::mod::unload(id) == astia::mod::module_error::ok);
}

ASTIA_TEST(module_lifecycle_close_waiter_defers_from_guarded_route) {
    loader_state                  state{};
    const astia::elf::loader::env env = environment(state);
    astia::mod::module_id         id{};

    lifecycle_api_mode = 1;

    astia::core::initialize_resume_queues();

    ASTIA_CHECK(astia::mod::load_image(blob(module_lifecycle_module_api_blob,
                                            module_lifecycle_module_api_blob_len),
                                       env, {}, id) == astia::mod::module_error::ok);

    int result = 0;

    ASTIA_CHECK(astia::mod::run(id, result) == astia::mod::module_error::ok);
    ASTIA_CHECK(result == 61);
    ASTIA_CHECK(lifecycle_claim_result);
    ASTIA_CHECK(astia::mod::exit(id) == astia::mod::module_error::ok);

    const auto filler   = std::noop_coroutine();
    astia::u32 accepted = 0;

    while (astia::core::schedule_resume(filler, accepted + 1)) {
        ++accepted;
    }

    ASTIA_CHECK(accepted != 0);

    auto closing = astia::mod::close(id);

    closing.resume();
    ASTIA_CHECK(!closing.done());

    astia::arch::cpu::interrupt_enter();

    const bool released = lifecycle_route.release();

    astia::arch::cpu::interrupt_exit();

    ASTIA_CHECK(released);
    ASTIA_CHECK(!closing.done());
    ASTIA_CHECK(state.unmapped == 0);

    astia::u32 drained = 0;

    while (const astia::u32 count = astia::core::drain_resumes(
               astia::resource_limits::queue::resume * 2 + 1)) {
        drained += count;
    }

    ASTIA_CHECK(drained == accepted);
    ASTIA_CHECK(!closing.done());

    astia::mod::service_module_waits();
    ASTIA_CHECK(astia::core::drain_resumes() == 1);
    ASTIA_CHECK(closing.done());
    ASTIA_CHECK(closing.value() != nullptr &&
                *closing.value() == astia::mod::module_error::ok);
    ASTIA_CHECK(state.unmapped == 1);
}

ASTIA_TEST(module_lifecycle_close_unbinds_module_owned_drivers) {
    loader_state                  state{};
    const astia::elf::loader::env env = environment(state);
    astia::mod::module_id         id{};

    ASTIA_CHECK(astia::mod::load_image(blob(module_lifecycle_no_fini_blob,
                                            module_lifecycle_no_fini_blob_len),
                                       env, {}, id) == astia::mod::module_error::ok);

    const astia::hw::owner_id owner = astia::mod::driver_owner(id);

    ASTIA_CHECK(owner == astia::hw::owner_id{id.slot + 1});
    ASTIA_CHECK(owner != astia::hw::kernel_owner);

    owner_test_driver        driver;
    astia::hw::driver_handle registered{};

    ASTIA_CHECK(astia::hw::drivers().register_driver(
                    driver, registered, owner, astia::hw::driver_resource_async) == 0);

    astia::mm::shptr<owner_test_info> info(new owner_test_info(7));

    astia::mm::shptr<astia::hw::device_info> device(
        static_cast<astia::mm::shptr<owner_test_info>&&>(info));

    astia::hw::device_handle bound{};
    astia::i64               bind_result = -1;

    ASTIA_CHECK(astia_test::run_task(
        astia::hw::drivers().bind_device(
            static_cast<astia::mm::shptr<astia::hw::device_info>&&>(device), bound),
        bind_result));
    ASTIA_CHECK(bind_result == 0);
    ASTIA_CHECK(bound.valid());
    ASTIA_CHECK(astia::hw::drivers().devices_owned_by(owner) == 1);
    ASTIA_CHECK(astia::mod::unload(id) == astia::mod::module_error::busy);

    auto closing = astia::mod::close(id);

    closing.resume();
    ASTIA_CHECK(closing.done());
    ASTIA_CHECK(closing.value() != nullptr &&
                *closing.value() == astia::mod::module_error::ok);
    ASTIA_CHECK(driver.stop_calls == 1);
    ASTIA_CHECK(driver.remove_calls == 1);
    ASTIA_CHECK(astia::hw::drivers().devices_owned_by(owner) == 0);
    ASTIA_CHECK(!astia::hw::drivers().valid(bound));
    ASTIA_CHECK(astia::hw::drivers().unregister_driver(registered) == 0);
}
