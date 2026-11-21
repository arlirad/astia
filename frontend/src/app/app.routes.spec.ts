import { TestBed } from '@angular/core/testing';
import { Router, provideRouter } from '@angular/router';
import { RouterTestingHarness } from '@angular/router/testing';
import { routes } from './app.routes';
import { DocumentationPage } from './documentation/documentation-page';
import { DOC_PAGES } from './content';
import { HomePage } from './home/home';

describe('Application routes', () => {
  beforeEach(async () => {
    await TestBed.configureTestingModule({
      providers: [provideRouter(routes)],
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
    expect(element.querySelectorAll('.docs-index-link').length).toBe(DOC_PAGES.length);
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

  it('renders a deep-linked section-2 syscall page', async () => {
    const harness = await RouterTestingHarness.create();
    await harness.navigateByUrl('/documentation/syscall-0-io-setup', DocumentationPage);
    harness.detectChanges();
    const element = harness.routeNativeElement as HTMLElement;

    expect(element.querySelector('.docs-page-title')?.textContent).toContain(
      'io_setup(2)',
    );
    expect(element.querySelector('.doc-markdown')?.textContent).toContain('SYNOPSIS');
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
});
