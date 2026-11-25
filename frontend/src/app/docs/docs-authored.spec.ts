import { DOC_PAGES } from '../content';
import { DocsMarkdownRenderer } from './docs-markdown';
import type { DocPage } from './docs.model';
import {
  DEFAULT_DOCS_REPOSITORY_CONFIG,
  GitHubDocsSource,
} from './docs.repository';

const SECTION7 = {
  aio: ['docs-aio-7', 'docs-7-aio', 'docs/7/aio.md', 'Asynchronous I/O'],
  alarm: ['docs-alarm-7', 'docs-7-alarm', 'docs/7/alarm.md', 'Process alarm'],
  exec: ['docs-exec-7', 'docs-7-exec', 'docs/7/exec.md', 'Asynchronous ELF loading and execve'],
  rcu: ['docs-rcu-7', 'docs-7-rcu', 'docs/7/rcu.md', 'RCU and lockless constraints'],
  sigtimedwait: [
    'docs-sigtimedwait-7',
    'docs-7-sigtimedwait',
    'docs/7/sigtimedwait.md',
    'sigtimedwait(2)',
  ],
  spawnve: [
    'docs-spawnve-7',
    'docs-7-spawnve',
    'docs/7/spawnve.md',
    'Asynchronous process spawn and spawnve',
  ],
  task: ['docs-task-7', 'docs-7-task', 'docs/7/task.md', 'Kernel coroutine tasks'],
} as const;

const SOURCE = new GitHubDocsSource(DEFAULT_DOCS_REPOSITORY_CONFIG);
const MARKDOWN = new DocsMarkdownRenderer(SOURCE);

const markdownPage = (): DocPage => ({
  slug: 'authored-io-setup',
  title: 'io_setup(2)',
  group: 'Syscall (2)',
  summary: 'Create an asynchronous I/O ring.',
  sourcePath: 'docs/2/fs/io_setup.md',
  contentKind: 'markdown',
});

const syscallPage = (): DocPage => ({
  slug: 'syscall-0-io-setup',
  title: 'io_setup(2)',
  group: 'Syscall (2)',
  summary: 'Create an asynchronous I/O ring.',
  sourcePath: 'docs/2/fs/io_setup.md',
  contentKind: 'markdown',
  implementationPath: 'kernel/src/fs/syscalls/io_setup.cpp',
  synopsisKind: 'public',
  synopsis: 'long astia_io_setup(uint32_t entries, uint32_t flags, astia_rings** out);',
});

const fetchResponse = (body: string, status = 200): Response => new Response(body, { status });
const fetchMock = (
  handler: (input: RequestInfo | URL) => Promise<Response>,
): typeof fetch => handler as typeof fetch;

