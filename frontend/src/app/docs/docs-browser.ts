import {
  ChangeDetectionStrategy,
  Component,
  ElementRef,
  computed,
  input,
  signal,
  viewChild,
} from '@angular/core';
import { marked } from 'marked';
import type { DocPage } from './docs.model';

interface DocGroupView {
  readonly name: string;
  readonly pages: readonly DocPage[];
}

@Component({
  selector: 'app-docs-browser',
  templateUrl: './docs-browser.html',
  styleUrl: './docs-browser.scss',
  changeDetection: ChangeDetectionStrategy.OnPush,
})
export class DocsBrowser {
  readonly pages = input.required<readonly DocPage[]>();

  protected readonly query = signal('');
  protected readonly selectedSlug = signal<string | null>(null);
  protected readonly selectedPage = computed(() => {
    const slug = this.selectedSlug();
    return slug ? (this.pages().find((page) => page.slug === slug) ?? null) : null;
  });
  protected readonly filteredGroups = computed<readonly DocGroupView[]>(() => {
    const query = this.query().trim().toLowerCase();
    const groups = new Map<string, DocPage[]>();
    for (const page of this.pages()) {
      if (query && !`${page.title} ${page.group} ${page.summary}`.toLowerCase().includes(query)) {
        continue;
      }
      const pages = groups.get(page.group) ?? [];
      pages.push(page);
      groups.set(page.group, pages);
    }
    return [...groups].map(([name, pages]) => ({ name, pages }));
  });
  protected readonly resultCount = computed(() =>
    this.filteredGroups().reduce((count, group) => count + group.pages.length, 0),
  );
  protected readonly renderedContent = computed(() => {
    const page = this.selectedPage();
    return page ? (marked.parse(page.content.replace(/^#\s+.+\n+/, '')) as string) : '';
  });
  protected readonly previousPage = computed(() => this.adjacentPage(-1));
  protected readonly nextPage = computed(() => this.adjacentPage(1));

  private readonly browser = viewChild<ElementRef<HTMLElement>>('browser');

  protected setQuery(event: Event): void {
    this.query.set((event.target as HTMLInputElement).value);
  }

  protected selectPage(slug: string): void {
    this.selectedSlug.set(slug);
    this.scrollToBrowser();
  }

  protected backToIndex(): void {
    this.selectedSlug.set(null);
    this.scrollToBrowser();
  }

  private adjacentPage(direction: -1 | 1): DocPage | null {
    const pages = this.pages();
    const slug = this.selectedSlug();
    const index = slug ? pages.findIndex((page) => page.slug === slug) : -1;
    if (index < 0) {
      return null;
    }
    return pages[index + direction] ?? null;
  }

  private scrollToBrowser(): void {
    queueMicrotask(() =>
      this.browser()?.nativeElement.scrollIntoView({ block: 'start', behavior: 'smooth' }),
    );
  }
}
