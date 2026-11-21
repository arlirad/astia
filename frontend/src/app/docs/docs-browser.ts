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
import { marked } from 'marked';
import type { DocPage } from './docs.model';

interface DocGroupView {
  readonly name: string;
  readonly pages: readonly DocPage[];
}

const escapeAttribute = (value: string): string =>
  value
    .replace(/&/g, '&amp;')
    .replace(/"/g, '&quot;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;');

const slugify = (value: string): string =>
  value
    .replace(/<[^>]+>/g, '')
    .replace(/&[^;]+;/g, '')
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-+|-+$/g, '') || 'section';

const safeHref = (href: string): boolean =>
  href.startsWith('#') || /^(?:https?:|mailto:|tel:)/i.test(href);

const pageMatches = (page: DocPage, query: string): boolean => {
  if (!query) {
    return true;
  }
  const searchable =
    `${page.title} ${page.group} ${page.category ?? ''} ${page.syscallId ?? ''} ` +
    `${page.synopsisKind ?? ''} ${page.summary}`;
  return searchable.toLowerCase().includes(query);
};

marked.use({
  gfm: true,
  renderer: {
    heading({ tokens, depth }) {
      const html = this.parser.parseInline(tokens);
      return `<h${depth} id="doc-${slugify(html)}">${html}</h${depth}>\n`;
    },
    link({ href, title, tokens }) {
      const html = this.parser.parseInline(tokens);
      const resolved = href.startsWith('#') ? `#doc-${slugify(href.slice(1))}` : href;
      if (!safeHref(resolved)) {
        return html;
      }
      const titleAttribute = title ? ` title="${escapeAttribute(title)}"` : '';
      return `<a href="${escapeAttribute(resolved)}"${titleAttribute}>${html}</a>`;
    },
  },
});

@Component({
  selector: 'app-docs-browser',
  templateUrl: './docs-browser.html',
  styleUrl: './docs-browser.scss',
  changeDetection: ChangeDetectionStrategy.OnPush,
})
export class DocsBrowser {
  readonly pages = input.required<readonly DocPage[]>();
  readonly selectedSlug = input<string | null>(null);
  readonly pageSelected = output<string>();
  readonly indexSelected = output<void>();

  protected readonly query = signal('');
  private readonly expandedGroups = signal<ReadonlySet<string>>(new Set());
  private firstSelectionChange = true;
  private lastSelectedSlug: string | null = null;

  protected readonly selectedPage = computed(() => {
    const slug = this.selectedSlug();
    return slug ? (this.pages().find((page) => page.slug === slug) ?? null) : null;
  });
  protected readonly filteredGroups = computed<readonly DocGroupView[]>(() => {
    const query = this.query().trim().toLowerCase();
    const groups = new Map<string, DocPage[]>();
    for (const page of this.pages()) {
      if (!pageMatches(page, query)) {
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

  protected setQuery(event: Event): void {
    const query = (event.target as HTMLInputElement).value;
    this.query.set(query);
    const normalized = query.trim().toLowerCase();
    if (!normalized) {
      return;
    }
    const matching = new Set(
      this.pages()
        .filter((page) => pageMatches(page, normalized))
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
