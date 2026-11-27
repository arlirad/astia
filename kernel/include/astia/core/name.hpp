#pragma once

#include <astia/common.hpp>

namespace astia::core {
    /// Fill `buffer` with the next name generated for `pattern`.
    ///
    /// The pattern holds exactly one '%': the counter is rendered there and
    /// any prefix or suffix is kept verbatim.  `name_letter_increment`
    /// produces a..z, aa..az, ba..; `name_number_increment` produces 0, 1,
    /// 2..  Each pattern has its own counter, shared process-wide and
    /// serialised.  Returns false for a malformed pattern or when the
    /// rendered name does not fit.
    bool name_letter_increment(char* buffer, usz capacity, const char* pattern);
    bool name_number_increment(char* buffer, usz capacity, const char* pattern);
} // namespace astia::core
