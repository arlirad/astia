#include <astia/mm/heap.hpp>
#include <astia/strings.hpp>

namespace astia::strings {
    string::~string() {
        if (data_ != nullptr)
            mm::heap::free(data_);
    }

    string::string(string&& other) noexcept
        : data_(other.data_), size_(other.size_), capacity_(other.capacity_) {
        other.data_     = nullptr;
        other.size_     = 0;
        other.capacity_ = 0;
    }

    string& string::operator=(string&& other) noexcept {
        if (this == &other)
            return *this;

        if (data_ != nullptr)
            mm::heap::free(data_);

        data_     = other.data_;
        size_     = other.size_;
        capacity_ = other.capacity_;

        other.data_     = nullptr;
        other.size_     = 0;
        other.capacity_ = 0;

        return *this;
    }

    optional<string> string::try_from(string_view input) {
        if (!input.valid())
            return optional<string>(error_t::einval);

        string result;

        if (!result.assign(input))
            return optional<string>(error_t::enomem);

        return optional<string>(static_cast<string&&>(result));
    }

    optional<string> string::try_from(const char* input) {
        if (input == nullptr)
            return optional<string>(error_t::einval);

        return try_from(string_view::from_c_str(input));
    }

    optional<string> string::clone() const {
        return try_from(view());
    }

    bool string::assign(string_view input) {
        if (!input.valid() || input.size == static_cast<usize>(-1))
            return false;

        if (input.size == 0) {
            clear();

            return true;
        }

        if (data_ == nullptr || capacity_ < input.size + 1) {
            const auto block = mm::heap::alloc(input.size + 1);

            if (!block || block.value == nullptr)
                return false;

            if (data_ != nullptr)
                mm::heap::free(data_);

            data_     = reinterpret_cast<char*>(block.value);
            capacity_ = input.size + 1;
        }

        for (usize i = 0; i < input.size; ++i) {
            data_[i] = input.data[i];
        }

        data_[input.size] = '\0';
        size_             = input.size;

        return true;
    }

    bool string::append(string_view suffix) {
        if (!suffix.valid())
            return false;

        if (suffix.size == 0)
            return true;

        const usize needed = size_ + suffix.size + 1;
        if (needed <= size_)
            return false; // Length overflow.

        if (capacity_ < needed) {
            const auto block = mm::heap::alloc(needed);

            if (!block || block.value == nullptr)
                return false;

            char* grown = reinterpret_cast<char*>(block.value);

            for (usize i = 0; i < size_; ++i) {
                grown[i] = data_[i];
            }

            // Copy the suffix before releasing the old block: it may be a
            // view into this string, as in append(view()).
            for (usize i = 0; i < suffix.size; ++i) {
                grown[size_ + i] = suffix.data[i];
            }

            if (data_ != nullptr)
                mm::heap::free(data_);

            data_     = grown;
            capacity_ = needed;
        }
        else {
            for (usize i = 0; i < suffix.size; ++i) {
                data_[size_ + i] = suffix.data[i];
            }
        }

        size_ += suffix.size;
        data_[size_] = '\0';

        return true;
    }

    void string::clear() {
        if (data_ != nullptr)
            data_[0] = '\0';

        size_ = 0;
    }

    string_view string::view() const {
        if (size_ == 0)
            return {};

        return {data_, size_};
    }

    const char* string::c_str() const {
        if (data_ == nullptr)
            return "";

        return data_;
    }

    usize string::size() const {
        return size_;
    }

    bool string::empty() const {
        return size_ == 0;
    }
} // namespace astia::strings
