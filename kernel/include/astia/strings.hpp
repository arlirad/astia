#pragma once

#include <astia/common.hpp>
#include <astia/optional.hpp>

namespace astia {
    /// Non-owning byte range. A null pointer is valid only for an empty range.
    ///
    /// The two-member layout is part of the source contract: callers rely on
    /// brace initialization such as `{data, size}`.
    struct string_view {
        const char* data = nullptr;
        usize       size = 0;

        constexpr bool valid() const {
            return data != nullptr || size == 0;
        }

        constexpr bool empty() const {
            return size == 0;
        }

        /// Byte-for-byte equality. Invalid views are never equal, not even to
        /// another invalid view.
        constexpr bool equals(string_view other) const {
            if (!valid() || !other.valid() || size != other.size)
                return false;

            for (usize i = 0; i < size; ++i) {
                if (data[i] != other.data[i])
                    return false;
            }

            return true;
        }

        /// Prefix test. Invalid views never match.
        constexpr bool starts_with(string_view prefix) const {
            if (!valid() || !prefix.valid() || prefix.size > size)
                return false;

            for (usize i = 0; i < prefix.size; ++i) {
                if (data[i] != prefix.data[i])
                    return false;
            }

            return true;
        }

        /// Unsigned-byte lexicographic order, then length. Two invalid views
        /// compare equal; a valid view orders above an invalid one.
        constexpr int compare(string_view other) const {
            if (!valid() || !other.valid()) {
                if (valid() == other.valid())
                    return 0;

                return valid() ? 1 : -1;
            }

            const usize common = size < other.size ? size : other.size;

            for (usize i = 0; i < common; ++i) {
                const auto left  = static_cast<unsigned char>(data[i]);
                const auto right = static_cast<unsigned char>(other.data[i]);

                if (left != right)
                    return left < right ? -1 : 1;
            }

            if (size == other.size)
                return 0;

            return size < other.size ? -1 : 1;
        }

        /// Split at the first NUL inside the range, without reading past it.
        constexpr bool find_nul(string_view& output) const {
            if (!valid())
                return false;

            for (usize i = 0; i < size; ++i) {
                if (data[i] == '\0') {
                    output = {data, i};

                    return true;
                }
            }

            return false;
        }

        /// Explicit conversion from a NUL-terminated C string. A null pointer
        /// yields the valid empty view `{nullptr, 0}`.
        static constexpr string_view from_c_str(const char* input) {
            if (input == nullptr)
                return {};

            usize length = 0;

            while (input[length] != '\0') {
                ++length;
            }

            return {input, length};
        }
    };

    /// Compare two NUL-terminated strings. A null pointer is not equal to
    /// anything, including another null pointer. This is the C-string helper
    /// shared by the filesystem code; string_view comparisons use
    /// string_view::equals.
    constexpr bool string_equal(const char* lhs, const char* rhs) {
        if (lhs == nullptr || rhs == nullptr)
            return false;

        while (*lhs != '\0' && *lhs == *rhs) {
            ++lhs;
            ++rhs;
        }

        return *lhs == *rhs;
    }
} // namespace astia

namespace astia::strings {
    /// Owning, NUL-terminated byte string backed by the kernel heap.
    ///
    /// A default-constructed string owns nothing: `data_ == nullptr`,
    /// `size_ == 0`, `capacity_ == 0`, and `c_str()` returns a static `""`.
    /// Every allocating operation reports failure through `optional`/`bool`
    /// instead of throwing or panicking, and failing operations leave the
    /// receiver unchanged.
    ///
    /// Allocation requires the kernel heap to be initialized: before
    /// `mm::heap::init()` every allocating operation fails with
    /// `error_t::enomem`, matching `mm::heap::alloc`.
    class string {
        public:
        string() = default;
        ~string();

        string(string&& other) noexcept;
        string& operator=(string&& other) noexcept;

        string(const string&)            = delete;
        string& operator=(const string&) = delete;

        /// Copy a view into a new owning string.
        static optional<string> try_from(string_view input);

        /// Copy a NUL-terminated C string into a new owning string. A null
        /// pointer is rejected with `error_t::einval`.
        static optional<string> try_from(const char* input);

        /// Explicit deep copy of this string.
        optional<string> clone() const;

        /// Replace the contents. Returns false on allocation failure or for an
        /// invalid view; the previous contents survive a failure.
        bool assign(string_view input);

        /// Append a suffix. Returns false on allocation failure or for an
        /// invalid view; the previous contents survive a failure.
        bool append(string_view suffix);

        /// Drop the contents but keep the allocation for reuse.
        void clear();

        /// Non-owning view. An empty string yields `{nullptr, 0}`.
        string_view view() const;

        /// Always non-null; a static `""` when empty.
        const char* c_str() const;

        /// Bytes, excluding the terminating NUL.
        usize size() const;

        bool empty() const;

        private:
        char* data_     = nullptr;
        usize size_     = 0;
        usize capacity_ = 0; // Allocated bytes, including the terminating NUL.
    };
} // namespace astia::strings
