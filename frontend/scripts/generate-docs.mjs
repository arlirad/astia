import {
  existsSync,
  readFileSync,
  readdirSync,
  statSync,
  writeFileSync,
} from 'node:fs';
import { dirname, join, posix, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const frontendRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const repoRoot = resolve(frontendRoot, '..');
const docsDir = join(repoRoot, 'docs');
const outputPath = join(frontendRoot, 'src/app/docs/docs.generated.ts');
const guides = [
  'README.md',
  'kernel/README.md',
  'toolchain/README.md',
  'modules/README.md',
  'libc/README.md',
  'pkg/README.md',
];

if (!existsSync(docsDir)) {
  console.log('Documentation sources are unavailable; keeping the existing generated docs file.');
  process.exit(0);
}

const githubRepo = 'https://github.com/arlirad/astia';
let rewrittenLinks = 0;

const normalize = (text) => text.replace(/\u2014/g, ' - ');

const rewriteTarget = (target, sourcePath) => {
  if (/^(?:[a-z][a-z0-9+.-]*:|\/\/|#)/i.test(target)) {
    return target;
  }

  const hashIndex = target.indexOf('#');
  const pathPart = hashIndex >= 0 ? target.slice(0, hashIndex) : target;
  const hash = hashIndex >= 0 ? target.slice(hashIndex) : '';
  if (!pathPart) {
    return target;
  }

  let decoded = pathPart.replace(/^<|>$/g, '');
  try {
    decoded = decodeURIComponent(decoded);
  } catch {
    return target;
  }

  const resolved = posix.normalize(posix.join(posix.dirname(sourcePath), decoded));
  if (resolved === '..' || resolved.startsWith('../')) {
    return target;
  }

  const absolute = join(repoRoot, resolved);
  if (!existsSync(absolute)) {
    console.warn(`Unresolved relative link ${target} in ${sourcePath}`);
    return target;
  }

  const kind = statSync(absolute).isDirectory() ? 'tree' : 'blob';
  const encoded = resolved.split('/').map(encodeURIComponent).join('/');
  rewrittenLinks += 1;
  return `${githubRepo}/${kind}/main/${encoded}${hash}`;
};

const rewriteLinks = (content, sourcePath) =>
  content.replace(
    /(?<!!)\[([^\]]*)\]\(([^)\s]+)\)/g,
    (match, text, target) => `[${text}](${rewriteTarget(target, sourcePath)})`,
  );

const guideTitles = {
  'README.md': 'Project README',
  'kernel/README.md': 'Kernel README',
};

const plain = (text) =>
  text
    .replace(/<!--[\s\S]*?-->/g, ' ')
    .replace(/<\/?(?:p|img|br|div|span|a|table|thead|tbody|tr|td|th|ul|ol|li|h[1-6]|blockquote|pre|code|hr|strong|em|sub|sup)\b[^>]*>/gi, ' ')
    .replace(/```[\s\S]*?```/g, ' ')
    .replace(/`([^`]*)`/g, '$1')
    .replace(/\[([^\]]+)\]\([^)]+\)/g, '$1')
    .replace(/(^|\s)>\s?/g, '$1')
    .replace(/[|*#]/g, ' ')
    .replace(/\s+/g, ' ')
    .trim();

const titleOf = (content, path) => {
  const heading = content.match(/^#\s+(.+)$/m);
  return heading ? plain(heading[1]) : path;
};

const summaryOf = (content) => {
  const body = content
    .replace(/^#\s+.+$/m, '')
    .replace(/^#{1,6}\s+.+$/gm, ' ')
    .replace(/^\|.*$/gm, ' ')
    .replace(/^```[\s\S]*?```$/gm, ' ');
  const text = plain(body);
  return text.length > 180 ? `${text.slice(0, 177).trimEnd()}...` : text;
};

const slugOf = (path) =>
  path
    .replace(/\.md$/i, '')
    .replace(/[^a-z0-9]+/gi, '-')
    .replace(/^-+|-+$/g, '')
    .toLowerCase();

const entries = [
  ...guides
    .filter((path) => existsSync(join(repoRoot, path)))
    .map((path) => ({ path, group: 'Guide' })),
  ...readdirSync(docsDir)
    .filter((name) => name.endsWith('.md'))
    .sort()
    .map((name) => ({
      path: `docs/${name}`,
      group: /^.+\.\d+\.md$/i.test(name) ? 'Manual page' : 'Design note',
    })),
];

const pages = entries.map(({ path, group }) => {
  const content = rewriteLinks(
    normalize(readFileSync(join(repoRoot, path), 'utf8')),
    path,
  );
  return {
    slug: slugOf(path),
    title: guideTitles[path] ?? titleOf(content, path),
    group,
    summary: summaryOf(content),
    source: `https://github.com/arlirad/astia/blob/main/${path}`,
    content,
  };
});

const body = pages
  .map(
    (page) => `  {
    slug: ${JSON.stringify(page.slug)},
    title: ${JSON.stringify(page.title)},
    group: ${JSON.stringify(page.group)},
    summary: ${JSON.stringify(page.summary)},
    source: ${JSON.stringify(page.source)},
    content: ${JSON.stringify(page.content)},
  }`,
  )
  .join(',\n');

writeFileSync(
  outputPath,
  `import type { DocPage } from './docs.model';\n\nexport const GENERATED_DOCS: readonly DocPage[] = [\n${body}\n];\n`,
);
console.log(
  `Generated ${pages.length} documentation pages from repository Markdown sources (${rewrittenLinks} links rewritten).`,
);
