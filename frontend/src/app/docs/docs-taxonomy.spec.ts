import { TestBed } from '@angular/core/testing';
import { DOC_PAGES } from '../content';
import { DocsBrowser } from './docs-browser';
import type { DocPage } from './docs.model';

const SYSCALL_PAGES = DOC_PAGES.filter((page) => page.group === 'Syscall (2)');

const SUBSYSTEM_COUNTS: Readonly<Record<string, number>> = {
  proc: 40,
  fs: 6,
  abi: 25,
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

  it('derives exact subsystem and category facets from all 71 syscall pages', () => {
    expect(SYSCALL_PAGES.length).toBe(71);
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
      'All (71)',
      'proc (40)',
      'fs (6)',
      'abi (25)',
    ]);
    expect(optionText(element.querySelector('.docs-category-filter')!)).toEqual([
      'All (71)',
      'core (9)',
      'ipc (1)',
      'memory (2)',
      'process (18)',
      'signal (4)',
      'thread (6)',
      'fs (6)',
      'abi (25)',
    ]);
    expect(element.querySelectorAll('.docs-index-link').length).toBe(DOC_PAGES.length);
    expect(element.querySelector('.docs-count')?.textContent).toContain(
      `${DOC_PAGES.length} pages`,
    );
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

  it('opens the selected page group and shows a stable taxonomy label', () => {
    const { element } = render('syscall-70-uname');

    expect(element.querySelector('.docs-page-title')?.textContent).toContain(
      'uname(2)',
    );
    expect(element.querySelectorAll('.docs-group-toggle[aria-expanded="true"]').length).toBe(
      1,
    );
    expect(element.querySelector('.docs-index-link.is-active')?.textContent).toContain(
      'proc / process',
    );
    expect(element.querySelector('.docs-page-top')?.textContent).toContain('proc');
    expect(element.querySelector('.docs-page-top')?.textContent).toContain('process');
    expect(element.querySelector('.docs-page-top')?.textContent).toContain('ID 70');
  });
});
