import { TestBed } from '@angular/core/testing';
import { provideRouter } from '@angular/router';
import { HomePage } from './home';
import { CAPABILITIES, COMMANDS, RESOURCES, ROADMAP } from '../content';

describe('HomePage', () => {
  beforeEach(async () => {
    await TestBed.configureTestingModule({
      imports: [HomePage],
      providers: [provideRouter([])],
    }).compileComponents();
  });

  function render(): HTMLElement {
    const fixture = TestBed.createComponent(HomePage);
    fixture.detectChanges();
    return fixture.nativeElement as HTMLElement;
  }

  it('renders the project overview and homepage sections', () => {
    const element = render();

    expect(element.querySelector('h1')?.textContent).toContain(
      'An operating system built to be understood.',
    );
    expect(element.querySelectorAll('#capabilities .capability').length).toBe(
      CAPABILITIES.length,
    );
    expect(element.querySelectorAll('#roadmap .roadmap-item').length).toBe(ROADMAP.length);
    expect(element.querySelectorAll('app-command-block').length).toBe(COMMANDS.length);
    expect(element.querySelectorAll('.resource-link').length).toBe(RESOURCES.length);
  });

  it('does not render the documentation browser', () => {
    const element = render();

    expect(element.querySelector('#documentation')).toBeNull();
    expect(element.querySelector('app-docs-browser')).toBeNull();
    expect(element.textContent).not.toContain('\u2014');
  });
});
