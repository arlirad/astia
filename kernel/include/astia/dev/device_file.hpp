#pragma once

#include <astia/core/spinlock.hpp>
#include <astia/dev/device.hpp>
#include <astia/fs.hpp>
#include <astia/mm/shptr.hpp>

namespace astia::dev {
    class device_file : public fs::file,
                        public fs::readable,
                        public fs::writable,
                        public fs::pollable,
                        public fs::ioctlable {
        public:
        constexpr explicit device_file(device* device) : borrowed_(device) {}

        explicit device_file(const mm::shptr<device>& device) : owner_(device) {}

        optional<fs::readable&> as_readable() override {
            return *this;
        }

        optional<fs::writable&> as_writable() override {
            return *this;
        }

        optional<fs::pollable&> as_pollable() override {
            return *this;
        }

        optional<fs::ioctlable&> as_ioctlable() override {
            return *this;
        }

        void detach();
        using quiescence_handler = void (*)(void*, u64);
        bool            arm_quiescence_handler(quiescence_handler handler, void* context,
                                               u64 generation);
        bool            quiescent() const;
        core::task<i64> readv(span<const fs::buffer> buffers, u64 offset,
                              core::cltoken token) override;
        core::task<i64> writev(span<const fs::buffer> buffers, u64 offset,
                               core::cltoken token) override;
        long            submit(io::request& request) override;
        u32             poll(u32 events, u32 flags) override;
        core::task<i64> ioctl(u64 request, void* argp, usize size,
                              core::cltoken token) override;
        long            acquire_controlling_terminal(i32 group) override;
        long            size() const override;
        u32             block_size() const override;

        fs::file_mode mode() const override {
            device*             value = begin_operation();
            const fs::file_mode result(
                (value != nullptr && (value->flags() & dv_block) != 0)
                    ? fs::file_type::block
                    : fs::file_type::character,
                fs::default_device_permissions);
            if (value != nullptr)
                end_operation();
            return result;
        }

        private:
        device*                    borrowed_ = nullptr;
        mm::shptr<device>          owner_;
        static constexpr u32       max_pending_requests = 64;
        mutable core::spinlock     pending_lock_;
        mutable u32                active_        = 0;
        mutable u32                pending_count_ = 0;
        mutable io::request*       pending_[max_pending_requests]{};
        mutable bool               pending_owned_[max_pending_requests]{};
        mutable quiescence_handler quiescence_handler_    = nullptr;
        mutable void*              quiescence_context_    = nullptr;
        mutable u64                quiescence_generation_ = 0;
        mutable bool               detached_              = false;

        device* device_instance() const {
            return owner_ ? owner_.get() : borrowed_;
        }

        device* begin_operation(const io::request* request = nullptr) const;
        void    end_operation() const;
        void    notify_quiescent() const;
        bool    track_request(io::request& request) const;
        bool    tracked_request(const io::request& request) const;
        void    untrack_request(io::request& request) const;
    };
} // namespace astia::dev
