import { ChangeDetectionStrategy, Component, HostListener, signal } from '@angular/core';
import { CommandBlock } from './command-block/command-block';
import { DocsBrowser } from './docs/docs-browser';
import {
  CAPABILITIES,
  COMMANDS,
  DOC_PAGES,
  GITHUB_URL,
  ISSUES_URL,
  LICENSE_URL,
  NAV_LINKS,
  NOT_GOALS,
  PREREQUISITES,
  README_URL,
  RESOURCES,
  ROADMAP,
  SNAPSHOT,
  SUPPORTED,
} from './content';

@Component({
  selector: 'app-root',
  imports: [CommandBlock, DocsBrowser],
  templateUrl: './app.html',
  styleUrl: './app.scss',
  changeDetection: ChangeDetectionStrategy.OnPush,
})
export class App {
  protected readonly navLinks = NAV_LINKS;
  protected readonly snapshot = SNAPSHOT;
  protected readonly capabilities = CAPABILITIES;
  protected readonly supported = SUPPORTED;
  protected readonly notGoals = NOT_GOALS;
  protected readonly roadmap = ROADMAP;
  protected readonly docPages = DOC_PAGES;
  protected readonly commands = COMMANDS;
  protected readonly resources = RESOURCES;
  protected readonly prerequisites = PREREQUISITES;
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
