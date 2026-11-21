import { TestBed } from '@angular/core/testing';
import { App } from './app';
import { CAPABILITIES, COMMANDS, NAV_LINKS, RESOURCES, ROADMAP } from './content';

describe('App', () => {
  beforeEach(async () => {
    await TestBed.configureTestingModule({
      imports: [App],
    }).compileComponents();
  });

  function render(): { fixture: ReturnType<typeof TestBed.createComponent<App>>; element: HTMLElement } {
    const fixture = TestBed.createComponent(App);
    fixture.detectChanges();
    return { fixture, element: fixture.nativeElement as HTMLElement };
  }

  it('renders the product overview and primary navigation', () => {
    const { element } = render();

    expect(element.querySelector('h1')?.textContent).toContain(
      'An operating system built to be understood.',
    );
    expect(element.querySelectorAll('.site-nav a').length).toBe(NAV_LINKS.length + 1);
    expect(element.querySelector('.hero-art img')?.getAttribute('src')).toContain('banner.svg');
  });

  it('renders every capability and roadmap item', () => {
    const { element } = render();

    expect(element.querySelectorAll('#capabilities .capability').length).toBe(
      CAPABILITIES.length,
    );
    expect(element.querySelectorAll('#roadmap .roadmap-item').length).toBe(ROADMAP.length);
    expect(element.querySelectorAll('#capabilities .check-list').length).toBe(2);
  });

  it('renders each getting-started command block and resource link', () => {
    const { element } = render();

    expect(element.querySelectorAll('app-command-block').length).toBe(COMMANDS.length);
    expect(element.querySelectorAll('.resource-link').length).toBe(RESOURCES.length);
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

    element.querySelector<HTMLAnchorElement>('.site-nav a')?.click();
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
