import { DocsMarkdownRenderer } from './docs-markdown';
import {
  DEFAULT_DOCS_REPOSITORY_CONFIG,
  GitHubDocsSource,
} from './docs.repository';

const source = new GitHubDocsSource(DEFAULT_DOCS_REPOSITORY_CONFIG);
const renderer = new DocsMarkdownRenderer(source);

describe('DocsMarkdownRenderer', () => {
  it('rewrites relative links and images against the source path', () => {
    const html = renderer.render(
      [
        '# Project README',
        '',
        '[testing](docs/ctest.md)',
        '',
        '![banner](img/banner.svg "Logo")',
      ].join('\n'),
      'README.md',
    );

    expect(html).not.toContain('<h1');
    expect(html).toContain(
      'href="https://github.com/arlirad/astia/blob/main/docs/ctest.md"',
    );
    expect(html).toContain(
      'src="https://raw.githubusercontent.com/arlirad/astia/main/img/banner.svg"',
    );
    expect(html).toContain('alt="banner"');
    expect(html).toContain('title="Logo"');
  });

  it('keeps safe external links and rewrites heading anchors', () => {
    const html = renderer.render(
      [
        '[external](https://example.com/docs)',
        '',
        '[jump](#build-and-run)',
        '',
        '## Build and run',
      ].join('\n'),
      'README.md',
    );

    expect(html).toContain('href="https://example.com/docs"');
    expect(html).toContain('href="#doc-build-and-run"');
    expect(html).toContain('id="doc-build-and-run"');
  });

  it('drops unsafe URLs and raw HTML', () => {
    const html = renderer.render(
      [
        '[bad](javascript:alert(1))',
        '',
        '<script>alert(1)</script>',
        '',
        '<img src="x" onerror="alert(1)">',
        '',
        '## Safe',
      ].join('\n'),
      'README.md',
    );

    expect(html).not.toContain('javascript:');
    expect(html).not.toContain('<script');
    expect(html).not.toContain('onerror');
    expect(html).toContain('bad');
    expect(html).toContain('Safe');
  });

  it('renders source pages as a fenced code block', () => {
    const html = renderer.render(
      '## REPOSITORY SOURCE\n\n```cpp\nlong astia_uname(astia_utsname* output);\n```',
      'kernel/src/proc/syscalls/process/uname.cpp',
    );

    expect(html).toContain('<h2 id="doc-repository-source">REPOSITORY SOURCE</h2>');
    expect(html).toContain(
      'long astia_uname(astia_utsname* output);',
    );
  });
});
