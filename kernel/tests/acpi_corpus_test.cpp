#include "acpi/aml.hpp"
#include "acpi/tables.hpp"
#include "acpi_corpus.hpp"
#include "test.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
    using astia::span;
    using astia::u32;
    using astia::u64;
    using astia::u8;
    using astia::usz;
    using astia::acpi::detail::aml_error;
    using astia::acpi::detail::madt_error;
    using astia::acpi::detail::table_error;
    using astia_test::acpi_corpus::aml_capacity;
    using astia_test::acpi_corpus::aml_case;
    using astia_test::acpi_corpus::aml_cases;
    using astia_test::acpi_corpus::build_aml_image;
    using astia_test::acpi_corpus::build_madt_image;
    using astia_test::acpi_corpus::build_table_image;
    using astia_test::acpi_corpus::fixture_kind;
    using astia_test::acpi_corpus::image;
    using astia_test::acpi_corpus::madt_case;
    using astia_test::acpi_corpus::madt_cases;
    using astia_test::acpi_corpus::table_case;
    using astia_test::acpi_corpus::table_cases;
    using astia_test::acpi_corpus::use_image_rsdp;
    using astia_test::acpi_corpus::valid_s5_aml;

    void* host_allocate(void*, usz bytes) {
        return std::malloc(bytes);
    }

    void host_release(void*, void* pointer) {
        std::free(pointer);
    }

    const astia::acpi::detail::table_allocator host_allocator{
        nullptr,
        host_allocate,
        host_release,
    };

    u32 next_random(u32& state) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;

        return state;
    }

    void scramble(image& target, u32& state, u32 flips) {
        if (target.size() == 0)
            return;

        for (u32 i = 0; i < flips; ++i) {
            const usz position = next_random(state) % target.size();
            const u8  bit      = static_cast<u8>(1u << (next_random(state) % 8));

            target.data()[position] = static_cast<u8>(target.data()[position] ^ bit);
        }
    }
} // namespace

ASTIA_TEST(acpi_corpus_baselines_validate) {
    for (int extended = 0; extended < 2; ++extended) {
        image target;

        ASTIA_CHECK(build_table_image(target, extended != 0));

        astia::acpi::detail::table_set tables;

        ASTIA_CHECK(astia::acpi::detail::discover(target.reader(), target.rsdp_address(),
                                                  host_allocator, tables));
        ASTIA_CHECK(tables.error == table_error::none);
        ASTIA_CHECK(tables.count == 3);
    }

    for (int extended = 0; extended < 2; ++extended) {
        image target;

        ASTIA_CHECK(build_madt_image(target, extended != 0));

        u32 apic_ids[4]{};
        u32 count = 0;

        auto error = madt_error::none;

        ASTIA_CHECK(astia::acpi::detail::discover_madt(
            target.reader(), target.rsdp_address(), apic_ids, 4, count, error));
        ASTIA_CHECK(error == madt_error::none);
        ASTIA_CHECK(count == 1);
        ASTIA_CHECK(apic_ids[0] == 0);
    }

    const span<const u8> aml = valid_s5_aml();
    image                target;

    ASTIA_CHECK(build_aml_image(target, aml.data(), aml.size()));

    astia::acpi::detail::table_set tables;

    tables.dsdt = {
        span<const u8>(target.data(), target.size()),
        target.base(),
    };

    astia::acpi::detail::interpreter interpreter;

    ASTIA_CHECK(interpreter.build(tables));

    astia::acpi::detail::value_id package = astia::acpi::detail::no_value;

    ASTIA_CHECK(interpreter.evaluate_s5(package));
    ASTIA_CHECK(interpreter.get(package) != nullptr);
    ASTIA_CHECK(interpreter.get(package)->count == 4);
}

ASTIA_TEST(acpi_corpus_table_rejections) {
    usz cases = 0;

    const span<const table_case> corpus = table_cases();

    for (usz index = 0; index < corpus.size(); ++index) {
        const table_case& test = corpus[index];

        image target;

        const bool built = build_table_image(target, test.kind == fixture_kind::rsdp_v2);

        ASTIA_CHECK(built);

        if (!built)
            continue;

        test.mutate(target);

        const u64 address =
            test.address == use_image_rsdp ? target.rsdp_address() : test.address;

        astia::acpi::detail::table_set tables;

        const bool accepted = astia::acpi::detail::discover(target.reader(), address,
                                                            host_allocator, tables);

        if (accepted || tables.error != test.expected)
            std::printf("    case %s: accepted=%d error=%s expected=%s\n", test.name,
                        accepted ? 1 : 0, astia::acpi::detail::error_text(tables.error),
                        astia::acpi::detail::error_text(test.expected));

        ASTIA_CHECK(!accepted);
        ASTIA_CHECK(tables.error == test.expected);
        ASTIA_CHECK(tables.count == 0);

        ++cases;
    }

    std::printf("acpi corpus: %zu malformed table case(s)\n", cases);
}

