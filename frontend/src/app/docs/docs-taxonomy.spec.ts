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
};

const optionText = (select: HTMLSelectElement): string[] =>
  [...select.options].map((option) => (option.textContent ?? '').trim().replace(/\s+/g, ' '));

const choose = (
  element: HTMLElement,
  selector: string,
  value: string,
): HTMLSelectElement => {
  const select = element.querySelector<HTMLSelectElement>(selector);
  if (!select) {
    throw new Error(`missing select: ${selector}`);
  }
  select.value = value;
  select.dispatchEvent(new Event('change', { bubbles: true }));
  return select;
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

  it('derives exact subsystem and category facets from all 73 syscall pages', () => {
    expect(SYSCALL_PAGES.length).toBe(73);
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
    expect(element.querySelector('.docs-taxonomy legend')?.textContent).toContain(
      'Taxonomy',
    );
    expect(optionText(element.querySelector('.docs-subsystem-filter')!)).toEqual([
      'All (73)',
      'proc (40)',
      'fs (6)',
      'abi (25)',
      'dl (2)',
    ]);
    expect(optionText(element.querySelector('.docs-category-filter')!)).toEqual([
      'All (73)',
      'core (9)',
      'ipc (1)',
      'memory (2)',
      'process (18)',
      'signal (4)',
      'thread (6)',
      'fs (6)',
      'abi (25)',
      'dl (2)',
    ]);

    const topLevel = [
      ...element.querySelectorAll<HTMLElement>('[role="treeitem"][aria-level="1"]'),
    ];
    expect(topLevel.map(rowLabel)).toEqual([
      'Syscalls (2)',
      'Guide',
      'Manual page',
      'Design note',
    ]);
    expect(rowCount(topLevel[0])).toBe('73');
    expect(topLevel[0].getAttribute('aria-expanded')).toBe('false');
    expect(topLevel[0].getAttribute('aria-setsize')).toBe('4');
    expect(topLevel[0].getAttribute('aria-posinset')).toBe('1');
    expect(element.querySelectorAll('[role="treeitem"]').length).toBe(4);
    expect(element.querySelectorAll('.docs-index-link').length).toBe(0);
    expect(element.querySelector('.docs-count')?.textContent).toContain(
      `${DOC_PAGES.length} pages`,
    );
  });

  it('nests proc, fs, abi, and dl under one expandable Syscalls node', () => {
    const { fixture, element } = render();
    const syscalls = row(element, DOC_SYSCALLS_KEY);

    expect(syscalls?.getAttribute('role')).toBe('treeitem');
    expect(syscalls?.getAttribute('aria-level')).toBe('1');
    expect(syscalls?.getAttribute('aria-expanded')).toBe('false');
    expect(rowLabel(syscalls!)).toBe('Syscalls (2)');
    expect(rowCount(syscalls!)).toBe('73');

    syscalls?.click();
    fixture.detectChanges();

    expect(syscalls?.getAttribute('aria-expanded')).toBe('true');
    const subsystems = [
      ...element.querySelectorAll<HTMLElement>('[data-tree-key^="taxonomy:"][aria-level="2"]'),
    ];
    expect(subsystems.map(rowLabel)).toEqual(['proc', 'fs', 'abi', 'dl']);
    expect(subsystems.map(rowCount)).toEqual(['40', '6', '25', '2']);
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

  it('filters by subsystem and category and resets incompatible categories', () => {
    const { fixture, element } = render();

    choose(element, '.docs-subsystem-filter', 'proc');
    fixture.detectChanges();

    expect(element.querySelectorAll('.docs-index-link').length).toBe(40);
    expect(element.querySelector('.docs-count')?.textContent).toContain('40 pages');
    expect(optionText(element.querySelector('.docs-category-filter')!)).toEqual([
      'All (40)',
      'core (9)',
      'ipc (1)',
      'memory (2)',
      'process (18)',
      'signal (4)',
      'thread (6)',
    ]);
    expect(
      [...element.querySelectorAll<HTMLButtonElement>('.docs-index-link')].every(
        (link) => link.getAttribute('data-syscall-id') !== null,
      ),
    ).toBeTrue();

    choose(element, '.docs-category-filter', 'process');
    fixture.detectChanges();

    expect(element.querySelectorAll('.docs-index-link').length).toBe(18);
    expect(
      element.querySelector('.docs-category-filter') as HTMLSelectElement,
    ).toBeTruthy();
    expect(element.querySelector('.docs-count')?.textContent).toContain('18 pages');

    choose(element, '.docs-subsystem-filter', 'fs');
    fixture.detectChanges();

    const category = element.querySelector('.docs-category-filter') as HTMLSelectElement;
    expect(category.value).toBe('all');
    expect(element.querySelectorAll('.docs-index-link').length).toBe(6);
    expect(optionText(category)).toEqual(['All (6)', 'fs (6)']);
    expect(element.querySelector('.docs-count')?.textContent).toContain('6 pages');
  });

  it('combines taxonomy facets with the existing text filter', () => {
    const { fixture, element } = render();

    choose(element, '.docs-subsystem-filter', 'proc');
    choose(element, '.docs-category-filter', 'process');
    fixture.detectChanges();

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

    if (input) {
      input.value = 'no such page';
      input.dispatchEvent(new Event('input', { bubbles: true }));
    }
    fixture.detectChanges();
    expect(element.querySelectorAll('.docs-index-link').length).toBe(0);
    expect(element.querySelector('.docs-empty')?.textContent).toContain(
      'No docs match',
    );
  });

  it('retains taxonomy facets across page selection and return to the index', () => {
    const { fixture, element } = render();

    choose(element, '.docs-subsystem-filter', 'proc');
    choose(element, '.docs-category-filter', 'core');
    fixture.detectChanges();
    expect(element.querySelectorAll('.docs-index-link').length).toBe(9);

    fixture.componentRef.setInput('selectedSlug', 'syscall-10-getpid');
    fixture.componentRef.setInput('status', 'ready');
    fixture.componentRef.setInput('renderedContent', '<p>getpid</p>');
    fixture.detectChanges();

    expect(element.querySelector('.docs-page-title')?.textContent).toContain(
      'getpid(2)',
    );
    expect(
      (element.querySelector('.docs-subsystem-filter') as HTMLSelectElement).value,
    ).toBe('proc');
    expect(
      (element.querySelector('.docs-category-filter') as HTMLSelectElement).value,
    ).toBe('core');
    expect(element.querySelectorAll('.docs-index-link').length).toBe(9);

    fixture.componentRef.setInput('selectedSlug', null);
    fixture.componentRef.setInput('status', 'idle');
    fixture.detectChanges();

    expect(
      (element.querySelector('.docs-subsystem-filter') as HTMLSelectElement).value,
    ).toBe('proc');
    expect(
      (element.querySelector('.docs-category-filter') as HTMLSelectElement).value,
    ).toBe('core');
    expect(element.querySelectorAll('.docs-index-link').length).toBe(9);
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
