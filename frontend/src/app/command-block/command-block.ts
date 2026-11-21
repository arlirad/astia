import {
  ChangeDetectionStrategy,
  Component,
  DestroyRef,
  inject,
  input,
  signal,
} from '@angular/core';

@Component({
  selector: 'app-command-block',
  templateUrl: './command-block.html',
  styleUrl: './command-block.scss',
  changeDetection: ChangeDetectionStrategy.OnPush,
})
export class CommandBlock {
  readonly label = input.required<string>();
  readonly code = input.required<string>();
  readonly note = input('');

  protected readonly copied = signal(false);
  private resetTimer: ReturnType<typeof setTimeout> | null = null;
  private readonly destroyRef = inject(DestroyRef);

  constructor() {
    this.destroyRef.onDestroy(() => {
      if (this.resetTimer !== null) {
        clearTimeout(this.resetTimer);
      }
    });
  }

  protected async copy(): Promise<void> {
    try {
      await navigator.clipboard.writeText(this.code());
      this.copied.set(true);
      if (this.resetTimer !== null) {
        clearTimeout(this.resetTimer);
      }
      this.resetTimer = setTimeout(() => this.copied.set(false), 2000);
    } catch {
      this.copied.set(false);
    }
  }
}
