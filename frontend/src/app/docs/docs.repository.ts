import { InjectionToken, inject } from '@angular/core';
import type { DocPage } from './docs.model';

export interface DocsRepositoryConfig {
  readonly owner: string;
  readonly repo: string;
  readonly ref: string;
  readonly githubHost: string;
  readonly rawHost: string;
}

export const DEFAULT_DOCS_REPOSITORY_CONFIG: DocsRepositoryConfig = {
  owner: 'arlirad',
  repo: 'astia',
  ref: 'main',
  githubHost: 'https://github.com',
  rawHost: 'https://raw.githubusercontent.com',
};

export const DOCS_REPOSITORY_CONFIG = new InjectionToken<DocsRepositoryConfig>(
  'DOCS_REPOSITORY_CONFIG',
  { providedIn: 'root', factory: () => DEFAULT_DOCS_REPOSITORY_CONFIG },
);

export const DOCS_FETCH = new InjectionToken<typeof fetch>('DOCS_FETCH', {
  providedIn: 'root',
  factory: () => globalThis.fetch.bind(globalThis),
});

export class DocsNotFoundError extends Error {
  constructor(message = 'Documentation page not found.') {
    super(message);
    this.name = 'DocsNotFoundError';
  }
}

export class DocsFetchError extends Error {
  constructor(
    message: string,
    readonly status?: number,
  ) {
    super(message);
    this.name = 'DocsFetchError';
  }
}

export interface DocsLinkResolver {
  resolveLink(target: string, sourcePath: string): string | null;
  resolveImage(target: string, sourcePath: string): string | null;
}

export interface DocsSource extends DocsLinkResolver {
  rawUrl(path: string): string;
  sourceUrl(page: DocPage): string;
  load(page: DocPage): Promise<string>;
}

export const DOCS_SOURCE = new InjectionToken<DocsSource>('DOCS_SOURCE', {
  providedIn: 'root',
  factory: () => new GitHubDocsSource(inject(DOCS_REPOSITORY_CONFIG), inject(DOCS_FETCH)),
});

const SAFE_PROTOCOLS = /^(?:https?:|mailto:|tel:)/i;
const IMAGE_PROTOCOLS = /^https?:/i;
const ABSOLUTE_PROTOCOL = /^[a-z][a-z0-9+.-]*:/i;
const FILE_LIKE = new Set(['LICENSE', 'Makefile', 'CMakeLists.txt', 'Dockerfile']);

const encodeSegments = (value: string): string =>
  value
    .split('/')
    .map((segment) => encodeURIComponent(segment))
    .join('/');

const encodePath = (path: string): string => encodeSegments(path.replace(/^\/+/, ''));

const splitSuffix = (target: string): { path: string; suffix: string } => {
  const hashIndex = target.indexOf('#');
  const queryIndex = target.indexOf('?');
  const cut = Math.min(
    hashIndex < 0 ? target.length : hashIndex,
    queryIndex < 0 ? target.length : queryIndex,
  );
  return { path: target.slice(0, cut), suffix: target.slice(cut) };
};

const resolveRelative = (base: string, target: string): string | null => {
  const parts = target.startsWith('/') ? [] : base.split('/').slice(0, -1);
  for (const raw of target.replace(/^\/+/, '').split('/')) {
    if (!raw || raw === '.') {
      continue;
    }
    if (raw === '..') {
      if (parts.length === 0) {
        return null;
      }
      parts.pop();
      continue;
    }
    parts.push(raw);
  }
  return parts.length > 0 ? parts.join('/') : null;
};

const looksLikeDirectory = (target: string, decoded: string): boolean => {
  const name = decoded.split('/').filter(Boolean).at(-1) ?? '';
  return target.endsWith('/') || (!name.includes('.') && !FILE_LIKE.has(name));
};

const languageFor = (path: string): string => {
  const extension = path.split('.').at(-1)?.toLowerCase() ?? '';
  return extension === 'c' ? 'c' : extension === 'h' ? 'c' : 'cpp';
};

