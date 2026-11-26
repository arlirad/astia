#include <astia/dbg/panic_report.hpp>

#include "record.hpp"

namespace astia::dbg {
    namespace {
        void emit_line(sink_fn sink, void* context, record& line) {
            sink(context, line.text, line.length);
            line.clear();
        }
    } // namespace

    void format_panic_context(const panic_process& process, sink_fn sink, void* context) {
        record line{};

        line.add("[PANIC] KERNEL PANIC");
        emit_line(sink, context, line);

        line.add("[PANIC] cpu=");
        line.add_dec(process.cpu);
        line.add(" arch=");
        line.add(process.arch);
        emit_line(sink, context, line);

        if (!process.available) {
            line.add("[PANIC] process unavailable");
            emit_line(sink, context, line);
        }
        else {
            line.add("[PANIC] pid=");
            line.add_dec(process.pid);
            line.add(" tid=");
            line.add_dec(process.tid);
            line.add(" name=\"");
            line.add(process.name);
            line.add("\" state=");
            line.add(process.state);

            if (process.metadata_available) {
                line.add(" uid=");
                line.add_dec(process.uid);
                line.add(" euid=");
                line.add_dec(process.euid);
                line.add(" gid=");
                line.add_dec(process.gid);
                line.add(" egid=");
                line.add_dec(process.egid);
                line.add(" pgrp=");
                line.add_dec(process.pgrp);
                line.add(" session=");
                line.add_dec(process.session);
            }
            else {
                line.add(" uid=unavailable euid=unavailable gid=unavailable "
                         "egid=unavailable pgrp=unavailable session=unavailable");
            }

            emit_line(sink, context, line);
        }

        if (!process.thread_available) {
            line.add("[PANIC] thread unavailable");
            emit_line(sink, context, line);
        }
        else {
            line.add("[PANIC] thread state=");
            line.add(process.thread_state);
            line.add(" kstack-top=");
            line.add_hex(process.kstack_top);
            line.add(" kstack-size=");
            line.add_hex(process.kstack_size);
            line.add(" kstack-high-water=");
            line.add_hex(process.kstack_high_water);
            line.add(" address-space=");
            line.add_hex(process.address_space);
            emit_line(sink, context, line);
        }
    }
} // namespace astia::dbg
