#pragma once

#include "../prototype/dispatch.hpp"

namespace astia::io::aio::handlers {

    template <typename Context>
    struct read : prototype::handler<read<Context>, prototype::opcode::read> {
        using context_type = Context;

        static prototype::dispatch_result handle(context_type&    context,
                                                 const astia_sqe& sqe) noexcept {
            return context.read(sqe);
        }
    };

} // namespace astia::io::aio::handlers
