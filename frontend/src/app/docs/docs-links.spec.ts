import { TestBed } from '@angular/core/testing';
import { DOC_PAGES } from '../content';
import { DocsBrowser } from './docs-browser';
import { DocsMarkdownRenderer } from './docs-markdown';
import type { DocPage } from './docs.model';
import {
  DEFAULT_DOCS_REPOSITORY_CONFIG,
  GitHubDocsSource,
} from './docs.repository';

const PAGE: DocPage = {
  slug: 'readme',
  title: 'Project README',
  group: 'Guide',
  summary: 'Build and run.',
  sourcePath: 'README.md',
  contentKind: 'markdown',
};

const SOURCE = new GitHubDocsSource(DEFAULT_DOCS_REPOSITORY_CONFIG);
const RENDERER = new DocsMarkdownRenderer(SOURCE);

const Markdown = [
  '[license](LICENSE)',
  '',
  '[external](https://example.com/docs)',
  '',
  '[build](#build-and-run)',
  '',
  '## Build and run',
].join('\n');

describe('Runtime documentation links', () => {
  beforeEach(async () => {
    await TestBed.configureTestingModule({
      imports: [DocsBrowser],
    }).compileComponents();
  });

  async function render(): Promise<{
    fixture: ReturnType<typeof TestBed.createComponent<DocsBrowser>>;
    element: HTMLElement;
  }> {
    const fixture = TestBed.createComponent(DocsBrowser);
    fixture.componentRef.setInput('pages', [PAGE]);
    fixture.componentRef.setInput('selectedSlug', PAGE.slug);
    fixture.componentRef.setInput('renderedContent', RENDERER.render(Markdown, PAGE.sourcePath));
    fixture.componentRef.setInput('status', 'ready');
    fixture.componentRef.setInput('sourceUrl', SOURCE.sourceUrl(PAGE));
    fixture.detectChanges();
    await fixture.whenStable();
    await Promise.resolve();
    fixture.detectChanges();
    return { fixture, element: fixture.nativeElement as HTMLElement };
  }

  it('renders only safe absolute or in-page links with heading anchors', async () => {
    const { element } = await render();
    const links = [...element.querySelectorAll<HTMLAnchorElement>('.doc-markdown a')];

    expect(links.length).toBe(3);
    expect(element.querySelector('h2#doc-build-and-run')).toBeTruthy();
    expect(
      links.some(
        (link) =>
          link.getAttribute('href') ===
          'https://github.com/arlirad/astia/blob/main/LICENSE',
      ),
    ).toBeTrue();
    expect(
      links.some((link) => link.getAttribute('href') === '#doc-build-and-run'),
    ).toBeTrue();

    for (const link of links) {
      const href = link.getAttribute('href') ?? '';
      expect(
        href.startsWith('http') || href.startsWith('#') || href.startsWith('mailto:'),
      ).toBeTrue();
    }
  });

  it('intercepts in-page anchors without leaving the docs route', async () => {
    const { element } = await render();
    const anchor = element.querySelector<HTMLAnchorElement>(
      '.doc-markdown a[href="#doc-build-and-run"]',
    );
    const scroll = spyOn(Element.prototype, 'scrollIntoView');
    const replace = spyOn(history, 'replaceState');

    anchor?.click();

    expect(scroll).toHaveBeenCalled();
    expect(replace).toHaveBeenCalled();
    expect(replace.calls.mostRecent().args[2] as string).toContain('#doc-build-and-run');
  });

  it('keeps the manifest free of Markdown body snapshots', () => {
    for (const page of DOC_PAGES) {
      expect('content' in page).toBeFalse();
      expect((page as unknown as { content?: string }).content).toBeUndefined();
      expect(page.summary).not.toContain('\n');
    }
  });
});
