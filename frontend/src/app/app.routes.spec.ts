import { TestBed } from '@angular/core/testing';
import { provideRouter } from '@angular/router';
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
});
