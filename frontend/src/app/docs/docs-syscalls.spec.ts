import { TestBed } from '@angular/core/testing';
import { DOC_PAGES } from '../content';
import { DocsBrowser } from './docs-browser';

const SYSCALL_PAGES = DOC_PAGES.filter((page) => page.group === 'Syscall (2)');
const CANONICAL_SYSCALL_IDS = [
  ...Array.from({ length: 70 }, (_, index) => index),
  82,
];
const RESERVED_SOCKET_IDS = Array.from({ length: 12 }, (_, index) => 70 + index);
const PRIVATE_SOCKET_TITLES = [
  'accept(2)',
  'bind(2)',
  'connect(2)',
  'getsockopt(2)',
  'listen(2)',
  'read(2)',
  'recv(2)',
  'recvfrom(2)',
  'send(2)',
  'sendto(2)',
  'shutdown(2)',
  'socket(2)',
  'socketpair(2)',
  'write(2)',
];

const synopsisOf = (page: (typeof SYSCALL_PAGES)[number]): string => {
  const match = page.content.match(
    /## SYNOPSIS\s+(?:```c\n([\s\S]*?)\n```|([^\n]+))/,
  );
  return (match?.[1] ?? match?.[2] ?? '').trim();
};

describe('Generated section-2 syscall docs', () => {
  beforeEach(async () => {
    await TestBed.configureTestingModule({
      imports: [DocsBrowser],
    }).compileComponents();
  });

  it('contains all 71 canonical syscall pages with stable IDs and slugs', () => {
    expect(SYSCALL_PAGES.length).toBe(CANONICAL_SYSCALL_IDS.length);
    expect(SYSCALL_PAGES.map((page) => page.syscallId).sort((a, b) => (a ?? 0) - (b ?? 0))).toEqual(
      CANONICAL_SYSCALL_IDS,
    );

    const slugs = new Set(SYSCALL_PAGES.map((page) => page.slug));
    expect(slugs.size).toBe(CANONICAL_SYSCALL_IDS.length);
    for (const page of SYSCALL_PAGES) {
      expect(page.slug).toBe(
        `syscall-${page.syscallId}-${page.title.replace(/\(2\)$/, '').replace(/_/g, '-')}`,
      );
    }
  });

  it('omits reserved socket IDs and private socket pages', () => {
    const ids = SYSCALL_PAGES.map((page) => page.syscallId).filter(
      (id): id is number => id !== undefined,
    );

    for (const id of RESERVED_SOCKET_IDS) {
      expect(SYSCALL_PAGES.some((page) => page.syscallId === id)).toBeFalse();
    }
    expect(Math.max(...ids) + 1).toBe(83);

    const titles = SYSCALL_PAGES.map((page) => page.title);
    for (const title of PRIVATE_SOCKET_TITLES) {
      expect(titles).not.toContain(title);
    }
  });

  it('carries real Markdown bodies and authoritative source links', () => {
    const ioSetup = SYSCALL_PAGES.find((page) => page.syscallId === 0);
    const alarm = SYSCALL_PAGES.find((page) => page.syscallId === 55);
    const fchdir = SYSCALL_PAGES.find((page) => page.syscallId === 69);

    expect(ioSetup?.title).toBe('io_setup(2)');
    expect(ioSetup?.content).toContain('## SYNOPSIS');
    expect(ioSetup?.content).toContain('Allocates a ring');
    expect(ioSetup?.category).toBe('fs');
    expect(ioSetup?.source).toBe(
      'https://github.com/arlirad/astia/blob/main/kernel/src/fs/syscalls/io_setup.cpp',
    );

    expect(alarm?.title).toBe('alarm(2)');
    expect(alarm?.content).toContain('Schedule delivery of a process alarm');
    expect(alarm?.source).toContain('kernel/src/proc/syscalls/alarm.cpp');

    expect(fchdir?.title).toBe('fchdir(2)');
    expect(fchdir?.source).toContain('kernel/include/astia/api/syscalls.h');

    const uname = SYSCALL_PAGES.find((page) => page.syscallId === 82);
    expect(uname?.title).toBe('uname(2)');
    expect(uname?.slug).toBe('syscall-82-uname');
    expect(uname?.category).toBe('proc/process');
    expect(uname?.source).toContain('kernel/src/proc/syscalls/process/uname.cpp');
    expect(uname?.content).toContain('## SYNOPSIS');
    expect(uname?.content).toContain('long astia_uname(astia_utsname* output);');
    expect(uname?.content).toContain('`-EFAULT`');
    expect(uname?.content).toContain('`-ESRCH`');

    const sigwait = SYSCALL_PAGES.find((page) => page.syscallId === 50);
    expect(sigwait?.content).toContain(
      'long astia_sigwait(const uint32_t* set, int32_t* signal);',
    );
    expect(sigwait?.content).not.toContain('long sigwait(u64');

    const clockGetTime = SYSCALL_PAGES.find((page) => page.syscallId === 7);
    const clockGetRes = SYSCALL_PAGES.find((page) => page.syscallId === 62);
    expect(clockGetTime?.source).toContain('kernel/include/astia/api/syscalls.h');
    expect(clockGetRes?.source).toContain('kernel/include/astia/api/syscalls.h');
    expect(clockGetRes?.content).toContain('astia_clock_getres');
    expect(clockGetRes?.content).toContain('Resolution is derived from the active timer');
    expect(clockGetTime?.content).toContain('`-EIO`');
    expect(clockGetTime?.content).toContain('active timer source');
    expect(clockGetRes?.content).toContain('`-EIO`');
    expect(clockGetRes?.content).toContain('active timer source');

    const execve = SYSCALL_PAGES.find((page) => page.syscallId === 24);
    expect(execve?.category).toBe('proc/process');
    expect(execve?.source).toContain('libc/src/unistd.c');
    expect(execve?.content).toContain('returns -1 and sets errno');
    expect(execve?.content).not.toContain('negative errno');
    expect(execve?.content).toContain('FD_CLOEXEC');
    expect(execve?.content).toContain('`E2BIG`');
    expect(execve?.content).not.toContain('`-E2BIG`');
    expect(execve?.content).toContain('argv or envp');
    expect(execve?.content).not.toContain('A path, vector, or string pointer is invalid');
    expect(execve?.content).toContain('null path returns EINVAL');
    expect(execve?.content).toContain('`EINVAL`');
    expect(execve?.content).toContain('not a valid ELF executable image');
    expect(execve?.content).toContain('`ENAMETOOLONG`');
    expect(execve?.content).toContain('`ENOEXEC`');
    expect(execve?.content).toContain('The file is empty');
    expect(execve?.content).not.toContain('not a supported executable image');
    expect(execve?.content).toContain('no current process or thread context');
    expect(execve?.content).toContain(
      'The current process or thread could not be retained',
    );
    expect(execve?.content).not.toContain('ESRCH: There is no current process');
    expect(execve?.content).toContain('resolved against the caller');
    expect(execve?.content).toContain('kernel/src/proc/syscalls/process/execve.cpp');
    expect(execve?.content).toContain('kernel/src/elf/exec_loader.cpp');

    const sigsuspend = SYSCALL_PAGES.find((page) => page.syscallId === 25);
    expect(sigsuspend?.category).toBe('proc/signal');
    expect(sigsuspend?.source).toContain('libc/src/signal.c');
    expect(sigsuspend?.content).toContain('Never returns 0');
    expect(sigsuspend?.content).toContain('errno to EINTR');
    expect(sigsuspend?.content).toContain('`EINTR`');
    expect(sigsuspend?.content).not.toContain('`-EINTR`');
    expect(sigsuspend?.content).toContain('kernel/src/proc/syscalls/signal/sigsuspend.cpp');

    const waitpid = SYSCALL_PAGES.find((page) => page.syscallId === 13);
    expect(waitpid?.category).toBe('proc/process');
    expect(waitpid?.source).toContain('kernel/include/astia/api/syscalls.h');
    expect(waitpid?.content).toContain(
      'long astia_waitpid_options(int32_t pid, int32_t* status, uint32_t options);',
    );
    expect(waitpid?.content).toContain('The libc waitpid() wrapper translates that result to -1');
    expect(waitpid?.content).toContain('`-EINTR`');
    expect(waitpid?.content).toContain('unblocked interrupting signal');
    expect(waitpid?.content).toContain('`-EAGAIN`');
    expect(waitpid?.content).toContain('process waiter table');

    for (const page of SYSCALL_PAGES) {
      expect(page.category).toBeTruthy();
      expect(page.source).toMatch(
        /^https:\/\/github\.com\/arlirad\/astia\/blob\/main\/(kernel\/src|kernel\/include|libc)\//,
      );
      expect(page.content).toContain('## DESCRIPTION');
      if (page.content.includes('Detailed manual text has not been added')) {
        const synopsis = page.content.match(/## SYNOPSIS\s+```c\n([\s\S]*?)\n```/)?.[1] ?? '';
        expect(synopsis).toContain('astia_');
        expect(synopsis.trimEnd().endsWith(';')).toBeTrue();
        expect(synopsis).not.toMatch(/\b(?:i64|u64|u32|i32)\b/);
      }
    }
  });

  it('uses public ABI or explicitly raw synopses for all 71 pages', () => {
    for (const page of SYSCALL_PAGES) {
      expect(page.synopsisKind).toBeTruthy();
      const synopsis = synopsisOf(page);
      if (page.synopsisKind === 'raw') {
        expect(synopsis).toContain('Raw kernel ABI');
        expect(synopsis).not.toContain('astia_');
        expect(synopsis).not.toContain('long ');
      } else {
        expect(synopsis).toMatch(/^(long astia_|int |void astia_)/);
        expect(synopsis.endsWith(';')).toBeTrue();
        expect(synopsis).not.toMatch(/\b(?:i64|u64|u32|i32)\b/);
      }
    }

    expect(
      SYSCALL_PAGES.filter((page) => page.synopsisKind === 'raw').map(
        (page) => page.title,
      ),
    ).toEqual(['sigreturn(2)']);

    const exact = (id: number) =>
      synopsisOf(SYSCALL_PAGES.find((page) => page.syscallId === id)!);

    expect(exact(0)).toBe(
      'long astia_io_setup(uint32_t entries, uint32_t flags, astia_rings** out);',
    );
    expect(exact(46)).toBe('long astia_unmount(const char* target);');
    expect(exact(7)).toBe(
      'long astia_clock_gettime(uint32_t clock_id, astia_timespec* out);',
    );
    expect(exact(62)).toBe(
      'long astia_clock_getres(uint32_t clock_id, astia_timespec* out);',
    );
    expect(exact(50)).toBe(
      'long astia_sigwait(const uint32_t* set, int32_t* signal);',
    );
    expect(exact(24)).toBe(
      'int execve(const char* path, char* const argv[], char* const envp[]);',
    );
    expect(exact(25)).toBe('int sigsuspend(const sigset_t* mask);');
    expect(exact(13)).toBe(
      'long astia_waitpid_options(int32_t pid, int32_t* status, uint32_t options);',
    );
    expect(exact(82)).toBe('long astia_uname(astia_utsname* output);');
  });

  it('renders a syscall page through the existing Markdown parser', async () => {
    const fixture = TestBed.createComponent(DocsBrowser);
    fixture.componentRef.setInput('pages', DOC_PAGES);
    fixture.componentRef.setInput('selectedSlug', 'syscall-0-io-setup');
    fixture.detectChanges();
    await fixture.whenStable();

    const element = fixture.nativeElement as HTMLElement;
    const headings = [...element.querySelectorAll('.doc-markdown h2')].map((node) =>
      node.textContent?.trim(),
    );

    expect(element.querySelector('.docs-page-title')?.textContent).toContain('io_setup(2)');
    expect(headings).toContain('SYNOPSIS');
    expect(element.querySelector('.doc-markdown pre code')?.textContent).toContain(
      'long astia_io_setup',
    );
    expect(element.querySelector('.docs-source')?.getAttribute('href')).toContain(
      'kernel/src/fs/syscalls/io_setup.cpp',
    );
    expect(element.querySelectorAll('.docs-page-top .tag').length).toBeGreaterThanOrEqual(3);
  });

  it('filters syscall pages by title', () => {
    const fixture = TestBed.createComponent(DocsBrowser);
    fixture.componentRef.setInput('pages', DOC_PAGES);
    fixture.detectChanges();

    const input = fixture.nativeElement.querySelector(
      '.docs-filter input',
    ) as HTMLInputElement;
    input.value = 'sigwait';
    input.dispatchEvent(new Event('input', { bubbles: true }));
    fixture.detectChanges();

    expect(fixture.nativeElement.querySelectorAll('.docs-index-link').length).toBe(1);
    expect(
      fixture.nativeElement.querySelector('.docs-index-link')?.textContent,
    ).toContain('sigwait(2)');
  });
});