ASTIA_TEST(acpi_corpus_madt_rejections) {
    usz cases      = 0;
    usz executions = 0;

    const span<const madt_case> corpus = madt_cases();

    for (usz index = 0; index < corpus.size(); ++index) {
        const madt_case& test = corpus[index];

        for (int root = 0; root < 2; ++root) {
            const bool extended = root != 0;

            if (test.extended_only && !extended)
                continue;

            image target;

            const bool built = build_madt_image(target, extended);

            ASTIA_CHECK(built);

            if (!built)
                continue;

            test.mutate(target);

            u32 apic_ids[8]{};
            u32 count = 0;

            auto error = madt_error::none;

            const bool accepted =
                astia::acpi::detail::discover_madt(target.reader(), target.rsdp_address(),
                                                   apic_ids, test.capacity, count, error);

            if (accepted || error != test.expected || count != 0)
                std::printf(
                    "    case %s (%s): accepted=%d error=%u expected=%u count=%u\n",
                    test.name, extended ? "xsdt" : "rsdt", accepted ? 1 : 0,
                    static_cast<unsigned>(error), static_cast<unsigned>(test.expected),
                    count);

            ASTIA_CHECK(!accepted);
            ASTIA_CHECK(error == test.expected);
            ASTIA_CHECK(count == 0);

            ++executions;
        }

        ++cases;
    }

    std::printf("acpi corpus: %zu malformed MADT case(s), %zu execution(s)\n", cases,
                executions);
}

ASTIA_TEST(acpi_corpus_aml_rejections) {
    std::vector<u8> generated(aml_capacity);

    usz cases = 0;

    const span<const aml_case> corpus = aml_cases();

    for (usz index = 0; index < corpus.size(); ++index) {
        const aml_case& test = corpus[index];

        const u8* aml = test.bytes;

        usz length = test.length;

        if (aml == nullptr) {
            length = test.generate(generated.data(), generated.size());
            aml    = generated.data();
        }

        ASTIA_CHECK(aml != nullptr);
        ASTIA_CHECK(length != 0);

        if (aml == nullptr || length == 0)
            continue;

        image target;

        const bool encoded = build_aml_image(target, aml, length);

        ASTIA_CHECK(encoded);

        if (!encoded)
            continue;

        astia::acpi::detail::table_set tables;

        tables.dsdt = {
            span<const u8>(target.data(), target.size()),
            target.base(),
        };

        astia::acpi::detail::interpreter interpreter;

        const bool built = interpreter.build(tables);

        if (test.evaluated) {
            if (!built)
                std::printf("    case %s: build failed with %s\n", test.name,
                            astia::acpi::detail::aml_error_text(interpreter.error()));

            ASTIA_CHECK(built);

            astia::acpi::detail::value_id package = astia::acpi::detail::no_value;

            const bool evaluated = interpreter.evaluate_s5(package);

            if (evaluated || interpreter.error() != test.expected)
                std::printf("    case %s: evaluated=%d error=%s expected=%s\n", test.name,
                            evaluated ? 1 : 0,
                            astia::acpi::detail::aml_error_text(interpreter.error()),
                            astia::acpi::detail::aml_error_text(test.expected));

            ASTIA_CHECK(!evaluated);
            ASTIA_CHECK(interpreter.error() == test.expected);
        }
        else {
            if (built || interpreter.error() != test.expected)
                std::printf("    case %s: built=%d error=%s expected=%s\n", test.name,
                            built ? 1 : 0,
                            astia::acpi::detail::aml_error_text(interpreter.error()),
                            astia::acpi::detail::aml_error_text(test.expected));

            ASTIA_CHECK(!built);
            ASTIA_CHECK(interpreter.error() == test.expected);
        }

        ++cases;
    }

    std::printf("acpi corpus: %zu malformed AML case(s)\n", cases);
}

ASTIA_TEST(acpi_corpus_random_mutations_stay_bounded) {
    constexpr u32 rounds = 128;
    constexpr u32 flips  = 6;

    u32 state = 0x567u;

    for (u32 round = 0; round < rounds; ++round) {
        {
            image target;

            ASTIA_CHECK(build_table_image(target, (round & 1u) != 0));
            scramble(target, state, flips);

            astia::acpi::detail::table_set tables;

            (void) astia::acpi::detail::discover(target.reader(), target.rsdp_address(),
                                                 host_allocator, tables);
            ASTIA_CHECK(tables.count <= astia::acpi::detail::max_tables);
        }

        {
            image target;

            ASTIA_CHECK(build_madt_image(target, (round & 1u) != 0));
            scramble(target, state, flips);

            u32 apic_ids[8]{};
            u32 count = 0;

            auto error = madt_error::none;

            (void) astia::acpi::detail::discover_madt(
                target.reader(), target.rsdp_address(), apic_ids, 8, count, error);
            ASTIA_CHECK(count <= 8);
        }

        {
            const span<const u8> aml = valid_s5_aml();
            image                target;

            ASTIA_CHECK(build_aml_image(target, aml.data(), aml.size()));
            scramble(target, state, flips);

            astia::acpi::detail::table_set tables;

            tables.dsdt = {
                span<const u8>(target.data(), target.size()),
                target.base(),
            };

            astia::acpi::detail::interpreter interpreter;

            const bool built = interpreter.build(tables);

            ASTIA_CHECK(interpreter.node_count() <= astia::acpi::detail::max_aml_nodes);
            ASTIA_CHECK(interpreter.value_count() <= astia::acpi::detail::max_aml_values);

            if (!built) {
                ASTIA_CHECK(interpreter.error() != aml_error::none);
                continue;
            }

            astia::acpi::detail::value_id package = astia::acpi::detail::no_value;

            (void) interpreter.evaluate_s5(package);
            ASTIA_CHECK(interpreter.node_count() <= astia::acpi::detail::max_aml_nodes);
            ASTIA_CHECK(interpreter.value_count() <= astia::acpi::detail::max_aml_values);
        }
    }

    std::printf("acpi corpus: %u deterministic mutation round(s)\n", rounds);
}
