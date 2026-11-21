import { ChangeDetectionStrategy, Component, HostListener, signal } from '@angular/core';
import { RouterLink, RouterOutlet } from '@angular/router';
import { GITHUB_URL, ISSUES_URL, LICENSE_URL, NAV_LINKS, README_URL } from './content';

@Component({
  selector: 'app-root',
  imports: [RouterLink, RouterOutlet],
  templateUrl: './app.html',
  styleUrl: './app.scss',
  changeDetection: ChangeDetectionStrategy.OnPush,
})
export class App {
  protected readonly navLinks = NAV_LINKS;
  protected readonly githubUrl = GITHUB_URL;
  protected readonly readmeUrl = README_URL;
  protected readonly issuesUrl = ISSUES_URL;
  protected readonly licenseUrl = LICENSE_URL;
  protected readonly menuOpen = signal(false);

  protected toggleMenu(): void {
    this.menuOpen.update((open) => !open);
  }

  protected closeMenu(): void {
    this.menuOpen.set(false);
  }

  @HostListener('document:keydown.escape')
  protected onEscape(): void {
    this.closeMenu();
  }
}
