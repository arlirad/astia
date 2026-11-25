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
    sourcePath: 'docs/7/aio.md',
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
  manual: 'https://github.com/arlirad/astia/blob/main/docs/7/aio.md',
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
    fixture.detectChanges();
  }

  const rows = (element: HTMLElement): HTMLElement[] => [
    ...element.querySelectorAll<HTMLElement>('[role="treeitem"]'),
  ];

  async function press(
    fixture: ReturnType<typeof TestBed.createComponent<DocsBrowser>>,
    key: string,
  ): Promise<void> {
    const target = document.activeElement;
    target?.dispatchEvent(new KeyboardEvent('keydown', { key, bubbles: true }));
    await settle(fixture);
  }

  it('renders an accessible tree of documentation groups', () => {
    const { element } = render();
    const tree = element.querySelector('[role="tree"]');
    const treeRows = rows(element);

    expect(tree).toBeTruthy();
    expect(tree?.getAttribute('aria-describedby')).toBe('docs-tree-help');
    expect(element.querySelector('#docs-tree-help')?.textContent).toContain(
      'Arrow keys move',
    );
    expect(treeRows.length).toBe(3);
    expect(
      treeRows.map(
        (row) => row.querySelector('.docs-tree-label')?.textContent?.trim(),
      ),
    ).toEqual(['Guide', 'Manual page', 'Design note']);
    expect(
      treeRows.map(
        (row) => row.querySelector('.docs-group-count')?.textContent?.trim(),
      ),
    ).toEqual(['1', '1', '1']);
    expect(treeRows[0].getAttribute('aria-level')).toBe('1');
    expect(treeRows[0].getAttribute('aria-setsize')).toBe('3');
    expect(treeRows[0].getAttribute('aria-posinset')).toBe('1');
    expect(treeRows[0].getAttribute('aria-expanded')).toBe('false');
    expect(treeRows[0].getAttribute('aria-selected')).toBe('false');
    expect(treeRows[0].tabIndex).toBe(0);
    expect(treeRows[1].tabIndex).toBe(-1);
    expect(element.querySelectorAll('.docs-index-link').length).toBe(0);
    expect(element.querySelector('.docs-taxonomy')).toBeNull();
    expect(element.querySelectorAll('.docs-index select').length).toBe(0);
    expect(element.querySelector('.docs-count')?.textContent).toContain('3 pages');
    expect(element.querySelector('.docs-count')?.textContent).toContain(
      'matching descendant pages',
    );
  });

  it('toggles groups with an accessible treeitem and a roving tabindex', async () => {
    const { fixture, element } = render();
    const toggle = element.querySelector<HTMLElement>('.docs-group-toggle');

    expect(toggle?.tagName).toBe('LI');
    expect(toggle?.getAttribute('role')).toBe('treeitem');
    expect(toggle?.getAttribute('aria-expanded')).toBe('false');

    toggle?.click();
    await settle(fixture);

    expect(toggle?.getAttribute('aria-expanded')).toBe('true');
    expect(element.querySelectorAll('.docs-index-link').length).toBe(1);
    expect(rows(element).length).toBe(4);

    toggle?.click();
    await settle(fixture);

    expect(toggle?.getAttribute('aria-expanded')).toBe('false');
    expect(element.querySelectorAll('.docs-index-link').length).toBe(0);
  });

  it('supports arrow, Home, End, Enter, and Space navigation', async () => {
    const { fixture, element } = render();
    const first = rows(element)[0];
    first.focus();

    await press(fixture, 'ArrowDown');
    expect(document.activeElement).toBe(rows(element)[1]);

    await press(fixture, 'End');
    expect(document.activeElement).toBe(rows(element)[2]);

    await press(fixture, 'Home');
    expect(document.activeElement).toBe(rows(element)[0]);

    await press(fixture, ' ');
    expect(rows(element)[0].getAttribute('aria-expanded')).toBe('true');
    await press(fixture, ' ');
    expect(rows(element)[0].getAttribute('aria-expanded')).toBe('false');

    await press(fixture, 'ArrowRight');
    expect(rows(element)[0].getAttribute('aria-expanded')).toBe('true');

    await press(fixture, 'ArrowDown');
    expect(document.activeElement).toBe(rows(element)[1]);
    expect(rows(element)[1].getAttribute('aria-level')).toBe('2');

    await press(fixture, 'ArrowLeft');
    expect(rows(element)[0].getAttribute('aria-expanded')).toBe('true');
    expect(document.activeElement).toBe(rows(element)[0]);

    await press(fixture, 'ArrowLeft');
    expect(rows(element)[0].getAttribute('aria-expanded')).toBe('false');

    const selected: string[] = [];
    fixture.componentInstance.pageSelected.subscribe((slug) => selected.push(slug));

    await press(fixture, 'ArrowRight');
    await press(fixture, 'ArrowDown');
    await press(fixture, 'Enter');
    expect(selected).toEqual(['guide']);

    await press(fixture, 'ArrowLeft');
    await press(fixture, 'ArrowLeft');
    expect(rows(element)[0].getAttribute('aria-expanded')).toBe('false');
  });

  it('renders a deep-linked page, opens its ancestors, and marks selection', async () => {
    const { element } = render('manual');

    expect(element.querySelector('.docs-page-title')?.textContent).toContain('Manual');
    expect(element.querySelector('.doc-markdown')?.textContent).toContain('API reference.');

    const open = rows(element).filter(
      (row) => row.getAttribute('aria-expanded') === 'true',
    );
    expect(open.length).toBe(1);
    expect(open[0].textContent).toContain('Manual page');

    const selected = element.querySelector('.docs-index-link.is-active');
    expect(selected?.getAttribute('aria-selected')).toBe('true');
    expect(selected?.getAttribute('aria-current')).toBe('page');
  });

  it('reveals matching ancestors and exact counts when filtering', async () => {
    const { fixture, element } = render();
    const input = element.querySelector<HTMLInputElement>('.docs-filter input');

    if (input) {
      input.value = 'manual';
      input.dispatchEvent(new Event('input', { bubbles: true }));
    }
    await settle(fixture);

    expect(element.querySelectorAll('.docs-index-link').length).toBe(1);
    expect(element.querySelector('.docs-index-link')?.textContent).toContain('Manual');
    expect(
      element.querySelectorAll('.docs-group-toggle[aria-expanded="true"]').length,
    ).toBe(1);
    expect(element.querySelector('.docs-count')?.textContent).toContain('1 pages');

    if (input) {
      input.value = 'no such page';
      input.dispatchEvent(new Event('input', { bubbles: true }));
    }
    await settle(fixture);

    expect(element.querySelectorAll('.docs-index-link').length).toBe(0);
    expect(element.querySelector('.docs-empty')?.textContent).toContain('No docs match');
    expect(element.querySelector('.docs-count')?.textContent).toContain('0 pages');
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

  it('retains the filter and fold state while opening a result and returning', async () => {
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

  it('retries without losing the filter or the fold state', async () => {
    const { fixture, element } = render();
    const input = element.querySelector<HTMLInputElement>('.docs-filter input');
    let retried = false;
    fixture.componentInstance.retryRequested.subscribe(() => (retried = true));

    if (input) {
      input.value = 'manual';
      input.dispatchEvent(new Event('input', { bubbles: true }));
    }
    await settle(fixture);
    select(fixture, 'manual');
    await settle(fixture);

    fixture.componentRef.setInput('status', 'error');
    fixture.componentRef.setInput('errorMessage', 'HTTP 503');
    fixture.detectChanges();

    element.querySelector<HTMLButtonElement>('.docs-state button')?.click();
    expect(retried).toBeTrue();

    fixture.componentRef.setInput('status', 'ready');
    fixture.componentRef.setInput('renderedContent', CONTENT['manual']);
    await settle(fixture);

    expect(input?.value).toBe('manual');
    expect(element.querySelectorAll('.docs-index-link').length).toBe(1);
    expect(element.querySelector('.doc-markdown')?.textContent).toContain('API reference.');
  });

  it('emits page selection, page navigation, and index requests', () => {
    const { fixture, element } = render();
    const component = fixture.componentInstance;
    const selected: string[] = [];
    let indexRequested = false;
    component.pageSelected.subscribe((slug) => selected.push(slug));
    component.indexSelected.subscribe(() => (indexRequested = true));

    element.querySelectorAll<HTMLElement>('.docs-group-toggle')[1].click();
    fixture.detectChanges();
    element.querySelectorAll<HTMLElement>('.docs-index-link')[0].click();
    expect(selected).toEqual(['manual']);

    select(fixture, 'manual');
    fixture.detectChanges();

    expect(element.querySelector('.docs-page-title')?.textContent).toContain('Manual');
    expect(element.querySelector('.docs-source')?.getAttribute('href')).toContain('docs/7/aio.md');
    expect(element.querySelector('.docs-pager-link')?.textContent).toContain('Guide');

    element.querySelector<HTMLButtonElement>('.docs-pager-link.is-next')?.click();
    expect(selected).toEqual(['manual', 'note']);

    element.querySelector<HTMLButtonElement>('.docs-back')?.click();
    expect(indexRequested).toBeTrue();
  });
});
