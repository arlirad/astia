import { TestBed } from '@angular/core/testing';
import { DOC_PAGES } from '../content';
import { DocsBrowser } from './docs-browser';
import { DocsMarkdownRenderer } from './docs-markdown';
import type { DocPage } from './docs.model';
import {
  DEFAULT_DOCS_REPOSITORY_CONFIG,
  GitHubDocsSource,
} from './docs.repository';

const SYSCALL_PAGES = DOC_PAGES.filter((page) => page.group === 'Syscall (2)');
const CANONICAL_SYSCALL_IDS = [
  ...Array.from({ length: 71 }, (_, index) => index),
];

describe('Runtime section-2 syscall docs', () => {
  beforeEach(async () => {
    await TestBed.configureTestingModule({
      imports: [DocsBrowser],
    }).compileComponents();
  });

  async function renderAuthoredSyscall(
    syscallId: number,
    markdown: string,
  ): Promise<{
    page: DocPage;
    element: HTMLElement;
    headings: (string | undefined)[];
  }> {
    const page = SYSCALL_PAGES.find((candidate) => candidate.syscallId === syscallId)!;
    const fetchSpy = jasmine
      .createSpy('fetch')
      .and.returnValue(Promise.resolve(new Response(markdown, { status: 200 })));
    const source = new GitHubDocsSource(
      DEFAULT_DOCS_REPOSITORY_CONFIG,
      fetchSpy as unknown as typeof fetch,
    );

    const loaded = await source.load(page);
    expect(fetchSpy.calls.mostRecent().args[0]).toBe(
      `https://raw.githubusercontent.com/arlirad/astia/main/docs/2/${page.slug.replace(/^syscall-\d+-/, '').replace(/-/g, '_')}.md`,
    );

    const fixture = TestBed.createComponent(DocsBrowser);
    fixture.componentRef.setInput('pages', SYSCALL_PAGES);
    fixture.componentRef.setInput('selectedSlug', page.slug);
    fixture.componentRef.setInput(
      'renderedContent',
      new DocsMarkdownRenderer(source).render(loaded, page.sourcePath),
    );
    fixture.componentRef.setInput('status', 'ready');
    fixture.componentRef.setInput('sourceUrl', source.sourceUrl(page));
    fixture.detectChanges();
    await fixture.whenStable();

    const element = fixture.nativeElement as HTMLElement;
    const headings = [...element.querySelectorAll('.doc-markdown h2')].map((node) =>
      node.textContent?.trim(),
    );
    return { page, element, headings };
  }

  it('contains all 71 canonical syscall pages with stable IDs and slugs', () => {
    expect(SYSCALL_PAGES.length).toBe(CANONICAL_SYSCALL_IDS.length);
    expect(
      SYSCALL_PAGES.map((page) => page.syscallId).sort((a, b) => (a ?? 0) - (b ?? 0)),
    ).toEqual(CANONICAL_SYSCALL_IDS);

    const slugs = new Set(SYSCALL_PAGES.map((page) => page.slug));
    expect(slugs.size).toBe(CANONICAL_SYSCALL_IDS.length);
    for (const page of SYSCALL_PAGES) {
      expect(page.slug).toBe(
        `syscall-${page.syscallId}-${page.title.replace(/\(2\)$/, '').replace(/_/g, '-')}`,
      );
    }
  });

  it('keeps authored page paths and authoritative implementation metadata', () => {
    const ioSetup = SYSCALL_PAGES.find((page) => page.syscallId === 0);
    const alarm = SYSCALL_PAGES.find((page) => page.syscallId === 55);
    const fchdir = SYSCALL_PAGES.find((page) => page.syscallId === 69);
    const uname = SYSCALL_PAGES.find((page) => page.syscallId === 70);

    expect(ioSetup?.title).toBe('io_setup(2)');
    expect(ioSetup?.category).toBe('fs');
    expect(ioSetup?.contentKind).toBe('markdown');
    expect(ioSetup?.sourcePath).toBe('docs/2/io_setup.md');
    expect(ioSetup?.implementationPath).toBe('kernel/src/fs/syscalls/io_setup.cpp');
    expect(ioSetup?.synopsis).toBe(
      'long astia_io_setup(uint32_t entries, uint32_t flags, astia_rings** out);',
    );

    expect(alarm?.title).toBe('alarm(2)');
    expect(alarm?.sourcePath).toBe('docs/2/alarm.md');
    expect(alarm?.implementationPath).toBe('kernel/src/proc/syscalls/alarm.cpp');

    expect(fchdir?.title).toBe('fchdir(2)');
    expect(fchdir?.sourcePath).toBe('docs/2/fchdir.md');
    expect(fchdir?.implementationPath).toBe('kernel/include/astia/api/syscalls.h');

    expect(uname?.title).toBe('uname(2)');
    expect(uname?.slug).toBe('syscall-70-uname');
    expect(uname?.category).toBe('proc/process');
    expect(uname?.sourcePath).toBe('docs/2/uname.md');
    expect(uname?.implementationPath).toBe('kernel/src/proc/syscalls/process/uname.cpp');
    expect(uname?.synopsisKind).toBe('public');
    expect(uname?.synopsis).toBe('long astia_uname(astia_utsname* output);');

    const sigreturn = SYSCALL_PAGES.find((page) => page.syscallId === 29);
    expect(sigreturn?.synopsisKind).toBe('raw');
    expect(sigreturn?.synopsis).toContain('Raw kernel ABI');

    for (const page of SYSCALL_PAGES) {
      expect(page.sourcePath).toMatch(/^docs\/2\/[a-z0-9_]+\.md$/);
      expect(page.contentKind).toBe('markdown');
      expect(page.implementationPath).toMatch(/^(kernel\/src|kernel\/include|libc)\//);
      expect(page.synopsisKind).toBeTruthy();
      expect('content' in page).toBeFalse();
    }

    expect(
      SYSCALL_PAGES.filter((page) => page.synopsisKind === 'raw').map(
        (page) => page.title,
      ),
    ).toEqual(['sigreturn(2)']);
  });

  it('preserves exact implementation paths and expected shared source groups', () => {
    const counts = new Map<string, number>();
    for (const page of SYSCALL_PAGES) {
      counts.set(page.implementationPath!, (counts.get(page.implementationPath!) ?? 0) + 1);
    }
    const shared = Object.fromEntries([...counts].filter(([, count]) => count > 1));

    expect(shared).toEqual({
      'kernel/include/astia/api/syscalls.h': 26,
      'kernel/src/proc/process.hpp': 8,
      'kernel/src/proc/proc.cpp': 2,
    });
    expect(new Set(SYSCALL_PAGES.map((page) => page.sourcePath)).size).toBe(71);
    expect(
      new Set(SYSCALL_PAGES.map((page) => page.implementationPath)).size,
    ).toBe(38);
    expect(SYSCALL_PAGES.find((page) => page.syscallId === 24)?.implementationPath).toBe(
      'libc/src/unistd.c',
    );
    expect(SYSCALL_PAGES.find((page) => page.syscallId === 25)?.implementationPath).toBe(
      'libc/src/signal.c',
    );
    expect(SYSCALL_PAGES.find((page) => page.syscallId === 13)?.implementationPath).toBe(
      'kernel/include/astia/api/syscalls.h',
    );
  });

  it('preserves canonical titles, categories, synopses, and public/raw kinds', () => {
    const categories = new Set(
      SYSCALL_PAGES.map((page) => page.category).filter((value): value is string => !!value),
    );
    expect(categories.size).toBeGreaterThan(0);
    for (const page of SYSCALL_PAGES) {
      const name = (page.slug ?? '').replace(/^syscall-\d+-/, '').replace(/-/g, '_');
      expect(page.title).toBe(`${name}(2)`);
      expect(page.sourcePath).toBe(`docs/2/${name}.md`);
      expect(page.category).toBeTruthy();
      expect(page.synopsis).toBeTruthy();
      expect(page.aliases).toBeUndefined();
    }

    expect(SYSCALL_PAGES.find((page) => page.syscallId === 0)?.synopsis).toBe(
      'long astia_io_setup(uint32_t entries, uint32_t flags, astia_rings** out);',
    );
    expect(SYSCALL_PAGES.find((page) => page.syscallId === 13)?.synopsis).toBe(
      'long astia_waitpid_options(int32_t pid, int32_t* status, uint32_t options);',
    );
    expect(SYSCALL_PAGES.find((page) => page.syscallId === 24)?.synopsis).toBe(
      'int execve(const char* path, char* const argv[], char* const envp[]);',
    );
    expect(SYSCALL_PAGES.find((page) => page.syscallId === 25)?.synopsis).toBe(
      'int sigsuspend(const sigset_t* mask);',
    );
    expect(SYSCALL_PAGES.find((page) => page.syscallId === 70)?.synopsis).toBe(
      'long astia_uname(astia_utsname* output);',
    );
    expect(
      SYSCALL_PAGES.filter((page) => page.synopsisKind === 'raw').map((page) => page.syscallId),
    ).toEqual([29]);
  });

  it('fetches and renders syscall-0 authored Markdown with an implementation source link', async () => {
    const markdown = [
      '# io_setup(2)',
      '',
      '## SYNOPSIS',
      '',
      '```c',
      'long astia_io_setup(uint32_t entries, uint32_t flags, astia_rings** out);',
      '```',
      '',
      '## DESCRIPTION',
      '',
      'Create an asynchronous I/O ring.',
      '',
      '## RETURN VALUE',
      '',
      'Returns zero on success.',
      '',
      '## ERRORS',
      '',
      '- `-EINVAL`: Invalid entries or flags.',
    ].join('\n');
    const { page, element, headings } = await renderAuthoredSyscall(0, markdown);

    expect(page.sourcePath).toBe('docs/2/io_setup.md');
    expect(element.querySelector('.docs-page-title')?.textContent).toContain(
      'io_setup(2)',
    );
    expect(headings).toContain('DESCRIPTION');
    expect(headings).toContain('RETURN VALUE');
    expect(headings).toContain('ERRORS');
    expect(headings).not.toContain('REPOSITORY SOURCE');
    expect(element.querySelector('.doc-markdown')?.textContent).toContain(
      'Create an asynchronous I/O ring.',
    );
    expect(element.querySelector('.docs-source')?.getAttribute('href')).toBe(
      'https://github.com/arlirad/astia/blob/main/kernel/src/fs/syscalls/io_setup.cpp',
    );
    expect(element.querySelectorAll('.docs-page-top .tag').length).toBeGreaterThanOrEqual(3);
  });

  it('fetches and renders syscall-70 uname authored Markdown without repository source', async () => {
    const markdown = [
      '# uname(2)',
      '',
      '## SYNOPSIS',
      '',
      '```c',
      'long astia_uname(astia_utsname* output);',
      '```',
      '',
      '## DESCRIPTION',
      '',
      'Read the system identity.',
      '',
      '## RETURN VALUE',
      '',
      'Returns zero on success or a negative errno.',
      '',
      '## ERRORS',
      '',
      '- `-EFAULT`: Output is not writable userspace memory.',
    ].join('\n');
    const { page, element, headings } = await renderAuthoredSyscall(70, markdown);

    expect(page.slug).toBe('syscall-70-uname');
    expect(page.sourcePath).toBe('docs/2/uname.md');
    expect(element.querySelector('.docs-page-title')?.textContent).toContain(
      'uname(2)',
    );
    expect(headings).toContain('DESCRIPTION');
    expect(headings).toContain('RETURN VALUE');
    expect(headings).toContain('ERRORS');
    expect(headings).not.toContain('REPOSITORY SOURCE');
    expect(element.querySelector('.docs-source')?.getAttribute('href')).toBe(
      'https://github.com/arlirad/astia/blob/main/kernel/src/proc/syscalls/process/uname.cpp',
    );
  });

  it('filters syscall pages by title', () => {
    const fixture = TestBed.createComponent(DocsBrowser);
    fixture.componentRef.setInput('pages', SYSCALL_PAGES);
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
