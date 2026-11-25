import {
  ChangeDetectionStrategy,
  Component,
  ElementRef,
  computed,
  effect,
  input,
  output,
  signal,
  viewChild,
} from '@angular/core';
import type { DocPage, DocsLoadStatus } from './docs.model';
import { slugify } from './docs-utils';

interface DocGroupView {
  readonly name: string;
  readonly pages: readonly DocPage[];
}

interface FacetCount {
  readonly name: string;
  readonly count: number;
}

const TAXONOMY_ALL = 'all';
const SUBSYSTEM_ORDER = ['proc', 'fs', 'abi'];
const CATEGORY_ORDER = ['core', 'ipc', 'memory', 'process', 'signal', 'thread', 'fs', 'abi'];

const orderFacets = (
  facets: readonly FacetCount[],
  order: readonly string[],
): readonly FacetCount[] => {
  const rank = (name: string): number => {
    const index = order.indexOf(name);
    return index < 0 ? order.length : index;
  };
  return [...facets].sort(
    (left, right) => rank(left.name) - rank(right.name)
    || left.name.localeCompare(right.name),
  );
};

const pageMatches = (page: DocPage, query: string): boolean => {
  if (!query) {
    return true;
  }
  const searchable =
    `${page.title} ${page.group} ${page.subsystem ?? ''} ${page.category ?? ''} ${page.syscallId ?? ''} ` +
    `${page.synopsisKind ?? ''} ${page.summary}`;
  return searchable.toLowerCase().includes(query);
};

const taxonomyMatches = (page: DocPage, subsystem: string, category: string): boolean => {
  if (subsystem !== TAXONOMY_ALL && page.subsystem !== subsystem) {
    return false;
  }
  return category === TAXONOMY_ALL || page.category === category;
};

@Component({
  selector: 'app-docs-browser',
  templateUrl: './docs-browser.html',
  styleUrl: './docs-browser.scss',
  changeDetection: ChangeDetectionStrategy.OnPush,
})
export class DocsBrowser {
  readonly pages = input.required<readonly DocPage[]>();
  readonly selectedSlug = input<string | null>(null);
  readonly renderedContent = input<string>('');
  readonly status = input<DocsLoadStatus>('idle');
  readonly errorMessage = input<string | null>(null);
  readonly sourceUrl = input<string | null>(null);
  readonly pageSelected = output<string>();
  readonly indexSelected = output<void>();
  readonly retryRequested = output<void>();

  protected readonly query = signal('');
  protected readonly subsystemFilter = signal(TAXONOMY_ALL);
  protected readonly categoryFilter = signal(TAXONOMY_ALL);
  private readonly expandedGroups = signal<ReadonlySet<string>>(new Set());
  private firstSelectionChange = true;
  private lastSelectedSlug: string | null = null;

