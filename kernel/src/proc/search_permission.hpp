#pragma once

#include <astia/fs.hpp>

#include "process.hpp"

namespace astia::proc {
    class process_search_permission final : public fs::search_permission {
        public:
        explicit process_search_permission(const process& owner) : _owner(owner) {}

        bool allows(const char* directory) const override {
            if (directory == nullptr)
                return false;

            const fs::file_mode mode = fs::node_mode(directory);

            if (!mode.is_directory())
                return false;

            if (_owner.euid() == 0)
                return true;

            return _owner.can_access(mode, fs::node_uid(directory),
                                     fs::node_gid(directory),
                                     fs::permission_bits::execute);
        }

        private:
        const process& _owner;
    };
} // namespace astia::proc
