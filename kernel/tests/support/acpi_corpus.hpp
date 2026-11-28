#pragma once

#include <astia/common.hpp>
#include <astia/span.hpp>

#include "acpi/aml.hpp"
#include "acpi/madt.hpp"
#include "acpi/tables.hpp"

namespace astia_test::acpi_corpus {
    using astia::span;
    using astia::u32;
    using astia::u64;
    using astia::u8;
    using astia::usz;
    using astia::acpi::detail::aml_error;
    using astia::acpi::detail::madt_error;
    using astia::acpi::detail::memory_reader;
    using astia::acpi::detail::table_error;

    constexpr usz image_capacity = 64 * 1024;
    constexpr usz aml_capacity   = 48 * 1024;

    constexpr u64 use_image_rsdp = ~u64{0};

    /// Firmware image whose backing allocation ends exactly at the last
    /// declared table byte. A reader overrun past that boundary is a heap
    /// overread under ASan.
    class image {
        public:
        image() = default;
        ~image();

        image(const image&)            = delete;
        image& operator=(const image&) = delete;

        u8* data() {
            return _data;
        }

        const u8* data() const {
            return _data;
        }

        usz size() const {
            return _size;
        }

        u64 base() const {
            return _base;
        }

        u64 rsdp_address() const {
            return _base;
        }

        bool          read(u64 address, u8* destination, usz length) const;
        memory_reader reader() const;

        private:
        friend bool build_table_image(image& out, bool extended);
        friend bool build_madt_image(image& out, bool extended);
        friend bool build_aml_image(image& out, const u8* aml, usz length);

        bool assign(const u8* bytes, usz size, u64 base);

        u8* _data = nullptr;
        usz _size = 0;
        u64 _base = 0;
    };

    bool build_table_image(image& out, bool extended);

    bool build_madt_image(image& out, bool extended);

    bool build_aml_image(image& out, const u8* aml, usz length);

    /// Valid DSDT body declaring `\_S5` as a four-entry package.
    span<const u8> valid_s5_aml();

    enum class fixture_kind : u8 {
        rsdp_v1,
        rsdp_v2,
    };

    struct table_case {
        const char*  name;
        fixture_kind kind;
        void (*mutate)(image&);
        table_error expected;
        u64         address = use_image_rsdp;
    };

    struct madt_case {
        const char* name;
        void (*mutate)(image&);
        madt_error expected;
        u32        capacity      = 4;
        bool       extended_only = false;
    };

    using aml_generator = usz (*)(u8* out, usz capacity);

    struct aml_case {
        const char*   name;
        const u8*     bytes;
        usz           length;
        aml_generator generate;
        aml_error     expected;
        bool          evaluated;
    };

    span<const table_case> table_cases();

    span<const madt_case> madt_cases();

    span<const aml_case> aml_cases();
} // namespace astia_test::acpi_corpus
