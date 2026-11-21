import { TestBed } from '@angular/core/testing';
import { DocsBrowser } from './docs-browser';
import type { DocPage } from './docs.model';

const PAGES: readonly DocPage[] = [
  {
    slug: 'guide',
    title: 'Guide',
    group: 'Guide',
    summary: 'Build and run.',
    source: 'https://github.com/arlirad/astia/blob/main/README.md',
    content: '# Guide\n\nBuild and run.\n\n## Steps\n\n- one\n- two',
  },
  {
    slug: 'manual',
    title: 'Manual',
    group: 'Manual page',
    summary: 'API reference.',
    source: 'https://github.com/arlirad/astia/blob/main/docs/aio.7.md',
    content: '# Manual\n\nAPI reference.',
  },
  {
    slug: 'note',
    title: 'Design note',
    group: 'Design note',
    summary: 'Boundary notes.',
    source: 'https://github.com/arlirad/astia/blob/main/docs/architecture-boundaries.md',
    content: '# Design note\n\nBoundary notes.',
  },
];

describe('DocsBrowser', () => {
  beforeEach(async () => {
    await TestBed.configureTestingModule({
      imports: [DocsBrowser],
    }).compileComponents();
  });

  function render(): {
    fixture: ReturnType<typeof TestBed.createComponent<DocsBrowser>>;
    element: HTMLElement;
  } {
    const fixture = TestBed.createComponent(DocsBrowser);
    fixture.componentRef.setInput('pages', PAGES);
    fixture.detectChanges();
    return { fixture, element: fixture.nativeElement as HTMLElement };
  }

  it('renders the grouped documentation index', () => {
    const { element } = render();

    expect(element.querySelectorAll('.docs-group').length).toBe(3);
    expect(element.querySelectorAll('.docs-index-link').length).toBe(PAGES.length);
    expect(element.querySelector('.docs-count')?.textContent).toContain('3 pages');
  });

  it('filters the index by title, group, or summary', () => {
    const { fixture, element } = render();
    const input = element.querySelector<HTMLInputElement>('.docs-filter input');

    if (input) {
      input.value = 'manual';
      input.dispatchEvent(new Event('input', { bubbles: true }));
    }
    fixture.detectChanges();

    expect(element.querySelectorAll('.docs-index-link').length).toBe(1);
    expect(element.querySelector('.docs-index-link')?.textContent).toContain('Manual');

    if (input) {
      input.value = 'no such page';
      input.dispatchEvent(new Event('input', { bubbles: true }));
    }
    fixture.detectChanges();

    expect(element.querySelectorAll('.docs-index-link').length).toBe(0);
    expect(element.querySelector('.docs-empty')?.textContent).toContain('No docs match');
  });

  it('opens a page, follows page navigation, and returns to the index', () => {
    const { fixture, element } = render();
    const links = element.querySelectorAll<HTMLButtonElement>('.docs-index-link');

    links[1].click();
    fixture.detectChanges();

    expect(element.querySelector('.docs-page-title')?.textContent).toContain('Manual');
    expect(element.querySelector('.docs-source')?.getAttribute('href')).toContain('docs/aio.7.md');
    expect(element.querySelector('.docs-pager-link')?.textContent).toContain('Guide');

    const next = element.querySelector<HTMLButtonElement>('.docs-pager-link.is-next');
    next?.click();
    fixture.detectChanges();

    expect(element.querySelector('.docs-page-title')?.textContent).toContain('Design note');

    element.querySelector<HTMLButtonElement>('.docs-back')?.click();
    fixture.detectChanges();

    expect(element.querySelector('.doc-markdown')).toBeNull();
    expect(element.querySelectorAll('.docs-index-link').length).toBe(PAGES.length);
  });
});
