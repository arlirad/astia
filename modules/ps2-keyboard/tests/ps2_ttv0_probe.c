#include <astia/api/aio.h>
#include <astia/api/syscalls.h>

static astia_rings* ring;
static uint64_t     next_tag = 1;

static long transact(uint32_t op, int32_t fd, uint32_t op_flags, uint64_t offset,
                     const void* address, uint32_t length) {
    astia_sqe* sqe = astia_sqe_slot(ring);
    if (sqe == 0) {
        if (astia_io_submit(ring, 0) < 0)
            return -1;

        sqe = astia_sqe_slot(ring);
    }

    if (sqe == 0)
        return -1;

    const uint64_t tag = next_tag++;

    sqe->op        = op;
    sqe->flags     = 0;
    sqe->fd        = fd;
    sqe->op_flags  = op_flags;
    sqe->offset    = offset;
    sqe->address   = (uint64_t) (uintptr_t) address;
    sqe->length    = length;
    sqe->aux       = 0;
    sqe->user_data = tag;

    astia_sq_publish(ring, 1);

    if (astia_io_submit(ring, 1) < 0)
        return -1;

    for (;;) {
        astia_cqe* cqe = astia_cq_peek(ring);
        if (cqe == 0) {
            (void) astia_yield();
            continue;
        }

        const uint64_t seen = cqe->user_data;

        const long result = cqe->result;

        astia_cq_consume(ring, 1);

        if (seen == tag)
            return result;
    }
}

static int put(const char* text, uint32_t length) {
    return transact(OP_WRITE, STDOUT, 0, 0, text, length) == (long) length;
}

static int put_text(const char* text) {
    uint32_t length = 0;

    while (text[length] != '\0') {
        ++length;
    }

    return put(text, length);
}

static void exit_with(int status) {
    astia_exit(status);

    for (;;) {
        ;
    }
}

static int read_line(int fd, char* buffer, uint32_t capacity) {
    uint32_t used = 0;

    while (used + 1 < capacity) {
        const long result =
            transact(OP_READ, fd, 0, 0, buffer + used, capacity - 1 - used);
        if (result <= 0)
            return -1;

        used += (uint32_t) result;

        if (buffer[used - 1] == '\n')
            break;
    }

    while (used != 0 && (buffer[used - 1] == '\n' || buffer[used - 1] == '\r')) {
        --used;
    }

    buffer[used] = '\0';

    return (int) used;
}

__attribute__((used)) static void start(void) {
    if (astia_io_setup(8, 0, &ring) != 0 || ring == 0)
        exit_with(11);

    const long terminal = transact(OP_OPENAT, -1, O_RDWR, 0, "/dev/ttv0", 0);
    if (terminal < 0) {
        (void) put_text("PS2_TTV0:open-failed\n");
        exit_with(12);
    }

    if (!put_text("PS2_READY\n"))
        exit_with(14);

    char      line[64];
    const int length = read_line((int) terminal, line, sizeof line);
    if (length < 0) {
        (void) put_text("PS2_KEYS:read-failed\n");
        exit_with(13);
    }

    if (!put_text("PS2_KEYS:") || !put(line, (uint32_t) length) || !put_text("\n"))
        exit_with(14);

    exit_with(0);
}

__asm__(".globl _start\n"
        "_start:\n"
        "    andq $-16, %rsp\n"
        "    call start\n"
        "    ud2\n");
