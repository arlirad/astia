import {
  ChangeDetectionStrategy,
  Component,
  computed,
  effect,
  inject,
  signal,
} from '@angular/core';
import { toSignal } from '@angular/core/rxjs-interop';
import { ActivatedRoute, Router } from '@angular/router';
import { map } from 'rxjs';
import { DOC_PAGES } from '../content';
import { DocsBrowser } from '../docs/docs-browser';
import { DOCS_MARKDOWN_RENDERER } from '../docs/docs-markdown';
import type { DocPage, DocsLoadStatus } from '../docs/docs.model';
import { DOCS_SOURCE, DocsNotFoundError } from '../docs/docs.repository';

@Component({
  selector: 'app-documentation-page',
  imports: [DocsBrowser],
  templateUrl: './documentation-page.html',
  styleUrl: './documentation-page.scss',
  changeDetection: ChangeDetectionStrategy.OnPush,
})
export class DocumentationPage {
  protected readonly docPages = DOC_PAGES;
  protected readonly status = signal<DocsLoadStatus>('idle');
  protected readonly errorMessage = signal<string | null>(null);

  private readonly route = inject(ActivatedRoute);
  private readonly router = inject(Router);
  private readonly source = inject(DOCS_SOURCE);
  private readonly markdownRenderer = inject(DOCS_MARKDOWN_RENDERER);
  private readonly pagesBySlug = new Map(DOC_PAGES.map((page) => [page.slug, page]));
  private readonly content = signal<string | null>(null);
  private readonly routeSlug = toSignal(
    this.route.paramMap.pipe(map((params) => params.get('slug'))),
    { initialValue: null },
  );
  private requestToken = 0;

  protected readonly selectedPage = computed(() => {
    const slug = this.routeSlug();
    return slug ? (this.pagesBySlug.get(slug) ?? null) : null;
  });
  protected readonly selectedSlug = computed(() => this.selectedPage()?.slug ?? null);
  protected readonly sourceUrl = computed(() => {
    const page = this.selectedPage();
    return page ? this.source.sourceUrl(page) : null;
  });
  protected readonly renderedContent = computed(() => {
    const page = this.selectedPage();
    const markdown = this.content();
    return page && markdown ? this.markdownRenderer.render(markdown, page.sourcePath) : '';
  });

  constructor() {
    effect(() => {
      const slug = this.routeSlug();
      if (slug && !this.pagesBySlug.has(slug)) {
        void this.router.navigate(['/documentation'], { replaceUrl: true });
      }
    });

    effect(() => {
      const page = this.selectedPage();
      if (!page) {
        this.requestToken += 1;
        this.content.set(null);
        this.errorMessage.set(null);
        this.status.set('idle');
        return;
      }
      void this.load(page);
    });
  }

  protected openPage(slug: string): void {
    void this.router.navigate(['/documentation', slug]);
  }

  protected openIndex(): void {
    void this.router.navigate(['/documentation']);
  }

  protected reload(): void {
    const page = this.selectedPage();
    if (page) {
      void this.load(page);
    }
  }

  private async load(page: DocPage): Promise<void> {
    const token = ++this.requestToken;
    this.content.set(null);
    this.errorMessage.set(null);
    this.status.set('loading');
    try {
      const markdown = await this.source.load(page);
      if (token !== this.requestToken) {
        return;
      }
      this.content.set(markdown);
      this.status.set('ready');
    } catch (error) {
      if (token !== this.requestToken) {
        return;
      }
      this.content.set(null);
      if (error instanceof DocsNotFoundError) {
        this.status.set('not-found');
        return;
      }
      this.status.set('error');
      this.errorMessage.set(
        error instanceof Error && error.message
          ? error.message
          : 'The documentation could not be loaded.',
      );
    }
  }
}