const longestBacktickRun = (value: string): number =>
  Math.max(0, ...[...value.matchAll(/`+/g)].map((match) => match[0].length));

export class GitHubDocsSource implements DocsSource {
  constructor(
    private readonly config: DocsRepositoryConfig = DEFAULT_DOCS_REPOSITORY_CONFIG,
    private readonly fetchImpl: typeof fetch = globalThis.fetch.bind(globalThis),
  ) {}

  rawUrl(path: string): string {
    return [
      this.config.rawHost.replace(/\/+$/, ''),
      encodeURIComponent(this.config.owner),
      encodeURIComponent(this.config.repo),
      encodeSegments(this.config.ref),
      encodePath(path),
    ].join('/');
  }

  sourceUrl(page: DocPage): string {
    return [
      this.config.githubHost.replace(/\/+$/, ''),
      encodeURIComponent(this.config.owner),
      encodeURIComponent(this.config.repo),
      'blob',
      encodeSegments(this.config.ref),
      encodePath(page.implementationPath ?? page.sourcePath),
    ].join('/');
  }

  async load(page: DocPage): Promise<string> {
    const url = this.rawUrl(page.sourcePath);
    let response: Response;
    try {
      response = await this.fetchImpl(url, { headers: { Accept: 'text/plain' } });
    } catch {
      throw new DocsFetchError(
        `Unable to reach ${this.config.rawHost} for ${page.sourcePath}.`,
      );
    }
    if (response.status === 404) {
      throw new DocsNotFoundError(`Documentation source not found: ${page.sourcePath}`);
    }
    if (!response.ok) {
      throw new DocsFetchError(
        `Unable to load ${page.sourcePath} from the public repository (HTTP ${response.status}).`,
        response.status,
      );
    }
    const body = await response.text();
    return page.contentKind === 'markdown' ? body : this.renderSourcePage(page, body);
  }

  resolveLink(target: string, sourcePath: string): string | null {
    if (target.startsWith('#')) {
      return target;
    }
    if (target.startsWith('//')) {
      return null;
    }
    if (SAFE_PROTOCOLS.test(target)) {
      return target;
    }
    if (ABSOLUTE_PROTOCOL.test(target)) {
      return null;
    }

    const { path, suffix } = splitSuffix(target);
    let decoded: string;
    try {
      decoded = decodeURIComponent(path).replace(/^<|>$/g, '');
    } catch {
      return null;
    }
    const resolved = resolveRelative(sourcePath, decoded);
    if (!resolved) {
      return null;
    }
    const kind = looksLikeDirectory(path, decoded) ? 'tree' : 'blob';
    return [
      this.config.githubHost.replace(/\/+$/, ''),
      encodeURIComponent(this.config.owner),
      encodeURIComponent(this.config.repo),
      kind,
      encodeSegments(this.config.ref),
      encodePath(resolved) + suffix,
    ].join('/');
  }

  resolveImage(target: string, sourcePath: string): string | null {
    if (IMAGE_PROTOCOLS.test(target)) {
      return target;
    }
    if (ABSOLUTE_PROTOCOL.test(target) || target.startsWith('//')) {
      return null;
    }
    const { path } = splitSuffix(target);
    let decoded: string;
    try {
      decoded = decodeURIComponent(path).replace(/^<|>$/g, '');
    } catch {
      return null;
    }
    const resolved = resolveRelative(sourcePath, decoded);
    return resolved ? this.rawUrl(resolved) : null;
  }

  private renderSourcePage(page: DocPage, source: string): string {
    const parts: string[] = [];
    if (page.synopsis) {
      if (page.synopsisKind === 'raw') {
        parts.push('## SYNOPSIS', '', page.synopsis);
      } else {
        parts.push('## SYNOPSIS', '', '```c', page.synopsis, '```');
      }
    }
    const fence = '`'.repeat(Math.max(3, longestBacktickRun(source) + 1));
    parts.push(
      '## REPOSITORY SOURCE',
      '',
      `${fence}${languageFor(page.sourcePath)}`,
      source,
      fence,
    );
    return parts.join('\n');
  }
}