describe('Authored documentation sources', () => {
  it('keeps exactly seven overview pages on stable slugs with aliases', () => {
    const overviews = DOC_PAGES.filter((page) => page.group === 'Manual page');
    expect(overviews.length).toBe(7);
    for (const page of overviews) {
      const expected = SECTION7[page.slug.replace('docs-', '').replace('-7', '') as keyof typeof SECTION7];
      expect(expected).withContext(page.slug).toBeDefined();
      expect(page.slug).toBe(expected[0]);
      expect(page.aliases).toEqual([expected[1]]);
      expect(page.sourcePath).toBe(expected[2]);
      expect(page.title).toBe(expected[3]);
      expect(page.contentKind).toBe('markdown');
    }
  });

  it('does not migrate design notes or the aarch64 drift', () => {
    expect(DOC_PAGES.some((page) => page.slug === 'docs-aarch64-virt-boot')).toBeFalse();
    expect(DOC_PAGES.some((page) => page.sourcePath === 'docs/aarch64-virt-boot.md')).toBeFalse();
    expect(DOC_PAGES.filter((page) => page.group === 'Manual page').length).toBe(7);

    const syscalls = DOC_PAGES.filter((page) => page.group === 'Syscall (2)');
    expect(syscalls.length).toBe(73);
    for (const page of syscalls) {
      const name = page.title.replace(/\(2\)$/, '');
      const folder = page.subsystem === 'proc' ? `proc/${page.category}/` : `${page.subsystem}/`;
      expect(page.sourcePath).toBe(`docs/2/${folder}${name}.md`);
      expect(page.contentKind).toBe('markdown');
      expect(page.implementationPath).toBeTruthy();
      expect(page.subsystem).toBeTruthy();
      expect(page.category).toBeTruthy();
    }
    expect(
      syscalls.filter((page) => page.subsystem === 'dl').map((page) => page.slug),
    ).toEqual(['syscall-71-dlmap', 'syscall-72-dlunmap']);

    for (const page of DOC_PAGES) {
      expect('content' in page).toBeFalse();
      expect((page as unknown as { content?: string }).content).toBeUndefined();
      expect(page.summary).not.toContain('\n');
    }
  });

  it('resolves both docs/2 and docs/7 authoritative paths through GitHub', async () => {
    const calls: string[] = [];
    const source = new GitHubDocsSource(
      DEFAULT_DOCS_REPOSITORY_CONFIG,
      fetchMock((input) => {
        calls.push(String(input));
        return Promise.resolve(fetchResponse('# Page\n\nBody'));
      }),
    );

    await source.load(markdownPage());
    const aio = DOC_PAGES.find((page) => page.slug === 'docs-aio-7')!;
    await source.load(aio);

    expect(calls[0]).toBe(
      'https://raw.githubusercontent.com/arlirad/astia/main/docs/2/fs/io_setup.md',
    );
    expect(calls[1]).toBe(
      'https://raw.githubusercontent.com/arlirad/astia/main/docs/7/aio.md',
    );
    expect(source.sourceUrl(aio)).toBe(
      'https://github.com/arlirad/astia/blob/main/docs/7/aio.md',
    );
  });

  it('loads authored syscall Markdown and links the implementation source', async () => {
    const body = [
      '# io_setup(2)',
      '',
      '## DESCRIPTION',
      '',
      'Create an asynchronous I/O ring.',
      '',
      '## RETURN VALUE',
      '',
      'Returns zero.',
      '',
      '## ERRORS',
      '',
      '- `-EINVAL`: Invalid arguments.',
    ].join('\n');
    const source = new GitHubDocsSource(
      DEFAULT_DOCS_REPOSITORY_CONFIG,
      fetchMock(() => Promise.resolve(fetchResponse(body))),
    );

    const markdown = await source.load(syscallPage());
    expect(markdown).toBe(body);
    expect(markdown).toContain('## DESCRIPTION');
    expect(markdown).not.toContain('## REPOSITORY SOURCE');
    expect(source.sourceUrl(syscallPage())).toBe(
      'https://github.com/arlirad/astia/blob/main/kernel/src/fs/syscalls/io_setup.cpp',
    );
  });

  it('resolves relative links and anchors from docs/7 through the nested docs/2 tree', () => {
    const markdown = [
      '[syscall](../2/fs/io_setup.md#synopsis)',
      '',
      '[external](https://example.com)',
      '',
      '[unsafe](javascript:alert(1))',
      '',
      '## OPERATION REGISTRY',
    ].join('\n');
    const html = MARKDOWN.render(markdown, 'docs/7/aio.md');

    expect(html).toContain(
      'href="https://github.com/arlirad/astia/blob/main/docs/2/fs/io_setup.md#synopsis"',
    );
    expect(html).toContain('href="https://example.com"');
    expect(html).toContain('id="doc-operation-registry"');
    expect(html).not.toContain('javascript:');
    expect(html).toContain('unsafe');
  });

  it('resolves relative links and anchors from nested docs/2 source paths', () => {
    const markdown = [
      '[overview](../../../7/aio.md#operation-registry)',
      '',
      '[sibling](../process/uname.md)',
      '',
      '## SYNOPSIS REGISTRY',
    ].join('\n');
    const html = MARKDOWN.render(markdown, 'docs/2/proc/core/getpid.md');

    expect(html).toContain(
      'href="https://github.com/arlirad/astia/blob/main/docs/7/aio.md#operation-registry"',
    );
    expect(html).toContain(
      'href="https://github.com/arlirad/astia/blob/main/docs/2/proc/process/uname.md"',
    );
    expect(html).toContain('id="doc-synopsis-registry"');
  });
});
