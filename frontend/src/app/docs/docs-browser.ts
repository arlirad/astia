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
import {
  buildDocTree,
  countDocTreePages,
  docTreeAncestorKeys,
  docTreeGroupKeys,
  filterDocTree,
  flattenDocTree,
  pageMatches,
  type DocTreeRow,
} from './docs-tree';
import { slugify } from './docs-utils';

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
  protected readonly expandedNodes = signal<ReadonlySet<string>>(new Set());
  protected readonly activeKey = signal<string | null>(null);
  private firstSelectionChange = true;
  private lastSelectedSlug: string | null = null;

  protected readonly selectedPage = computed(() => {
    const slug = this.selectedSlug();
    return slug ? (this.pages().find((page) => page.slug === slug) ?? null) : null;
  });
  protected readonly filteredTree = computed(() => {
    const query = this.query().trim().toLowerCase();
    return filterDocTree(buildDocTree(this.pages()), (page) => pageMatches(page, query));
  });
  protected readonly visibleRows = computed(() =>
    flattenDocTree(this.filteredTree(), this.expandedNodes()),
  );
  protected readonly resultCount = computed(() =>
    this.filteredTree().reduce((count, node) => count + countDocTreePages(node), 0),
  );
  protected readonly effectiveActiveKey = computed(() => {
    const active = this.activeKey();
    const rows = this.visibleRows();
    if (active && rows.some((row) => row.key === active)) {
      return active;
    }
    return rows[0]?.key ?? null;
  });
  protected readonly previousPage = computed(() => this.adjacentPage(-1));
  protected readonly nextPage = computed(() => this.adjacentPage(1));

  private readonly browser = viewChild<ElementRef<HTMLElement>>('browser');
  private readonly markdown = viewChild<ElementRef<HTMLElement>>('markdown');
  private readonly pageTitle = viewChild<ElementRef<HTMLElement>>('pageTitle');
  private readonly filterInput = viewChild<ElementRef<HTMLInputElement>>('filterInput');
  private readonly tree = viewChild<ElementRef<HTMLElement>>('tree');

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
      this.expandKeys(docTreeAncestorKeys(page));
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

  protected toggleNode(key: string): void {
    this.expandedNodes.update((nodes) => {
      const next = new Set(nodes);
      if (next.has(key)) {
        next.delete(key);
      } else {
        next.add(key);
      }
      return next;
    });
  }

  protected activateRow(row: DocTreeRow): void {
    this.activeKey.set(row.key);
    if (row.kind === 'page' && row.page) {
      this.selectPage(row.page.slug);
      return;
    }
    this.toggleNode(row.key);
  }

  protected onRowFocus(row: DocTreeRow): void {
    this.activeKey.set(row.key);
  }

  protected onTreeKeydown(event: KeyboardEvent): void {
    const target = event.target;
    if (!(target instanceof Element)) {
      return;
    }
    const rowElement = target.closest<HTMLElement>('[data-tree-key]');
    const key = rowElement?.getAttribute('data-tree-key');
    const rows = this.visibleRows();
    const index = key ? rows.findIndex((row) => row.key === key) : -1;
    if (index < 0) {
      return;
    }
    const row = rows[index];

    switch (event.key) {
      case 'ArrowDown':
        event.preventDefault();
        this.focusRow(rows[index + 1]?.key);
        return;
      case 'ArrowUp':
        event.preventDefault();
        this.focusRow(rows[index - 1]?.key);
        return;
      case 'ArrowRight':
        event.preventDefault();
        if (row.kind !== 'group') {
          return;
        }
        if (!row.expanded) {
          this.toggleNode(row.key);
          return;
        }
        if (rows[index + 1]?.level === row.level + 1) {
          this.focusRow(rows[index + 1].key);
        }
        return;
      case 'ArrowLeft':
        event.preventDefault();
        if (row.kind === 'group' && row.expanded) {
          this.toggleNode(row.key);
          return;
        }
        for (let parent = index - 1; parent >= 0; parent -= 1) {
          if (rows[parent].level < row.level) {
            this.focusRow(rows[parent].key);
            return;
          }
        }
        return;
      case 'Home':
        event.preventDefault();
        this.focusRow(rows[0]?.key);
        return;
      case 'End':
        event.preventDefault();
        this.focusRow(rows[rows.length - 1]?.key);
        return;
      case 'Enter':
      case ' ':
        event.preventDefault();
        this.activateRow(row);
        return;
      default:
        return;
    }
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
    this.expandMatchingNodes();
  }

  private expandMatchingNodes(): void {
    this.expandKeys(docTreeGroupKeys(this.filteredTree()));
  }

  private expandKeys(keys: readonly string[]): void {
    this.expandedNodes.update((nodes) => {
      const missing = keys.filter((key) => !nodes.has(key));
      if (missing.length === 0) {
        return nodes;
      }
      const next = new Set(nodes);
      for (const key of missing) {
        next.add(key);
      }
      return next;
    });
  }

  private focusRow(key: string | undefined): void {
    if (!key) {
      return;
    }
    this.activeKey.set(key);
    queueMicrotask(() => this.rowElement(key)?.focus());
  }

  private rowElement(key: string): HTMLElement | null {
    const root = this.tree()?.nativeElement;
    if (!root) {
      return null;
    }
    return [...root.querySelectorAll<HTMLElement>('[data-tree-key]')].find(
      (element) => element.getAttribute('data-tree-key') === key,
    ) ?? null;
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
