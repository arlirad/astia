import { TestBed } from '@angular/core/testing';
import { DocsBrowser } from './docs-browser';
import type { DocPage } from './docs.model';

const PAGES: readonly DocPage[] = [
  {
    slug: 'guide',
    title: 'Guide',
    group: 'Guide',
    summary: 'Build and run.',
    sourcePath: 'README.md',
    contentKind: 'markdown',
  },
  {
    slug: 'manual',
    title: 'Manual',
    group: 'Manual page',
    summary: 'API reference.',
    sourcePath: 'docs/aio.7.md',
    contentKind: 'markdown',
  },
  {
    slug: 'note',
    title: 'Design note',
    group: 'Design note',
    summary: 'Boundary notes.',
    sourcePath: 'docs/architecture-boundaries.md',
    contentKind: 'markdown',
  },
];

const CONTENT: Readonly<Record<string, string>> = {
  guide:
    '<h2 id="doc-steps">Steps</h2><ul><li>one</li><li>two</li></ul>',
  manual: '<p>API reference.</p>',
  note: '<p>Boundary notes.</p>',
};

const SOURCE_URLS: Readonly<Record<string, string>> = {
  guide: 'https://github.com/arlirad/astia/blob/main/README.md',
  manual: 'https://github.com/arlirad/astia/blob/main/docs/aio.7.md',
  note: 'https://github.com/arlirad/astia/blob/main/docs/architecture-boundaries.md',
};

