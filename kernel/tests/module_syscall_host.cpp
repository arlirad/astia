#include <astia/api/errno.h>
#include <astia/core/task.hpp>
#include <astia/error.hpp>
#include <astia/mm/address_space.hpp>
#include <astia/mm/frame_allocator.hpp>

#include "../src/mod/module.hpp"
#include "../src/proc/process.hpp"

#include <sys/mman.h>

#include <cstring>

extern unsigned char module_lifecycle_no_fini_blob[];
extern unsigned int  module_lifecycle_no_fini_blob_len;

namespace {

    alignas(astia::proc::process) unsigned char process_storage[sizeof(
        astia::proc::process)]{};

    char last_path[ASTIA_MODULE_PATH_MAX]{};
    astia::mod::module_error next_error = astia::mod::module_error::ok;

    void* map_image(void*, astia::u64, astia::usize size, astia::usize, bool fixed) {
        if (fixed) {
            return nullptr;
        }
        void* image = mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        return image == MAP_FAILED ? nullptr : image;
    }

    void unmap_image(void*, void* image, astia::usize size) {
        if (image != nullptr) {
            munmap(image, size);
        }
    }

    astia::u64 resolve_symbol(void*, const char*) {
        return 0;
    }

    astia::elf::loader::env host_environment() {
        astia::elf::loader::env env{};
        env.map     = map_image;
        env.unmap   = unmap_image;
        env.resolve = resolve_symbol;
        return env;
    }

} // namespace

namespace astia::mm {

    bool address_space::attached() const {
        return false;
    }

    bool address_space::valid_user_range(u64, usize) const {
        return false;
    }

    bool address_space::validate(u64, usize, access) const {
        return false;
    }

    bool address_space::user_accessible(vaddr_t) const {
        return false;
    }

    bool address_space::writable(vaddr_t) const {
        return false;
    }

    paddr_t address_space::translate(vaddr_t) const {
        return invalid;
    }

    frame_allocator& kernel_frames() {
        static frame_allocator allocator;
        return allocator;
    }

} // namespace astia::mm

namespace astia::proc {

    bool process::cow_fault(u64, bool) {
        return false;
    }

    process* current_process() {
        return reinterpret_cast<process*>(process_storage);
    }

    i64 process::seteuid(u32 value) {
        arch::interrupt_spinlock_guard guard(_metadata_lock);
        if (_euid != 0 && value != _uid && value != _euid) {
            return -EPERM;
        }
        _euid = value;
        return 0;
    }

    i64 run_task(core::task<i64> operation) {
        operation.resume();
        while (!operation.done()) {
            if (core::drain_resumes() == 0) {
                return -EAGAIN;
            }
            operation.resume();
        }
        if (operation.failed()) {
            return -error_errno(operation.error());
        }
        return *operation.value();
    }

} // namespace astia::proc

namespace astia::mod::host_test {

    int invoke_entry(int (*entry)()) {
        return entry();
    }

    void invoke_initializer(elf::loader::init_fn initializer) {
        initializer();
    }

    void invoke_finalizer(elf::loader::init_fn finalizer) {
        finalizer();
    }

} // namespace astia::mod::host_test

namespace astia::mod {

    module_error load_test_module(module_id& out, const module_load_options& options) {
        const elf::loader::env env = ::host_environment();
        return load_image(
            span<const u8>(module_lifecycle_no_fini_blob,
                           module_lifecycle_no_fini_blob_len),
            env, options, out);
    }

    const char* last_open_path() {
        return last_path;
    }

    void set_open_path_error(module_error error) {
        next_error = error;
    }

    core::task<module_open_result> open_path(proc::process&, const char* path,
                                             const module_load_options& options) {
        module_open_result result{};
        std::memset(last_path, 0, sizeof last_path);
        if (path != nullptr) {
            for (usize i = 0; i + 1 < sizeof last_path && path[i] != '\0'; ++i) {
                last_path[i] = path[i];
            }
        }
        if (next_error != module_error::ok) {
            result.error = next_error;
            co_return result;
        }
        result.error = load_test_module(result.id, options);
        co_return result;
    }

} // namespace astia::mod