  protected readonly selectedPage = computed(() => {
    const slug = this.selectedSlug();
    return slug ? (this.pages().find((page) => page.slug === slug) ?? null) : null;
  });
  protected readonly subsystemFacets = computed<readonly FacetCount[]>(() =>
    orderFacets(this.facetCounts(this.pages(), (page) => page.subsystem), SUBSYSTEM_ORDER),
  );
  protected readonly categoryFacets = computed<readonly FacetCount[]>(() => {
    const subsystem = this.subsystemFilter();
    const pages = this.pages().filter(
      (page) => subsystem === TAXONOMY_ALL || page.subsystem === subsystem,
    );
    return orderFacets(this.facetCounts(pages, (page) => page.category), CATEGORY_ORDER);
  });
  protected readonly taxonomyPageCount = computed(
    () => this.pages().filter((page) => page.subsystem).length,
  );
  protected readonly categoryPageCount = computed(() => {
    const subsystem = this.subsystemFilter();
    return this.pages().filter(
      (page) =>
        page.category
        && (subsystem === TAXONOMY_ALL || page.subsystem === subsystem),
    ).length;
  });
  protected readonly filteredGroups = computed<readonly DocGroupView[]>(() => {
    const query = this.query().trim().toLowerCase();
    const subsystem = this.subsystemFilter();
    const category = this.categoryFilter();
    const groups = new Map<string, DocPage[]>();
    for (const page of this.pages()) {
      if (!pageMatches(page, query) || !taxonomyMatches(page, subsystem, category)) {
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
  protected readonly previousPage = computed(() => this.adjacentPage(-1));
  protected readonly nextPage = computed(() => this.adjacentPage(1));

  private readonly browser = viewChild<ElementRef<HTMLElement>>('browser');
  private readonly markdown = viewChild<ElementRef<HTMLElement>>('markdown');
  private readonly pageTitle = viewChild<ElementRef<HTMLElement>>('pageTitle');
  private readonly filterInput = viewChild<ElementRef<HTMLInputElement>>('filterInput');

  constructor() {
    effect(() => {
      const page = this.selectedPage();
      if (!page) {
        this.lastSelectedSlug = null;
        return;
      }
      if (page.slug === this.lastSelectedSlug) {
        return;
      }
      this.lastSelectedSlug = page.slug;
      this.expandedGroups.update((groups) => {
        if (groups.has(page.group)) {
          return groups;
        }
        const next = new Set(groups);
        next.add(page.group);
        return next;
      });
    });

    effect(() => {
      const slug = this.selectedSlug();
      const initial = this.firstSelectionChange;
      this.firstSelectionChange = false;
      queueMicrotask(() => {
        const browser = this.browser()?.nativeElement;
        const panel = browser?.querySelector<HTMLElement>('.docs-main');
        if (panel) {
          panel.scrollTop = 0;
          if (panel.scrollHeight <= panel.clientHeight) {
            browser?.scrollIntoView({ block: 'start', behavior: 'smooth' });
          }
        } else {
          browser?.scrollIntoView({ block: 'start', behavior: 'smooth' });
        }
        if (slug) {
          this.pageTitle()?.nativeElement.focus({ preventScroll: true });
        } else if (!initial) {
          this.filterInput()?.nativeElement.focus({ preventScroll: true });
        }
      });
    });

    effect(() => {
      this.renderedContent();
      queueMicrotask(() => this.assignHeadingIds());
    });
  }

  protected groupOpen(name: string): boolean {
    return this.expandedGroups().has(name);
  }

  protected groupListId(name: string): string {
    return `docs-group-${slugify(name)}`;
  }

  protected toggleGroup(name: string): void {
    this.expandedGroups.update((groups) => {
      const next = new Set(groups);
      if (next.has(name)) {
        next.delete(name);
      } else {
        next.add(name);
      }
      return next;
    });
  }

  protected taxonomyLabel(page: DocPage): string {
    if (!page.subsystem) {
      return '';
    }
    return page.category && page.category !== page.subsystem
      ? `${page.subsystem} / ${page.category}`
      : page.subsystem;
  }

  protected setQuery(event: Event): void {
    const query = (event.target as HTMLInputElement).value;
    this.query.set(query);
    if (!query.trim()) {
      return;
    }
    this.expandMatchingGroups();
  }

  protected setSubsystem(event: Event): void {
    this.subsystemFilter.set((event.target as HTMLSelectElement).value);
    const categories = new Set(this.categoryFacets().map((facet) => facet.name));
    if (this.categoryFilter() !== TAXONOMY_ALL && !categories.has(this.categoryFilter())) {
      this.categoryFilter.set(TAXONOMY_ALL);
    }
    this.expandMatchingGroups();
  }

  protected setCategory(event: Event): void {
    this.categoryFilter.set((event.target as HTMLSelectElement).value);
    this.expandMatchingGroups();
  }

  private facetCounts(
    pages: readonly DocPage[],
    keyOf: (page: DocPage) => string | undefined,
  ): readonly FacetCount[] {
    const counts = new Map<string, number>();
    for (const page of pages) {
      const key = keyOf(page);
      if (key) {
        counts.set(key, (counts.get(key) ?? 0) + 1);
      }
    }
    return [...counts].map(([name, count]) => ({ name, count }));
  }

  private expandMatchingGroups(): void {
    const query = this.query().trim().toLowerCase();
    const subsystem = this.subsystemFilter();
    const category = this.categoryFilter();
    const matching = new Set(
      this.pages()
        .filter(
          (page) =>
            pageMatches(page, query) && taxonomyMatches(page, subsystem, category),
        )
        .map((page) => page.group),
    );
    this.expandedGroups.update((groups) => {
      const next = new Set(groups);
      for (const name of matching) {
        next.add(name);
      }
      return next;
    });
  }

  protected selectPage(slug: string): void {
    this.pageSelected.emit(slug);
  }

  protected backToIndex(): void {
    this.indexSelected.emit();
  }

  protected retry(): void {
    this.retryRequested.emit();
  }

  protected onMarkdownClick(event: MouseEvent): void {
    const target = event.target;
    if (!(target instanceof Element)) {
      return;
    }
    const anchor = target.closest('a') as HTMLAnchorElement | null;
    const href = anchor?.getAttribute('href') ?? '';
    if (!anchor || !href.startsWith('#')) {
      return;
    }
    const destination = this.markdown()?.nativeElement.querySelector<HTMLElement>(href);
    if (!destination) {
      return;
    }
    event.preventDefault();
    destination.scrollIntoView({ block: 'start', behavior: 'smooth' });
    history.replaceState(null, '', `${location.pathname}${location.search}${href}`);
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

  private assignHeadingIds(): void {
    const root = this.markdown()?.nativeElement;
    if (!root) {
      return;
    }
    for (const heading of root.querySelectorAll<HTMLElement>('h1, h2, h3, h4, h5, h6')) {
      heading.id = `doc-${slugify(heading.textContent ?? '')}`;
    }
    const hash = location.hash.slice(1);
    if (hash) {
      document.getElementById(hash)?.scrollIntoView({ block: 'start' });
    }
  }
}