describe('DocsBrowser', () => {
  beforeEach(async () => {
    await TestBed.configureTestingModule({
      imports: [DocsBrowser],
    }).compileComponents();
  });

  function render(selectedSlug: string | null = null): {
    fixture: ReturnType<typeof TestBed.createComponent<DocsBrowser>>;
    element: HTMLElement;
  } {
    const fixture = TestBed.createComponent(DocsBrowser);
    fixture.componentRef.setInput('pages', PAGES);
    fixture.componentRef.setInput('selectedSlug', selectedSlug);
    if (selectedSlug) {
      fixture.componentRef.setInput('renderedContent', CONTENT[selectedSlug]);
      fixture.componentRef.setInput('status', 'ready');
      fixture.componentRef.setInput('sourceUrl', SOURCE_URLS[selectedSlug]);
    }
    fixture.detectChanges();
    return { fixture, element: fixture.nativeElement as HTMLElement };
  }

  function select(
    fixture: ReturnType<typeof TestBed.createComponent<DocsBrowser>>,
    slug: string | null,
  ): void {
    fixture.componentRef.setInput('selectedSlug', slug);
    fixture.componentRef.setInput('renderedContent', slug ? CONTENT[slug] : '');
    fixture.componentRef.setInput('status', slug ? 'ready' : 'idle');
    fixture.componentRef.setInput('sourceUrl', slug ? SOURCE_URLS[slug] : null);
  }

  async function settle(
    fixture: ReturnType<typeof TestBed.createComponent<DocsBrowser>>,
  ): Promise<void> {
    fixture.detectChanges();
    await fixture.whenStable();
    await Promise.resolve();
  }

  it('renders foldable documentation categories', () => {
    const { element } = render();

    expect(element.querySelectorAll('.docs-group').length).toBe(3);
    expect(element.querySelectorAll('.docs-group-toggle').length).toBe(3);
    expect(element.querySelectorAll('.docs-index-link').length).toBe(PAGES.length);
    expect(element.querySelector('.docs-count')?.textContent).toContain('3 pages');
    expect(element.querySelectorAll('.docs-group-toggle[aria-expanded="true"]').length).toBe(0);
    expect(element.querySelectorAll('.docs-group ul:not([hidden])').length).toBe(0);
  });

  it('toggles categories with an accessible button and aria state', () => {
    const { fixture, element } = render();
    const toggle = element.querySelector<HTMLButtonElement>('.docs-group-toggle');
    const list = element.querySelector<HTMLElement>('.docs-group ul');

    expect(toggle?.tagName).toBe('BUTTON');
    expect(toggle?.tabIndex).toBe(0);
    expect(toggle?.getAttribute('aria-expanded')).toBe('false');
    expect(toggle?.getAttribute('aria-controls')).toBe(list?.id);
    expect(list?.hidden).toBeTrue();

    toggle?.click();
    fixture.detectChanges();

    expect(toggle?.getAttribute('aria-expanded')).toBe('true');
    expect(list?.hidden).toBeFalse();

    toggle?.click();
    fixture.detectChanges();

    expect(toggle?.getAttribute('aria-expanded')).toBe('false');
    expect(list?.hidden).toBeTrue();
  });

  it('renders a deep-linked page and opens its category', () => {
    const { element } = render('manual');

    expect(element.querySelector('.docs-page-title')?.textContent).toContain('Manual');
    expect(element.querySelector('.doc-markdown')?.textContent).toContain('API reference.');

    const groups = [...element.querySelectorAll<HTMLElement>('.docs-group')];
    const open = groups.filter((group) => !group.querySelector('ul')?.hidden);
    expect(open.length).toBe(1);
    expect(open[0].textContent).toContain('Manual page');
  });

  it('reveals matching categories and results when filtering', () => {
    const { fixture, element } = render();
    const input = element.querySelector<HTMLInputElement>('.docs-filter input');

    if (input) {
      input.value = 'manual';
      input.dispatchEvent(new Event('input', { bubbles: true }));
    }
    fixture.detectChanges();

    expect(element.querySelectorAll('.docs-index-link').length).toBe(1);
    expect(element.querySelector('.docs-index-link')?.textContent).toContain('Manual');
    expect(element.querySelectorAll('.docs-group-toggle[aria-expanded="true"]').length).toBe(1);
    expect(element.querySelectorAll('.docs-group ul:not([hidden])').length).toBe(1);

    if (input) {
      input.value = 'no such page';
      input.dispatchEvent(new Event('input', { bubbles: true }));
    }
    fixture.detectChanges();

    expect(element.querySelectorAll('.docs-index-link').length).toBe(0);
    expect(element.querySelector('.docs-empty')?.textContent).toContain('No docs match');
  });

  it('moves focus to the page heading after selection', async () => {
    const { fixture, element } = render();

    select(fixture, 'manual');
    await settle(fixture);

    const title = element.querySelector<HTMLElement>('.docs-page-title');
    expect(document.activeElement).toBe(title);
    expect(title?.getAttribute('tabindex')).toBe('-1');
    expect(element.querySelectorAll('.docs-group-toggle[aria-expanded="true"]').length).toBe(1);
  });

  it('retains the filter while opening a result and returning to the index', async () => {
    const { fixture, element } = render();
    const input = element.querySelector<HTMLInputElement>('.docs-filter input');

    if (input) {
      input.value = 'manual';
      input.dispatchEvent(new Event('input', { bubbles: true }));
    }
    await settle(fixture);

    select(fixture, 'manual');
    await settle(fixture);

    expect(input?.value).toBe('manual');
    expect(element.querySelectorAll('.docs-index-link').length).toBe(1);
    expect(element.querySelector('.docs-page-title')?.textContent).toContain('Manual');

    select(fixture, null);
    await settle(fixture);

    expect(input?.value).toBe('manual');
    expect(element.querySelectorAll('.docs-index-link').length).toBe(1);
    expect(document.activeElement).toBe(input);
  });

  it('shows loading, not-found, and error states for the selected page', () => {
    const { fixture, element } = render('manual');

    fixture.componentRef.setInput('status', 'loading');
    fixture.componentRef.setInput('renderedContent', '');
    fixture.detectChanges();
    expect(element.querySelector('[role="status"]')?.textContent).toContain('Loading');
    expect(element.querySelector('.doc-markdown')).toBeNull();

    fixture.componentRef.setInput('status', 'not-found');
    fixture.detectChanges();
    expect(element.querySelector('[role="alert"]')?.textContent).toContain('not available');

    fixture.componentRef.setInput('status', 'error');
    fixture.componentRef.setInput('errorMessage', 'HTTP 503');
    fixture.detectChanges();
    expect(element.querySelector('[role="alert"]')?.textContent).toContain('HTTP 503');
    expect(element.querySelector('.docs-state button')?.textContent).toContain('Retry');
  });

  it('emits a retry request from the error state', () => {
    const { fixture, element } = render('manual');
    const component = fixture.componentInstance;
    let retried = false;
    component.retryRequested.subscribe(() => (retried = true));

    fixture.componentRef.setInput('status', 'error');
    fixture.componentRef.setInput('errorMessage', 'HTTP 503');
    fixture.detectChanges();

    element.querySelector<HTMLButtonElement>('.docs-state button')?.click();
    expect(retried).toBeTrue();
  });

  it('emits page selection, page navigation, and index requests', () => {
    const { fixture, element } = render();
    const component = fixture.componentInstance;
    const selected: string[] = [];
    let indexRequested = false;
    component.pageSelected.subscribe((slug) => selected.push(slug));
    component.indexSelected.subscribe(() => (indexRequested = true));

    element.querySelectorAll<HTMLButtonElement>('.docs-index-link')[1].click();
    expect(selected).toEqual(['manual']);

    select(fixture, 'manual');
    fixture.detectChanges();

    expect(element.querySelector('.docs-page-title')?.textContent).toContain('Manual');
    expect(element.querySelector('.docs-source')?.getAttribute('href')).toContain('docs/aio.7.md');
    expect(element.querySelector('.docs-pager-link')?.textContent).toContain('Guide');

    element.querySelector<HTMLButtonElement>('.docs-pager-link.is-next')?.click();
    expect(selected).toEqual(['manual', 'note']);

    element.querySelector<HTMLButtonElement>('.docs-back')?.click();
    expect(indexRequested).toBeTrue();
  });
});
