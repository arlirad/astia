/*
 * The libc running in ring 3: CRT0 unpacks the kernel's startup frame,
 * TLS-backed errno reads and writes %fs-relative storage, stdio rides
 * the async rings down to fd 1, malloc hands out arena blocks, and the
 * process API traps into the kernel.  Prints one line the boot test
 * greps for; any failed step leaves the boot test with a nonzero exit
 * status and this program's step number in the serial log.
 */
#include <sys/futex.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <sys/wait.h>

#include <errno.h>
#include <dirent.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void* libc_thread_worker(void* opaque) {
    int* value = (int*) opaque;
    *value     = 42;
    errno      = EINTR;
    return (void*) (unsigned long) (errno == EINTR ? 0x1234 : 0x1235);
}

static volatile unsigned int libc_futex_word;
static volatile int          libc_futex_ready;

static void* libc_futex_worker(void* opaque) {
    (void) opaque;
    libc_futex_ready = 1;
    const int rc     = futex_wait((unsigned int*) &libc_futex_word, 0);
    return (void*) (unsigned long) (rc == 0 ? 0x2345 : 0x2346);
}

int main(int argc, char** argv) {
    /* 1: TLS.  errno is _Thread_local, reached at a negative %fs
       offset — only correct if the kernel loaded the thread pointer
       from the image's PT_TLS block. */
    errno = EAGAIN;
    if (errno != EAGAIN)
        return 1;

    /* 2: malloc over the static arena, memset through string.c */
    char* block = malloc(64);
    if (block == NULL)
        return 2;
    memset(block, 0x5A, 64);
    if ((unsigned char) block[63] != 0x5A)
        return 3;
    free(block);

    /* 3: the process API over the direct syscalls */
    const int pid = getpid();
    if (pid <= 0)
        return 4;

    /* 4: process working directory state through chdir/getcwd. */
    {
        char cwd[64];
        if (chdir("/bin") != 0 || getcwd(cwd, sizeof(cwd)) == NULL ||
            strcmp(cwd, "/bin") != 0)
            return 41;
        DIR* bin = opendir("/bin");
        if (bin == NULL || readdir(bin) == NULL) return 42;
        closedir(bin);
        if (chdir("/") != 0) return 43;
        DIR* root = opendir("/");
        if (root == NULL || readdir(root) == NULL) return 44;
        closedir(root);
    }

    /* 5-8: anonymous mmap through the kernel — fresh memory that is
       zeroed, writable, and removable again */
    {
        unsigned char* page =
            mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (page == MAP_FAILED)
            return 5;
        if (page[0] != 0 || page[4095] != 0) /* fresh anon memory is zeroed */
            return 6;
        page[4095] = 0x5A;
        if (page[4095] != 0x5A)
            return 7;
        if (munmap(page, 4096) != 0)
            return 8;
    }

    /* 9-15: file-backed mmap — /sbin/init's bytes captured into a
       private mapping that outlives the descriptor */
    {
        FILE* image = fopen("/sbin/init", "r");
        if (image == NULL)
            return 9;
        unsigned char* page =
            mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE, fileno(image), 0);
        fclose(image); /* the mapping keeps the file description alive */
        if (page == MAP_FAILED)
            return 10;
        if (page[0] != 0x7F || page[1] != 'E' || page[2] != 'L' ||
            page[3] != 'F') /* it really is the ELF image */
            return 11;
        page[0] = 0x42; /* private: the write lands on our copy */
        if (page[0] != 0x42)
            return 12;
        if (munmap(page, 4096) != 0)
            return 13;

        /* the same file, shared this time — a later slice */
        image = fopen("/sbin/init", "r");
        if (image == NULL)
            return 14;
        void* shared = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fileno(image), 0);
        fclose(image);
        if (shared != MAP_FAILED || errno != ENOSYS)
            return 15;
    }

    /* 16-22: fork + copy-on-write.  The child inherits every mapping
       sharing its frames; its write splits one off privately, the
       parent's page keeps its value, and waitpid reaps the child's
       exit status. */
    {
        unsigned char* page =
            mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (page == MAP_FAILED)
            return 16;
        page[0] = 7;

        const int child = fork();
        if (child < 0)
            return 17;
        if (child == 0) {
            /* The inherited view is intact before we write... */
            if (page[0] != 7)
                _Exit(43);
            /* ...and the write lands on our private copy. */
            page[0] = 99;
            if (page[0] != 99)
                _Exit(44);
            _Exit(42);
        }

        int status = 0;
        if (waitpid(child, &status, 0) != child)
            return 18;
        if ((status & 0x7f) != 0 || ((status >> 8) & 0xff) != 42)
            return 19;
        if (page[0] != 7) /* the child's write never touched our page */
            return 20;
        page[0] = 55; /* our own CoW fault regains the write bit */
        if (page[0] != 55)
            return 21;
        if (munmap(page, 4096) != 0)
            return 22;
    }

    /* 23-25: pthread_create/join.  join blocks the caller through the
       kernel futex-backed thread wake path until the worker returns. */
    {
        int       value = 0;
        pthread_t worker;
        errno = EAGAIN;
        if (pthread_create(&worker, NULL, libc_thread_worker, &value) != 0)
            return 23;
        void* result = NULL;
        if (pthread_join(worker, &result) != 0)
            return 24;
        if (value != 42 || result != (void*) (unsigned long) 0x1234 || errno != EAGAIN)
            return 25;
    }

    /* 26-27: futex value comparison and wake ABI. */
    {
        unsigned int word = 0;
        if (futex_wait(&word, 1) != -1 || errno != EAGAIN)
            return 26;
        if (futex_wake(&word, 1) != 0)
            return 27;
    }

    /* 28-30: a real futex block/wake between two userspace threads. */
    {
        libc_futex_word  = 0;
        libc_futex_ready = 0;
        pthread_t worker;
        if (pthread_create(&worker, NULL, libc_futex_worker, NULL) != 0)
            return 28;
        while (!libc_futex_ready)
            sched_yield();
        /* Let the worker publish its waiter before waking it. */
        sched_yield();
        libc_futex_word = 1;
        if (futex_wake((unsigned int*) &libc_futex_word, 1) != 1)
            return 29;
        void* result = NULL;
        if (pthread_join(worker, &result) != 0 ||
            result != (void*) (unsigned long) 0x2345)
            return 30;
    }

    /* 31-35: writable tmpfs plus MAP_SHARED.  The mapping names the
       tmpfs file's physical region directly, so a mapped write is
       immediately visible through a second open description. */
    {
        FILE* shared_file = fopen("/tmp/mmap_shared", "w+");
        if (shared_file == NULL)
            return 31;
        const unsigned char initial = 0x31;
        if (fwrite(&initial, 1, 1, shared_file) != 1)
            return 32;
        if (fflush(shared_file) != 0)
            return 33;
        unsigned char* page =
            mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fileno(shared_file), 0);
        fclose(shared_file);
        if (page == MAP_FAILED)
            return 34;
        page[0] = 0x73;
        if (page[0] != 0x73)
            return 35;
        FILE* verify = fopen("/tmp/mmap_shared", "r");
        if (verify == NULL)
            return 36;
        unsigned char observed = 0;
        if (fread(&observed, 1, 1, verify) != 1 || observed != 0x73)
            return 37;
        fclose(verify);
        if (munmap(page, 4096) != 0)
            return 38;
    }

    /* 39-50: fork + interior munmap + CoW through the reference-less
       tail.  The four-page mapping splits into a left flank carrying
       the address space's backing reference and a one-page tail that
       carries none; the tail then CoWs while the page is still shared
       with the child.  Neither the split nor that fault may release
       the group's reference — the left flank still maps it — and once
       both address spaces are done, the released frames must actually
       be back in the allocator: recycled over by the loop below, which
       would clobber page 0 if it had been freed early. */
    {
        unsigned char* q = mmap(NULL, 4 * 4096, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (q == MAP_FAILED)
            return 39;
        q[0]        = 0x77;
        q[3 * 4096] = 0x88;

        const int child = fork();
        if (child < 0)
            return 40;
        if (child == 0) {
            if (q[0] != 0x77 || q[3 * 4096] != 0x88)
                _Exit(90);
            q[0]        = 0x66; /* CoW through the full-range VMA */
            q[3 * 4096] = 0xBB;
            if (q[0] != 0x66 || q[3 * 4096] != 0xBB)
                _Exit(91);
            _Exit(42);
        }

        /* Interior split: [0,1) keeps the reference, the tail [3,4)
           does not.  The write then CoWs through that tail while the
           page is still shared with the child. */
        if (munmap(q + 4096, 2 * 4096) != 0)
            return 41;
        q[3 * 4096] = 0xAA;
        if (q[3 * 4096] != 0xAA)
            return 42;

        int status = 0;
        if (waitpid(child, &status, 0) != child)
            return 43;
        if ((status & 0x7f) != 0 || ((status >> 8) & 0xff) != 42)
            return 44;
        if (q[3 * 4096] != 0xAA) /* the child's 0xBB stayed private */
            return 45;
        q[0] = 0x55; /* page 0's frame is still ours: in-place CoW */
        if (q[0] != 0x55)
            return 46;

        /* Recycle pressure: fresh mappings must not land on page 0's
           or page 3's frames while we still map them. */
        for (int i = 0; i < 32; ++i) {
            unsigned char* p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (p == MAP_FAILED)
                return 47;
            p[0]    = 0xEE;
            p[4095] = 0xEE;
            if (q[0] != 0x55 || q[3 * 4096] != 0xAA) {
                printf("recycle clobber i=%d p=%p q0=%02x q3=%02x p0=%02x\n", i,
                       (void*) p, q[0], q[3 * 4096], p[0]);
                return 48;
            }
            if (munmap(p, 4096) != 0)
                return 49;
        }
        if (munmap(q, 4 * 4096) != 0)
            return 50;
    }

    /* 52-60: the public scatter/gather pair over a real file.  writev()
       sends two entries; a fresh open description reads them back
       through three, the middle one empty, so the bytes have to land in
       the order the vector named them and the empty entry has to be
       stepped over rather than stopped on.  A further one-byte readv
       comes back 0, which is the descriptor's position having moved by
       exactly what was read.  The last step is the cap the kernel owns:
       a vector longer than its eight entries is refused whole with
       EINVAL, never truncated into a shorter answer. */
    {
        FILE* out = fopen("/tmp/uio", "w+");
        if (out == NULL)
            return 52;
        char first[5];
        char second[4];
        memcpy(first, "alpha", 5);
        memcpy(second, "beta", 4);
        const struct iovec sent[2] = {
            {first, 5},
            {second, 4},
        };
        if (writev(fileno(out), sent, 2) != 9)
            return 53;
        if (fclose(out) != 0)
            return 54;

        FILE* in = fopen("/tmp/uio", "r");
        if (in == NULL)
            return 55;
        char               charter[3] = {0};
        char               middle     = 'x';
        char               last[6]    = {0};
        const struct iovec taken[3]   = {
            {charter, 3},
            {&middle, 0},
            {last, 6},
        };
        if (readv(fileno(in), taken, 3) != 9)
            return 56;
        if (memcmp(charter, "alp", 3) != 0 || middle != 'x' ||
            memcmp(last, "habeta", 6) != 0)
            return 57;

        char               tail_byte = 0;
        const struct iovec tail      = {&tail_byte, 1};
        if (readv(fileno(in), &tail, 1) != 0)
            return 58;

        struct iovec too_many[9] = {0}; /* one past the kernel's cap */
        errno                    = 0;
        if (readv(fileno(in), too_many, 9) != -1 || errno != EINVAL)
            return 59;
        if (fclose(in) != 0)
            return 60;
    }

    /* 51: stdio over the rings; argc/argv arrived through CRT0 from
       the frame the kernel built. */
    printf("astia: libc ok pid=%d argc=%d argv0=%s\n", pid, argc,
           argc > 0 ? argv[0] : "-");
    return 0;
}
