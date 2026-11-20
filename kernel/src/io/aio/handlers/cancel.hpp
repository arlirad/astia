#pragma once

#include "../prototype/dispatch.hpp"

namespace astia::io::aio::handlers {

    template <typename Context>
    struct cancel : prototype::handler<cancel<Context>, prototype::opcode::cancel> {
        using context_type = Context;

        static prototype::dispatch_result handle(context_type&    context,
                                                 const astia_sqe& sqe) noexcept {
            return context.cancel(sqe);
        }
    };

} // namespace astia::io::aio::handlers
