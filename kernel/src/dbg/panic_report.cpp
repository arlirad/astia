#include <astia/dbg/panic_report.hpp>

#include "record.hpp"

namespace astia::dbg {
    void format_panic_context(const panic_process& process, sink_fn sink, void* context) {
        record line{};

        line.add("kpanic: cpu=");
        line.add_dec(process.cpu);
        line.add(" arch=");
        line.add(process.arch);
        sink(context, line.text, line.length);

        line.clear();

        if (!process.available) {
            line.add("kpanic: proc unavailable");
        }
        else {
            line.add("kpanic: proc pid=");
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
        }

        sink(context, line.text, line.length);

        line.clear();

        if (!process.thread_available) {
            line.add("kpanic: thread unavailable");
        }
        else {
            line.add("kpanic: thread state=");
            line.add(process.thread_state);
            line.add(" kstack-top=");
            line.add_hex(process.kstack_top);
            line.add(" kstack-size=");
            line.add_hex(process.kstack_size);
            line.add(" kstack-high-water=");
            line.add_hex(process.kstack_high_water);
            line.add(" cpu=");
            line.add_dec(process.cpu);
            line.add(" address-space=");
            line.add_hex(process.address_space);
        }

        sink(context, line.text, line.length);
    }
} // namespace astia::dbg
