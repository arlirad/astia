import { TestBed } from '@angular/core/testing';
import { ActivatedRoute, Router, convertToParamMap, type ParamMap } from '@angular/router';
import { BehaviorSubject } from 'rxjs';
import { DOCS_MARKDOWN_RENDERER, type DocsMarkdownRenderer } from '../docs/docs-markdown';
import {
  DOCS_SOURCE,
  DocsFetchError,
  DocsNotFoundError,
  type DocsSource,
} from '../docs/docs.repository';
import { DocumentationPage } from './documentation-page';

function setup(slug: string): {
  fixture: ReturnType<typeof TestBed.createComponent<DocumentationPage>>;
  source: DocsSource;
  router: { navigate: jasmine.Spy };
} {
  const params = new BehaviorSubject<ParamMap>(convertToParamMap({ slug }));
  const router = { navigate: jasmine.createSpy('navigate').and.returnValue(Promise.resolve(true)) };
  const source: DocsSource = {
    rawUrl: () => 'https://raw.githubusercontent.com/arlirad/astia/main/README.md',
    sourceUrl: () => 'https://github.com/arlirad/astia/blob/main/README.md',
    resolveLink: () => null,
    resolveImage: () => null,
    load: jasmine.createSpy('load').and.returnValue(Promise.resolve('# Project README\n\nBody')),
  };

  TestBed.configureTestingModule({
    imports: [DocumentationPage],
    providers: [
      { provide: ActivatedRoute, useValue: { paramMap: params.asObservable() } },
      { provide: Router, useValue: router },
      { provide: DOCS_SOURCE, useValue: source },
      {
        provide: DOCS_MARKDOWN_RENDERER,
        useValue: {
          render: () => '<p>Rendered documentation</p>',
        } as unknown as DocsMarkdownRenderer,
      },
    ],
  });

  return { fixture: TestBed.createComponent(DocumentationPage), source, router };
}

async function flush(
  fixture: ReturnType<typeof TestBed.createComponent<DocumentationPage>>,
): Promise<HTMLElement> {
  fixture.detectChanges();
  await fixture.whenStable();
  await Promise.resolve();
  await Promise.resolve();
  fixture.detectChanges();
  return fixture.nativeElement as HTMLElement;
}

describe('DocumentationPage runtime loading', () => {
  it('loads the selected page and renders it', async () => {
    const { fixture, source } = setup('readme');
    const element = await flush(fixture);

    expect((source.load as jasmine.Spy).calls.mostRecent().args[0].sourcePath).toBe(
      'README.md',
    );
    expect(element.querySelector('.doc-markdown')?.textContent).toContain(
      'Rendered documentation',
    );
    expect(element.querySelector('.docs-source')?.getAttribute('href')).toContain(
      'README.md',
    );
  });

  it('shows a not-found state for a missing source', async () => {
    const { fixture, source } = setup('readme');
    (source.load as jasmine.Spy).and.returnValue(
      Promise.reject(new DocsNotFoundError()),
    );
    const element = await flush(fixture);

    expect(element.querySelector('[role="alert"]')?.textContent).toContain(
      'not available',
    );
  });

  it('shows an error state and retries the fetch', async () => {
    const { fixture, source } = setup('readme');
    const load = source.load as jasmine.Spy;
    load.and.returnValue(Promise.reject(new DocsFetchError('HTTP 503', 503)));
    let element = await flush(fixture);

    expect(element.querySelector('[role="alert"]')?.textContent).toContain('HTTP 503');

    load.and.returnValue(Promise.resolve('# Project README'));
    element.querySelector<HTMLButtonElement>('.docs-state button')?.click();
    element = await flush(fixture);

    expect(load.calls.count()).toBe(2);
    expect(element.querySelector('.doc-markdown')?.textContent).toContain(
      'Rendered documentation',
    );
  });

  it('redirects an unknown slug back to the documentation index', async () => {
    const { fixture, router } = setup('missing-page');
    await flush(fixture);

    expect(router.navigate).toHaveBeenCalledWith(['/documentation'], {
      replaceUrl: true,
    });
  });
});
