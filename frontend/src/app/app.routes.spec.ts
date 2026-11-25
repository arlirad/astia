import { TestBed } from '@angular/core/testing';
import { Router, provideRouter } from '@angular/router';
import { RouterTestingHarness } from '@angular/router/testing';
import { routes } from './app.routes';
import { DocumentationPage } from './documentation/documentation-page';
import { DOCS_FETCH } from './docs/docs.repository';
import { DOC_SYSCALLS_KEY } from './docs/docs-tree';
import { HomePage } from './home/home';

async function settle(harness: RouterTestingHarness): Promise<void> {
  harness.detectChanges();
  await harness.fixture.whenStable();
  await Promise.resolve();
  await new Promise((resolve) => setTimeout(resolve, 0));
  harness.detectChanges();
}

describe('Application routes', () => {
  beforeEach(async () => {
    const fetchSpy = jasmine.createSpy('fetch').and.callFake((input: RequestInfo | URL) => {
      const url = String(input);
      if (url.endsWith('/docs/7/aio.md')) {
        return Promise.resolve(
          new Response('# Asynchronous I/O\n\n## DESCRIPTION\n\nRuntime content.', {
            status: 200,
          }),
        );
      }
      if (url.endsWith('/docs/2/fs/io_setup.md')) {
        return Promise.resolve(
          new Response(
            '# io_setup(2)\n\n## DESCRIPTION\n\nCreate an asynchronous I/O ring.\n\n## RETURN VALUE\n\nReturns zero.\n\n## ERRORS\n\n- `-EINVAL`: Invalid arguments.',
            { status: 200 },
          ),
        );
      }
      if (url.endsWith('/docs/2/proc/process/uname.md')) {
        return Promise.resolve(
          new Response(
            '# uname(2)\n\n## DESCRIPTION\n\nRead the system identity.\n\n## RETURN VALUE\n\nReturns zero.\n\n## ERRORS\n\n- `-EFAULT`: Invalid output.',
            { status: 200 },
          ),
        );
      }
      if (url.endsWith('/docs/2/dl/dlmap.md')) {
        return Promise.resolve(
          new Response(
            '# dlmap(2)\n\n## DESCRIPTION\n\nMap a shared-library image.\n\n## RETURN VALUE\n\nReturns the load bias.\n\n## ERRORS\n\n- `-EINVAL`: Invalid flags.',
            { status: 200 },
          ),
        );
      }
      return Promise.resolve(
        new Response('long astia_io_setup(uint32_t entries, uint32_t flags, astia_rings** out) {\n  return 0;\n}', {
          status: 200,
        }),
      );
    });

    await TestBed.configureTestingModule({
      providers: [provideRouter(routes), { provide: DOCS_FETCH, useValue: fetchSpy }],
    }).compileComponents();
  });

  it('renders the homepage at / without the docs browser', async () => {
    const harness = await RouterTestingHarness.create();
    const component = await harness.navigateByUrl('/', HomePage);
    const element = harness.routeNativeElement as HTMLElement;

    expect(component).toBeInstanceOf(HomePage);
    expect(element.querySelector('h1')?.textContent).toContain(
      'An operating system built to be understood.',
    );
    expect(element.querySelector('app-docs-browser')).toBeNull();
  });

  it('renders the documentation index at /documentation', async () => {
    const harness = await RouterTestingHarness.create();
    const component = await harness.navigateByUrl('/documentation', DocumentationPage);
    const element = harness.routeNativeElement as HTMLElement;

    expect(component).toBeInstanceOf(DocumentationPage);
    expect(element.querySelectorAll('[role="treeitem"]').length).toBe(4);
    expect(element.querySelectorAll('.docs-index-link').length).toBe(0);
    expect(element.querySelector('[role="tree"]')?.textContent).toContain('Syscalls (2)');
    expect(
      element
        .querySelector(`[data-tree-key="${DOC_SYSCALLS_KEY}"]`)
        ?.getAttribute('aria-expanded'),
    ).toBe('false');
    expect(element.querySelector('.docs-page-title')).toBeNull();
    expect(element.querySelector('h1.eyebrow')?.textContent).toContain('Documentation');
    expect(element.textContent).not.toContain('Design notes, boundaries, and current limits.');
    expect(element.textContent).not.toContain(
      'Bundled from the repository Markdown sources.',
    );

    const shell = element.querySelector('#documentation .shell');
    const sectionHead = shell?.querySelector('.section-head');
    const docsBrowser = shell?.querySelector('app-docs-browser');
    expect(sectionHead?.nextElementSibling).toBe(docsBrowser);
    expect(docsBrowser?.querySelector('.docs-index')).toBeTruthy();
    expect(docsBrowser?.querySelector('.docs-welcome')).toBeNull();
    expect(element.textContent).not.toContain('Browse the Astia documentation.');
  });

  it('renders a deep-linked documentation page', async () => {
    const harness = await RouterTestingHarness.create();
    await harness.navigateByUrl('/documentation/docs-aio-7', DocumentationPage);
    harness.detectChanges();
    const element = harness.routeNativeElement as HTMLElement;

    expect(element.querySelector('.docs-page-title')?.textContent).toContain(
      'Asynchronous I/O',
    );
    expect(element.textContent).not.toContain('\u2014');
  });

  it('resolves the new section-7 alias to the stable route page', async () => {
    const harness = await RouterTestingHarness.create();
    await harness.navigateByUrl('/documentation/docs-7-aio', DocumentationPage);
    harness.detectChanges();
    const element = harness.routeNativeElement as HTMLElement;

    expect(element.querySelector('.docs-page-title')?.textContent).toContain(
      'Asynchronous I/O',
    );
    expect(element.querySelector('.docs-source')?.getAttribute('href')).toContain(
      'docs/7/aio.md',
    );
  });

  it('renders a deep-linked authored section-2 syscall page', async () => {
    const harness = await RouterTestingHarness.create();
    await harness.navigateByUrl('/documentation/syscall-0-io-setup', DocumentationPage);
    await settle(harness);
    const element = harness.routeNativeElement as HTMLElement;

    expect(element.querySelector('.docs-page-title')?.textContent).toContain(
      'io_setup(2)',
    );
    expect(element.querySelector('.doc-markdown')?.textContent).toContain(
      'Create an asynchronous I/O ring.',
    );
    expect(element.querySelector('.doc-markdown')?.textContent).toContain(
      'RETURN VALUE',
    );
    expect(element.querySelector('.doc-markdown')?.textContent).not.toContain(
      'REPOSITORY SOURCE',
    );
    expect(element.querySelector('.docs-source')?.getAttribute('href')).toContain(
      'kernel/src/fs/syscalls/io_setup.cpp',
    );
  });

  it('serves the dl taxonomy page on its stable route', async () => {
    const harness = await RouterTestingHarness.create();
    await harness.navigateByUrl('/documentation/syscall-71-dlmap', DocumentationPage);
    await settle(harness);
    const element = harness.routeNativeElement as HTMLElement;

    expect(element.querySelector('.docs-page-title')?.textContent).toContain(
      'dlmap(2)',
    );
    expect(element.querySelector('.doc-markdown')?.textContent).toContain(
      'Map a shared-library image.',
    );
    expect(element.querySelector('.docs-source')?.getAttribute('href')).toBe(
      'https://github.com/arlirad/astia/blob/main/kernel/src/dl/syscalls/dlmap.cpp',
    );
    expect(
      element
        .querySelector(`[data-tree-key="${DOC_SYSCALLS_KEY}"]`)
        ?.getAttribute('aria-expanded'),
    ).toBe('true');
    expect(
      element
        .querySelector('[data-tree-key="taxonomy:dl"]')
        ?.getAttribute('aria-expanded'),
    ).toBe('true');
    expect(
      element.querySelector('[data-tree-key="page:syscall-71-dlmap"]')
        ?.getAttribute('aria-selected'),
    ).toBe('true');
  });

  it('serves syscall-70 uname on its stable route', async () => {
    const harness = await RouterTestingHarness.create();
    await harness.navigateByUrl('/documentation/syscall-70-uname', DocumentationPage);
    await settle(harness);
    const element = harness.routeNativeElement as HTMLElement;

    expect(element.querySelector('.docs-page-title')?.textContent).toContain(
      'uname(2)',
    );
    expect(element.querySelector('.doc-markdown')?.textContent).toContain(
      'Read the system identity.',
    );
    expect(element.querySelector('.doc-markdown')?.textContent).toContain('ERRORS');
    expect(element.querySelector('.doc-markdown')?.textContent).not.toContain(
      'REPOSITORY SOURCE',
    );
    expect(element.querySelector('.docs-source')?.getAttribute('href')).toBe(
      'https://github.com/arlirad/astia/blob/main/kernel/src/proc/syscalls/process/uname.cpp',
    );
    expect(
      element
        .querySelector(`[data-tree-key="${DOC_SYSCALLS_KEY}"]`)
        ?.getAttribute('aria-expanded'),
    ).toBe('true');
    expect(
      element
        .querySelector('[data-tree-key="taxonomy:proc"]')
        ?.getAttribute('aria-expanded'),
    ).toBe('true');
    expect(
      element
        .querySelector('[data-tree-key="taxonomy:proc/process"]')
        ?.getAttribute('aria-expanded'),
    ).toBe('true');
    expect(
      element
        .querySelector('[data-tree-key="page:syscall-70-uname"]')
        ?.getAttribute('aria-selected'),
    ).toBe('true');
  });

  it('keeps the docs filter and category state across result navigation', async () => {
    const harness = await RouterTestingHarness.create();
    await harness.navigateByUrl('/documentation', DocumentationPage);
    let element = harness.routeNativeElement as HTMLElement;
    const input = element.querySelector<HTMLInputElement>('.docs-filter input');

    if (input) {
      input.value = 'manual';
      input.dispatchEvent(new Event('input', { bubbles: true }));
    }
    harness.detectChanges();
    const filteredCount = element.querySelectorAll('.docs-index-link').length;

    await TestBed.inject(Router).navigateByUrl('/documentation/docs-aio-7');
    harness.detectChanges();
    element = harness.routeNativeElement as HTMLElement;

    expect(element.querySelector('.docs-page-title')?.textContent).toContain(
      'Asynchronous I/O',
    );
    expect(element.querySelector<HTMLInputElement>('.docs-filter input')?.value).toBe(
      'manual',
    );
    expect(element.querySelectorAll('.docs-index-link').length).toBe(filteredCount);
    expect(
      element.querySelectorAll('.docs-group-toggle[aria-expanded="true"]').length,
    ).toBeGreaterThan(0);

    await TestBed.inject(Router).navigateByUrl('/documentation');
    harness.detectChanges();
    element = harness.routeNativeElement as HTMLElement;

    expect(element.querySelector<HTMLInputElement>('.docs-filter input')?.value).toBe(
      'manual',
    );
    expect(element.querySelectorAll('.docs-index-link').length).toBe(filteredCount);
  });

  it('keeps taxonomy facet state across result navigation', async () => {
    const harness = await RouterTestingHarness.create();
    await harness.navigateByUrl('/documentation', DocumentationPage);
    let element = harness.routeNativeElement as HTMLElement;

    const subsystem = element.querySelector<HTMLSelectElement>('.docs-subsystem-filter');
    const category = element.querySelector<HTMLSelectElement>('.docs-category-filter');
    if (subsystem && category) {
      subsystem.value = 'proc';
      subsystem.dispatchEvent(new Event('change', { bubbles: true }));
      harness.detectChanges();
      category.value = 'core';
      category.dispatchEvent(new Event('change', { bubbles: true }));
      harness.detectChanges();
    }
    expect(element.querySelectorAll('.docs-index-link').length).toBe(9);

    await TestBed.inject(Router).navigateByUrl('/documentation/syscall-10-getpid');
    harness.detectChanges();
    element = harness.routeNativeElement as HTMLElement;

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

    await TestBed.inject(Router).navigateByUrl('/documentation');
    harness.detectChanges();
    element = harness.routeNativeElement as HTMLElement;

    expect(
      (element.querySelector('.docs-subsystem-filter') as HTMLSelectElement).value,
    ).toBe('proc');
    expect(
      (element.querySelector('.docs-category-filter') as HTMLSelectElement).value,
    ).toBe('core');
    expect(element.querySelectorAll('.docs-index-link').length).toBe(9);
  });
});
