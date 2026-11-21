import { ChangeDetectionStrategy, Component, computed, effect, inject } from '@angular/core';
import { toSignal } from '@angular/core/rxjs-interop';
import { ActivatedRoute, Router } from '@angular/router';
import { map } from 'rxjs';
import { DOC_PAGES } from '../content';
import { DocsBrowser } from '../docs/docs-browser';

@Component({
  selector: 'app-documentation-page',
  imports: [DocsBrowser],
  templateUrl: './documentation-page.html',
  styleUrl: './documentation-page.scss',
  changeDetection: ChangeDetectionStrategy.OnPush,
})
export class DocumentationPage {
  protected readonly docPages = DOC_PAGES;

  private readonly route = inject(ActivatedRoute);
  private readonly router = inject(Router);
  private readonly validSlugs = new Set(DOC_PAGES.map((page) => page.slug));
  private readonly routeSlug = toSignal(
    this.route.paramMap.pipe(map((params) => params.get('slug'))),
    { initialValue: null },
  );

  protected readonly selectedSlug = computed(() => {
    const slug = this.routeSlug();
    return slug && this.validSlugs.has(slug) ? slug : null;
  });

  constructor() {
    effect(() => {
      const slug = this.routeSlug();
      if (slug && !this.validSlugs.has(slug)) {
        void this.router.navigate(['/documentation'], { replaceUrl: true });
      }
    });
  }

  protected openPage(slug: string): void {
    void this.router.navigate(['/documentation', slug]);
  }

  protected openIndex(): void {
    void this.router.navigate(['/documentation']);
  }
}
