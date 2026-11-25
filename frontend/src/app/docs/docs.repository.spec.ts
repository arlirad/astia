import type { DocPage } from './docs.model';
import {
  DEFAULT_DOCS_REPOSITORY_CONFIG,
  DocsFetchError,
  DocsNotFoundError,
  GitHubDocsSource,
} from './docs.repository';

const page = (overrides: Partial<DocPage> = {}): DocPage => ({
  slug: 'readme',
  title: 'Project README',
  group: 'Guide',
  summary: 'Build and run.',
  sourcePath: 'README.md',
  contentKind: 'markdown',
  ...overrides,
});

const fetchMock = (
  handler: (input: RequestInfo | URL, init?: RequestInit) => Promise<Response>,
): typeof fetch => handler as typeof fetch;

const response = (body: string, status = 200): Response => new Response(body, { status });

describe('GitHubDocsSource', () => {
  it('builds public raw and source URLs from the central repository config', () => {
    const source = new GitHubDocsSource(DEFAULT_DOCS_REPOSITORY_CONFIG);

    expect(source.rawUrl('README.md')).toBe(
      'https://raw.githubusercontent.com/arlirad/astia/main/README.md',
    );
    expect(source.rawUrl('docs/ctest.md')).toBe(
      'https://raw.githubusercontent.com/arlirad/astia/main/docs/ctest.md',
    );
    expect(source.sourceUrl(page())).toBe(
      'https://github.com/arlirad/astia/blob/main/README.md',
    );
  });

  it('prefers the authoritative implementation path for source links', () => {
    const source = new GitHubDocsSource(DEFAULT_DOCS_REPOSITORY_CONFIG);

    expect(
      source.sourceUrl(
        page({
          sourcePath: 'docs/2/fs/io_setup.md',
          implementationPath: 'kernel/src/fs/syscalls/io_setup.cpp',
        }),
      ),
    ).toBe(
      'https://github.com/arlirad/astia/blob/main/kernel/src/fs/syscalls/io_setup.cpp',
    );
  });

  it('switches every URL to a different ref through one config change', () => {
    const source = new GitHubDocsSource({
      ...DEFAULT_DOCS_REPOSITORY_CONFIG,
      ref: 'v1.2.0',
    });

    expect(source.rawUrl('docs/ctest.md')).toBe(
      'https://raw.githubusercontent.com/arlirad/astia/v1.2.0/docs/ctest.md',
    );
    expect(source.sourceUrl(page())).toBe(
      'https://github.com/arlirad/astia/blob/v1.2.0/README.md',
    );
    expect(source.resolveLink('docs/ctest.md', 'README.md')).toBe(
      'https://github.com/arlirad/astia/blob/v1.2.0/docs/ctest.md',
    );
  });

  it('preserves slash-separated refs as path segments', () => {
    const source = new GitHubDocsSource({
      ...DEFAULT_DOCS_REPOSITORY_CONFIG,
      ref: 'release/v1',
    });

    expect(source.rawUrl('README.md')).toBe(
      'https://raw.githubusercontent.com/arlirad/astia/release/v1/README.md',
    );
  });

  it('loads Markdown from the public raw endpoint', async () => {
    const fetchSpy = jasmine
      .createSpy('fetch')
      .and.returnValue(Promise.resolve(response('# Title\n\nBody')));
    const source = new GitHubDocsSource(
      DEFAULT_DOCS_REPOSITORY_CONFIG,
      fetchSpy as unknown as typeof fetch,
    );

    const markdown = await source.load(page());

    expect(markdown).toBe('# Title\n\nBody');
    expect(fetchSpy).toHaveBeenCalledWith(
      'https://raw.githubusercontent.com/arlirad/astia/main/README.md',
      { headers: { Accept: 'text/plain' } },
    );
  });

  it('reports a missing source as a not-found error', async () => {
    const source = new GitHubDocsSource(
      DEFAULT_DOCS_REPOSITORY_CONFIG,
      fetchMock(() => Promise.resolve(response('not found', 404))),
    );

    await expectAsync(source.load(page())).toBeRejectedWithError(DocsNotFoundError);
  });

  it('reports a server failure as a fetch error with its HTTP status', async () => {
    const source = new GitHubDocsSource(
      DEFAULT_DOCS_REPOSITORY_CONFIG,
      fetchMock(() => Promise.resolve(response('unavailable', 503))),
    );

    try {
      await source.load(page());
      fail('expected load to reject');
    } catch (error) {
      expect(error).toBeInstanceOf(DocsFetchError);
      expect((error as DocsFetchError).status).toBe(503);
    }
  });

  it('reports a network failure as a fetch error', async () => {
    const source = new GitHubDocsSource(
      DEFAULT_DOCS_REPOSITORY_CONFIG,
      fetchMock(() => Promise.reject(new TypeError('Failed to fetch'))),
    );

    const error = await source.load(page()).catch((reason: unknown) => reason);

    expect(error).toBeInstanceOf(DocsFetchError);
    expect((error as DocsFetchError).status).toBeUndefined();
  });

  it('wraps source files in a safe fenced code block', async () => {
    const body = 'int main() { const char* fence = "```"; return 0; }';
    const source = new GitHubDocsSource(
      DEFAULT_DOCS_REPOSITORY_CONFIG,
      fetchMock(() => Promise.resolve(response(body))),
    );

    const markdown = await source.load(
      page({
        slug: 'syscall-0-io-setup',
        sourcePath: 'kernel/src/fs/syscalls/io_setup.cpp',
        contentKind: 'source',
        synopsisKind: 'public',
        synopsis: 'long astia_io_setup(uint32_t entries, uint32_t flags, astia_rings** out);',
      }),
    );

    expect(markdown).toContain('## SYNOPSIS');
    expect(markdown).toContain(
      'long astia_io_setup(uint32_t entries, uint32_t flags, astia_rings** out);',
    );
    expect(markdown).toContain('## REPOSITORY SOURCE');
    expect(markdown).toContain('````cpp');
    expect(markdown).toContain(body);
  });

  it('resolves repository-relative links against the configured ref', () => {
    const source = new GitHubDocsSource(DEFAULT_DOCS_REPOSITORY_CONFIG);

    expect(source.resolveLink('docs/ctest.md', 'README.md')).toBe(
      'https://github.com/arlirad/astia/blob/main/docs/ctest.md',
    );
    expect(source.resolveLink('../kernel/README.md', 'docs/ctest.md')).toBe(
      'https://github.com/arlirad/astia/blob/main/kernel/README.md',
    );
    expect(source.resolveLink('kernel', 'README.md')).toBe(
      'https://github.com/arlirad/astia/tree/main/kernel',
    );
    expect(source.resolveLink('LICENSE', 'README.md')).toBe(
      'https://github.com/arlirad/astia/blob/main/LICENSE',
    );
    expect(source.resolveLink('docs/ctest.md?plain=1#testing', 'README.md')).toBe(
      'https://github.com/arlirad/astia/blob/main/docs/ctest.md?plain=1#testing',
    );
  });

  it('keeps safe external links and rejects unsafe protocols or traversal', () => {
    const source = new GitHubDocsSource(DEFAULT_DOCS_REPOSITORY_CONFIG);

    expect(source.resolveLink('https://example.com/docs', 'README.md')).toBe(
      'https://example.com/docs',
    );
    expect(source.resolveLink('mailto:docs@example.com', 'README.md')).toBe(
      'mailto:docs@example.com',
    );
    expect(source.resolveLink('#build', 'README.md')).toBe('#build');
    expect(source.resolveLink('javascript:alert(1)', 'README.md')).toBeNull();
    expect(source.resolveLink('//example.com/x', 'README.md')).toBeNull();
    expect(source.resolveLink('../../outside.md', 'README.md')).toBeNull();
  });

  it('resolves images to raw repository URLs and rejects unsafe sources', () => {
    const source = new GitHubDocsSource(DEFAULT_DOCS_REPOSITORY_CONFIG);

    expect(source.resolveImage('img/banner.png', 'README.md')).toBe(
      'https://raw.githubusercontent.com/arlirad/astia/main/img/banner.png',
    );
    expect(source.resolveImage('https://example.com/banner.png', 'README.md')).toBe(
      'https://example.com/banner.png',
    );
    expect(source.resolveImage('data:image/png;base64,AAAA', 'README.md')).toBeNull();
    expect(source.resolveImage('../../outside.png', 'README.md')).toBeNull();
  });
});
