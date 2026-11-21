import { TestBed } from '@angular/core/testing';
import { DOC_PAGES } from '../content';
import { DocsBrowser } from './docs-browser';

const RELATIVE_LINK =
  /(?<!!)\[[^\]]*\]\((?!https?:|mailto:|tel:|#|\/\/)([^)\s]+)\)/;

describe('Bundled documentation links', () => {
  beforeEach(async () => {
    await TestBed.configureTestingModule({
      imports: [DocsBrowser],
    }).compileComponents();
  });

  it('rewrites repository-relative Markdown links to GitHub', () => {
    for (const page of DOC_PAGES) {
      expect(page.content).not.toMatch(RELATIVE_LINK);
    }
  });

  it('preserves external URLs, anchors, and rewrites repository paths', () => {
    const readme = DOC_PAGES.find((page) => page.slug === 'readme');

    expect(readme?.content).toContain('https://github.com/arlirad/astia/issues');
    expect(readme?.content).toContain('](#build-and-run)');
    expect(readme?.content).toContain(
      'https://github.com/arlirad/astia/blob/main/LICENSE',
    );
    expect(readme?.content).toContain(
      'https://github.com/arlirad/astia/blob/main/docs/ctest.md',
    );
    expect(readme?.content).toContain(
      'https://github.com/arlirad/astia/blob/main/toolchain/README.md',
    );
    expect(readme?.content).toContain(
      'https://github.com/arlirad/astia/blob/main/modules/README.md',
    );
    expect(readme?.content).toContain(
      'https://github.com/arlirad/astia/blob/main/tools/man/DOCUMENTING.md',
    );
  });

  it('renders safe absolute or in-page links with heading anchors', async () => {
    const fixture = TestBed.createComponent(DocsBrowser);
    fixture.componentRef.setInput('pages', DOC_PAGES);
    fixture.componentRef.setInput('selectedSlug', 'readme');
    fixture.detectChanges();
    await fixture.whenStable();

    const element = fixture.nativeElement as HTMLElement;
    const links = [...element.querySelectorAll<HTMLAnchorElement>('.doc-markdown a')];

    expect(links.length).toBeGreaterThan(0);
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
    const fixture = TestBed.createComponent(DocsBrowser);
    fixture.componentRef.setInput('pages', DOC_PAGES);
    fixture.componentRef.setInput('selectedSlug', 'readme');
    fixture.detectChanges();
    await fixture.whenStable();

    const element = fixture.nativeElement as HTMLElement;
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
});
