import { TestBed } from '@angular/core/testing';
import { DOC_PAGES } from '../content';
import { DocsBrowser } from './docs-browser';
import type { DocPage } from './docs.model';
import { DOC_SYSCALLS_KEY } from './docs-tree';

const SYSCALL_PAGES = DOC_PAGES.filter((page) => page.group === 'Syscall (2)');

const SUBSYSTEM_COUNTS: Readonly<Record<string, number>> = {
  proc: 40,
  fs: 6,
  abi: 25,
  dl: 2,
  mod: 4,
};

const CATEGORY_COUNTS: Readonly<Record<string, number>> = {
  core: 9,
  ipc: 1,
  memory: 2,
  process: 18,
  signal: 4,
  thread: 6,
  fs: 6,
  abi: 25,
  dl: 2,
  mod: 4,
};

const row = (element: HTMLElement, key: string): HTMLElement | null =>
  element.querySelector<HTMLElement>(`[data-tree-key="${key}"]`);

const rowLabel = (element: HTMLElement): string =>
  element.querySelector('.docs-tree-label')?.textContent?.trim() ?? '';

const rowCount = (element: HTMLElement): string =>
  element.querySelector('.docs-group-count')?.textContent?.trim() ?? '';

describe('DocsBrowser taxonomy', () => {
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
    fixture.componentRef.setInput('pages', DOC_PAGES);
    if (selectedSlug) {
      const page = DOC_PAGES.find((candidate) => candidate.slug === selectedSlug) as DocPage;
      fixture.componentRef.setInput('selectedSlug', selectedSlug);
      fixture.componentRef.setInput('status', 'ready');
      fixture.componentRef.setInput('renderedContent', `<p>${page.title}</p>`);
      fixture.componentRef.setInput('sourceUrl', null);
    }
    fixture.detectChanges();
    return { fixture, element: fixture.nativeElement as HTMLElement };
  }

  async function settle(
    fixture: ReturnType<typeof TestBed.createComponent<DocsBrowser>>,
  ): Promise<void> {
    fixture.detectChanges();
    await fixture.whenStable();
    await Promise.resolve();
    fixture.detectChanges();
  }

  async function press(
    fixture: ReturnType<typeof TestBed.createComponent<DocsBrowser>>,
    key: string,
  ): Promise<void> {
    document.activeElement?.dispatchEvent(
      new KeyboardEvent('keydown', { key, bubbles: true }),
    );
    await settle(fixture);
  }

  it('derives exact taxonomy counts and renders them in the tree without filter controls', () => {
    expect(SYSCALL_PAGES.length).toBe(77);
    const subsystems = new Map<string, number>();
    const categories = new Map<string, number>();
    for (const page of SYSCALL_PAGES) {
      const subsystem = page.subsystem ?? '';
      const category = page.category ?? '';
      subsystems.set(subsystem, (subsystems.get(subsystem) ?? 0) + 1);
      categories.set(category, (categories.get(category) ?? 0) + 1);
    }
    expect(Object.fromEntries(subsystems)).toEqual(SUBSYSTEM_COUNTS);
    expect(Object.fromEntries(categories)).toEqual(CATEGORY_COUNTS);

    const { element } = render();
    expect(element.querySelector('.docs-taxonomy')).toBeNull();
    expect(element.querySelector('.docs-subsystem-filter')).toBeNull();
    expect(element.querySelector('.docs-category-filter')).toBeNull();
    expect(element.querySelectorAll('.docs-index select').length).toBe(0);

    const topLevel = [
      ...element.querySelectorAll<HTMLElement>('[role="treeitem"][aria-level="1"]'),
    ];
    expect(topLevel.map(rowLabel)).toEqual([
      'Syscalls (2)',
      'Guide',
      'Design (7)',
      'Design note',
    ]);
    expect(topLevel.map(rowCount)).toEqual(['77', '6', '7', '17']);
    expect(topLevel[0].getAttribute('aria-expanded')).toBe('false');
    expect(topLevel[0].getAttribute('aria-setsize')).toBe('4');
    expect(topLevel[0].getAttribute('aria-posinset')).toBe('1');
    expect(element.querySelectorAll('[role="treeitem"]').length).toBe(4);
    expect(element.querySelectorAll('.docs-index-link').length).toBe(0);
    expect(element.querySelector('.docs-count')?.textContent).toContain(
      `${DOC_PAGES.length} pages`,
    );
  });

  it('filters the seven section-7 overview pages by their displayed group label', async () => {
    const { fixture, element } = render();
    const input = element.querySelector<HTMLInputElement>('.docs-filter input');

    if (input) {
      input.value = 'design (7)';
      input.dispatchEvent(new Event('input', { bubbles: true }));
    }
    await settle(fixture);

    expect(element.querySelectorAll('[role="treeitem"][aria-level="1"]').length).toBe(1);
    expect(rowLabel(row(element, 'group:Manual page')!)).toBe('Design (7)');
    expect(rowCount(row(element, 'group:Manual page')!)).toBe('7');
    expect(row(element, 'group:Manual page')?.getAttribute('aria-expanded')).toBe('true');
    expect(element.querySelectorAll('.docs-index-link').length).toBe(7);
    expect(element.querySelector('.docs-count')?.textContent).toContain('7 pages');
    expect(row(element, 'group:Design note')).toBeNull();
  });

  it('nests proc, fs, abi, dl, and mod under one expandable Syscalls node', () => {
    const { fixture, element } = render();
    const syscalls = row(element, DOC_SYSCALLS_KEY);

    expect(syscalls?.getAttribute('role')).toBe('treeitem');
    expect(syscalls?.getAttribute('aria-level')).toBe('1');
    expect(syscalls?.getAttribute('aria-expanded')).toBe('false');
    expect(rowLabel(syscalls!)).toBe('Syscalls (2)');
    expect(rowCount(syscalls!)).toBe('77');

    syscalls?.click();
    fixture.detectChanges();

    expect(syscalls?.getAttribute('aria-expanded')).toBe('true');
    const subsystems = [
      ...element.querySelectorAll<HTMLElement>('[data-tree-key^="taxonomy:"][aria-level="2"]'),
    ];
    expect(subsystems.map(rowLabel)).toEqual(['proc', 'fs', 'abi', 'dl', 'mod']);
    expect(subsystems.map(rowCount)).toEqual(['40', '6', '25', '2', '4']);
    for (const subsystem of subsystems) {
      expect(subsystem.getAttribute('aria-expanded')).toBe('false');
    }
    expect(element.querySelectorAll('.docs-index-link').length).toBe(0);

    row(element, 'taxonomy:proc')?.click();
    fixture.detectChanges();

    expect(row(element, 'taxonomy:proc')?.getAttribute('aria-expanded')).toBe('true');
    const categories = [
      ...element.querySelectorAll<HTMLElement>('[data-tree-key^="taxonomy:proc/"]'),
    ];
    expect(categories.map(rowLabel)).toEqual([
      'core',
      'ipc',
      'memory',
      'process',
      'signal',
      'thread',
    ]);
    expect(categories.map(rowCount)).toEqual(['9', '1', '2', '18', '4', '6']);
    for (const category of categories) {
      expect(category.getAttribute('aria-level')).toBe('3');
      expect(category.getAttribute('aria-expanded')).toBe('false');
    }
    expect(element.querySelectorAll('.docs-index-link').length).toBe(0);

    row(element, 'taxonomy:proc/process')?.click();
    fixture.detectChanges();

    expect(element.querySelectorAll('.docs-index-link').length).toBe(18);
    expect(row(element, 'page:syscall-70-uname')?.getAttribute('aria-level')).toBe('4');
    expect(row(element, 'page:syscall-70-uname')?.getAttribute('aria-setsize')).toBe('18');

    row(element, 'taxonomy:fs')?.click();
    fixture.detectChanges();

    expect(row(element, 'taxonomy:fs')?.getAttribute('aria-expanded')).toBe('true');
    expect(element.querySelectorAll('.docs-index-link').length).toBe(24);
    expect(row(element, 'page:syscall-0-io-setup')?.getAttribute('aria-level')).toBe('3');

    row(element, 'taxonomy:proc')?.click();
    fixture.detectChanges();

    expect(row(element, 'taxonomy:proc')?.getAttribute('aria-expanded')).toBe('false');
    expect(element.querySelectorAll('.docs-index-link').length).toBe(6);
    expect(row(element, 'page:syscall-70-uname')).toBeNull();

    syscalls?.click();
    fixture.detectChanges();

    expect(syscalls?.getAttribute('aria-expanded')).toBe('false');
    expect(element.querySelectorAll('.docs-index-link').length).toBe(0);
    expect(row(element, 'taxonomy:proc')).toBeNull();
  });

  it('filters the nested syscall tree and keeps matching descendant counts', async () => {
    const { fixture, element } = render();
    const input = element.querySelector<HTMLInputElement>('.docs-filter input');

    if (input) {
      input.value = 'uname';
      input.dispatchEvent(new Event('input', { bubbles: true }));
    }
    await settle(fixture);

    expect(row(element, DOC_SYSCALLS_KEY)?.getAttribute('aria-expanded')).toBe('true');
    expect(rowCount(row(element, DOC_SYSCALLS_KEY)!)).toBe('1');
    expect(rowLabel(row(element, 'taxonomy:proc')!)).toBe('proc');
    expect(rowCount(row(element, 'taxonomy:proc')!)).toBe('1');
    expect(rowLabel(row(element, 'taxonomy:proc/process')!)).toBe('process');
    expect(rowCount(row(element, 'taxonomy:proc/process')!)).toBe('1');
    expect(element.querySelectorAll('.docs-index-link').length).toBe(1);
    expect(element.querySelector('.docs-index-link')?.textContent).toContain('uname(2)');
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

  it('supports keyboard navigation through the nested syscall tree', async () => {
    const { fixture, element } = render();
    const selected: string[] = [];
    fixture.componentInstance.pageSelected.subscribe((slug) => selected.push(slug));
    row(element, DOC_SYSCALLS_KEY)?.focus();

    await press(fixture, 'ArrowRight');
    expect(row(element, DOC_SYSCALLS_KEY)?.getAttribute('aria-expanded')).toBe('true');
    expect(document.activeElement).toBe(row(element, DOC_SYSCALLS_KEY));

    await press(fixture, 'ArrowDown');
    expect(document.activeElement).toBe(row(element, 'taxonomy:proc'));

    await press(fixture, 'ArrowRight');
    expect(row(element, 'taxonomy:proc')?.getAttribute('aria-expanded')).toBe('true');
    expect(document.activeElement).toBe(row(element, 'taxonomy:proc'));

    await press(fixture, 'ArrowDown');
    expect(document.activeElement).toBe(row(element, 'taxonomy:proc/core'));

    await press(fixture, 'ArrowRight');
    expect(row(element, 'taxonomy:proc/core')?.getAttribute('aria-expanded')).toBe('true');

    await press(fixture, 'ArrowRight');
    const firstLeaf = row(element, 'page:syscall-10-getpid');
    expect(document.activeElement).toBe(firstLeaf);
    expect(firstLeaf?.getAttribute('aria-level')).toBe('4');

    await press(fixture, 'Enter');
    expect(selected).toEqual(['syscall-10-getpid']);

    await press(fixture, 'ArrowLeft');
    expect(document.activeElement).toBe(row(element, 'taxonomy:proc/core'));

    await press(fixture, 'ArrowLeft');
    expect(row(element, 'taxonomy:proc/core')?.getAttribute('aria-expanded')).toBe('false');
    expect(document.activeElement).toBe(row(element, 'taxonomy:proc/core'));

    await press(fixture, 'ArrowLeft');
    expect(document.activeElement).toBe(row(element, 'taxonomy:proc'));
  });

  it('filters taxonomy branches with the text filter', () => {
    const { fixture, element } = render();
    const input = element.querySelector<HTMLInputElement>('.docs-filter input');

    if (input) {
      input.value = 'uname';
      input.dispatchEvent(new Event('input', { bubbles: true }));
    }
    fixture.detectChanges();

    expect(element.querySelectorAll('.docs-index-link').length).toBe(1);
    expect(element.querySelector('.docs-index-link')?.textContent).toContain(
      'uname(2)',
    );
    expect(element.querySelector('.docs-count')?.textContent).toContain('1 pages');
    expect(rowLabel(row(element, DOC_SYSCALLS_KEY)!)).toBe('Syscalls (2)');
    expect(rowLabel(row(element, 'taxonomy:proc')!)).toBe('proc');
    expect(rowLabel(row(element, 'taxonomy:proc/process')!)).toBe('process');
    expect(
      element.querySelector('[data-tree-key="taxonomy:proc"]')?.getAttribute(
        'aria-expanded',
      ),
    ).toBe('true');
    expect(
      element.querySelector('[data-tree-key="taxonomy:proc/process"]')?.getAttribute(
        'aria-expanded',
      ),
    ).toBe('true');
    expect(element.querySelector('[data-tree-key="taxonomy:fs"]')).toBeNull();

    if (input) {
      input.value = 'no such page';
      input.dispatchEvent(new Event('input', { bubbles: true }));
    }
    fixture.detectChanges();
    expect(element.querySelectorAll('.docs-index-link').length).toBe(0);
    expect(element.querySelector('.docs-empty')?.textContent).toContain(
      'No docs match',
    );
    expect(element.querySelector('.docs-count')?.textContent).toContain('0 pages');
  });

  it('keeps the taxonomy tree expanded around the selected page', () => {
    const { fixture, element } = render();

    fixture.componentRef.setInput('selectedSlug', 'syscall-10-getpid');
    fixture.componentRef.setInput('status', 'ready');
    fixture.componentRef.setInput('renderedContent', '<p>getpid</p>');
    fixture.detectChanges();

    expect(element.querySelector('.docs-page-title')?.textContent).toContain(
      'getpid(2)',
    );
    expect(
      element.querySelector('[data-tree-key="taxonomy:proc"]')?.getAttribute(
        'aria-expanded',
      ),
    ).toBe('true');
    expect(
      element.querySelector('[data-tree-key="taxonomy:proc/core"]')?.getAttribute(
        'aria-expanded',
      ),
    ).toBe('true');
    expect(
      element.querySelector('[data-tree-key="page:syscall-10-getpid"]')?.getAttribute(
        'aria-selected',
      ),
    ).toBe('true');
    expect(element.querySelectorAll('.docs-index select').length).toBe(0);

    fixture.componentRef.setInput('selectedSlug', null);
    fixture.componentRef.setInput('status', 'idle');
    fixture.detectChanges();

    expect(
      element.querySelector('[data-tree-key="taxonomy:proc"]')?.getAttribute(
        'aria-expanded',
      ),
    ).toBe('true');
    expect(
      element.querySelector('[data-tree-key="page:syscall-10-getpid"]')?.getAttribute(
        'aria-selected',
      ),
    ).toBe('false');
  });

  it('expands Syscalls, proc, and process for a deep-linked page and marks selection', async () => {
    const { fixture, element } = render('syscall-70-uname');

    await settle(fixture);

    expect(row(element, DOC_SYSCALLS_KEY)?.getAttribute('aria-expanded')).toBe('true');
    expect(row(element, 'taxonomy:proc')?.getAttribute('aria-expanded')).toBe('true');
    expect(
      row(element, 'taxonomy:proc/process')?.getAttribute('aria-expanded'),
    ).toBe('true');
    expect(row(element, 'taxonomy:fs')?.getAttribute('aria-expanded')).toBe('false');

    const selected = row(element, 'page:syscall-70-uname');
    expect(element.querySelector('.docs-page-title')?.textContent).toContain(
      'uname(2)',
    );
    expect(selected?.getAttribute('aria-selected')).toBe('true');
    expect(selected?.getAttribute('aria-current')).toBe('page');
    expect(selected?.getAttribute('aria-level')).toBe('4');
    expect(selected?.classList.contains('is-active')).toBeTrue();
    expect(element.querySelector('.docs-index-link.is-active')?.textContent).toContain(
      'proc / process',
    );
    expect(document.activeElement).toBe(element.querySelector('.docs-page-title'));
    expect(element.querySelector('.docs-page-top')?.textContent).toContain('proc');
    expect(element.querySelector('.docs-page-top')?.textContent).toContain('process');
    expect(element.querySelector('.docs-page-top')?.textContent).toContain('ID 70');
  });
});
