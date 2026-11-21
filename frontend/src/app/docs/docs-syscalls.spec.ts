import { TestBed } from '@angular/core/testing';
import { DOC_PAGES } from '../content';
import { DocsBrowser } from './docs-browser';

const SYSCALL_PAGES = DOC_PAGES.filter((page) => page.group === 'Syscall (2)');

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

  it('contains all 70 canonical syscall pages with stable IDs and slugs', () => {
    expect(SYSCALL_PAGES.length).toBe(70);
    expect(SYSCALL_PAGES.map((page) => page.syscallId).sort((a, b) => (a ?? 0) - (b ?? 0))).toEqual(
      Array.from({ length: 70 }, (_, index) => index),
    );

    const slugs = new Set(SYSCALL_PAGES.map((page) => page.slug));
    expect(slugs.size).toBe(70);
    for (const page of SYSCALL_PAGES) {
      expect(page.slug).toBe(
        `syscall-${page.syscallId}-${page.title.replace(/\(2\)$/, '').replace(/_/g, '-')}`,
      );
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
      'https://github.com/arlirad/astia/blob/main/kernel/src/fs/syscalls/io_setup.hpp',
    );

    expect(alarm?.title).toBe('alarm(2)');
    expect(alarm?.content).toContain('System call handler for `alarm`');
    expect(alarm?.source).toContain('kernel/src/proc/syscalls/alarm.cpp');

    expect(fchdir?.title).toBe('fchdir(2)');
    expect(fchdir?.source).toContain('kernel/src/proc/syscalls/process/fchdir.cpp');

    const sigwait = SYSCALL_PAGES.find((page) => page.syscallId === 50);
    expect(sigwait?.content).toContain(
      'long astia_sigwait(const uint32_t* set, int32_t* signal);',
    );
    expect(sigwait?.content).not.toContain('long sigwait(u64');

    const clockGetTime = SYSCALL_PAGES.find((page) => page.syscallId === 7);
    const clockGetRes = SYSCALL_PAGES.find((page) => page.syscallId === 62);
    expect(clockGetTime?.source).toContain('kernel/src/sys/time.cpp');
    expect(clockGetRes?.source).toContain('kernel/src/sys/time.cpp');
    expect(clockGetRes?.content).toContain('astia_clock_getres');
    expect(clockGetRes?.content).toContain('Detailed manual text has not been added');

    for (const page of SYSCALL_PAGES) {
      expect(page.category).toBeTruthy();
      expect(page.source).toMatch(
        /^https:\/\/github\.com\/arlirad\/astia\/blob\/main\/(kernel\/src|kernel\/include)\//,
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

  it('uses public ABI or explicitly raw synopses for all 70 pages', () => {
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
      'kernel/src/fs/syscalls/io_setup.hpp',
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
