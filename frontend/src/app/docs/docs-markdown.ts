import { InjectionToken, inject } from '@angular/core';
import { Marked, type RendererObject } from 'marked';
import { DOCS_SOURCE, type DocsLinkResolver } from './docs.repository';
import { escapeAttribute, escapeHtml, slugify } from './docs-utils';

export class DocsMarkdownRenderer {
  constructor(private readonly resolver: DocsLinkResolver) {}

  render(markdown: string, sourcePath: string): string {
    const resolver = this.resolver;
    const renderer: RendererObject = {
      heading({ tokens, depth }) {
        const html = this.parser.parseInline(tokens);
        return `<h${depth} id="doc-${slugify(html)}">${html}</h${depth}>\n`;
      },
      link({ href, title, tokens }) {
        const html = this.parser.parseInline(tokens);
        const resolved = href.startsWith('#')
          ? `#doc-${slugify(href.slice(1))}`
          : resolver.resolveLink(href, sourcePath);
        if (!resolved) {
          return html;
        }
        const titleAttribute = title ? ` title="${escapeAttribute(title)}"` : '';
        return `<a href="${escapeAttribute(resolved)}"${titleAttribute}>${html}</a>`;
      },
      image({ href, title, text }) {
        const resolved = resolver.resolveImage(href, sourcePath);
        if (!resolved) {
          return escapeHtml(text);
        }
        const titleAttribute = title ? ` title="${escapeAttribute(title)}"` : '';
        return `<img src="${escapeAttribute(resolved)}" alt="${escapeAttribute(text)}"${titleAttribute}>`;
      },
      html() {
        return '';
      },
    };

    const marked = new Marked({ gfm: true });
    marked.use({ renderer });
    return marked.parse(markdown.replace(/^#\s+.+\n+/, ''), { async: false });
  }
}

export const DOCS_MARKDOWN_RENDERER = new InjectionToken<DocsMarkdownRenderer>(
  'DOCS_MARKDOWN_RENDERER',
  {
    providedIn: 'root',
    factory: () => new DocsMarkdownRenderer(inject(DOCS_SOURCE)),
  },
);
