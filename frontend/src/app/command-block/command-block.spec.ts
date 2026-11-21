import { TestBed } from '@angular/core/testing';
import { CommandBlock } from './command-block';

describe('CommandBlock', () => {
  beforeEach(async () => {
    await TestBed.configureTestingModule({
      imports: [CommandBlock],
    }).compileComponents();
  });

  function render(label = 'Build the kernel', code = 'cmake --build build', note = ''): {
    fixture: ReturnType<typeof TestBed.createComponent<CommandBlock>>;
    element: HTMLElement;
  } {
    const fixture = TestBed.createComponent(CommandBlock);
    fixture.componentRef.setInput('label', label);
    fixture.componentRef.setInput('code', code);
    fixture.componentRef.setInput('note', note);
    fixture.detectChanges();
    return { fixture, element: fixture.nativeElement as HTMLElement };
  }

  it('renders its label, command, and optional note', () => {
    const { element } = render('Boot the kernel', './kernel/scripts/run-qemu.sh', 'Serial console.');

    expect(element.querySelector('.command-label')?.textContent).toContain('Boot the kernel');
    expect(element.querySelector('pre code')?.textContent).toContain(
      './kernel/scripts/run-qemu.sh',
    );
    expect(element.querySelector('.command-note')?.textContent).toContain('Serial console.');
  });

  it('omits the note when none is provided', () => {
    const { element } = render();

    expect(element.querySelector('.command-note')).toBeNull();
  });

  it('copies the command and reports success', async () => {
    const writeText = jasmine.createSpy('writeText').and.resolveTo();
    Object.defineProperty(navigator, 'clipboard', {
      configurable: true,
      value: { writeText },
    });

    const { fixture, element } = render('Boot', './run');
    const button = element.querySelector<HTMLButtonElement>('.command-copy');

    button?.click();
    await Promise.resolve();
    fixture.detectChanges();

    expect(writeText).toHaveBeenCalledWith('./run');
    expect(button?.textContent).toContain('Copied');
  });

  it('does not report success when clipboard access fails', async () => {
    const writeText = jasmine.createSpy('writeText').and.rejectWith(new Error('denied'));
    Object.defineProperty(navigator, 'clipboard', {
      configurable: true,
      value: { writeText },
    });

    const { fixture, element } = render('Boot', './run');
    const button = element.querySelector<HTMLButtonElement>('.command-copy');

    button?.click();
    await Promise.resolve();
    fixture.detectChanges();

    expect(button?.textContent).toContain('Copy');
    expect(button?.textContent).not.toContain('Copied');
  });
});
