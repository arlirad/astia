import { TestBed } from '@angular/core/testing';
import { provideRouter } from '@angular/router';
import { App } from './app';
import { routes } from './app.routes';
import { NAV_LINKS } from './content';

describe('App shell', () => {
  beforeEach(async () => {
    await TestBed.configureTestingModule({
      imports: [App],
      providers: [provideRouter(routes)],
    }).compileComponents();
  });

  function render(): {
    fixture: ReturnType<typeof TestBed.createComponent<App>>;
    element: HTMLElement;
  } {
    const fixture = TestBed.createComponent(App);
    fixture.detectChanges();
    return { fixture, element: fixture.nativeElement as HTMLElement };
  }

  it('renders the primary navigation, router outlet, and footer', () => {
    const { element } = render();

    expect(element.querySelectorAll('.site-nav a').length).toBe(NAV_LINKS.length + 1);
    expect(element.querySelector('router-outlet')).toBeTruthy();
    expect(element.querySelector('.footer-links a[href="/documentation"]')).toBeTruthy();
  });

  it('renders Documentation after Get started in the navbar', () => {
    const { element } = render();
    const labels = [
      ...element.querySelectorAll<HTMLAnchorElement>('.site-nav a:not(.nav-github)'),
    ].map((link) => link.textContent?.trim());

    expect(labels).toEqual([
      'Overview',
      'Capabilities',
      'Roadmap',
      'Get started',
      'Documentation',
    ]);
  });

  it('toggles the mobile navigation and closes it from a link', () => {
    const { fixture, element } = render();
    const toggle = element.querySelector<HTMLButtonElement>('.nav-toggle');
    const nav = element.querySelector<HTMLElement>('.site-nav');

    expect(toggle?.getAttribute('aria-expanded')).toBe('false');

    toggle?.click();
    fixture.detectChanges();

    expect(toggle?.getAttribute('aria-expanded')).toBe('true');
    expect(nav?.classList.contains('is-open')).toBeTrue();

    element.querySelector<HTMLAnchorElement>('.site-nav a[href="/documentation"]')?.click();
    fixture.detectChanges();

    expect(toggle?.getAttribute('aria-expanded')).toBe('false');
    expect(nav?.classList.contains('is-open')).toBeFalse();
  });

  it('closes the mobile navigation on Escape', () => {
    const { fixture, element } = render();
    const toggle = element.querySelector<HTMLButtonElement>('.nav-toggle');

    toggle?.click();
    fixture.detectChanges();
    document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape' }));
    fixture.detectChanges();

    expect(toggle?.getAttribute('aria-expanded')).toBe('false');
  });
});
