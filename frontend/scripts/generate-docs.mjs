import { execFileSync } from 'node:child_process';
import { existsSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const frontendRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const repoRoot = resolve(frontendRoot, '..');
const docsDir = join(repoRoot, 'docs');
const generatorPath = join(repoRoot, 'tools/man/generate.py');
const syscallExportDir = join(frontendRoot, '.cache/syscall-docs');
const manOutputDir = join(frontendRoot, '.cache/man7');
const outputPath = join(frontendRoot, 'src/app/docs/docs.manifest.ts');
const guides = [
  'README.md',
  'kernel/README.md',
  'toolchain/README.md',
  'modules/README.md',
  'libc/README.md',
  'pkg/README.md',
];

if (!existsSync(docsDir)) {
  console.log('Documentation sources are unavailable; keeping the existing docs manifest.');
  process.exit(0);
}

execFileSync(
  process.env.PYTHON ?? 'python3',
  [
    generatorPath,
    join(repoRoot, 'kernel/src'),
    manOutputDir,
    '--docs',
    docsDir,
    '--export-section2',
    syscallExportDir,
  ],
  { stdio: 'inherit' },
);

const normalize = (text) => text.replace(/\u2014/g, ' - ');

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

const synopsisOf = (content) => {
  const match = content.match(
    /## SYNOPSIS\s+(?:```c\n([\s\S]*?)\n```|([^\n]+))/,
  );
  return (match?.[1] ?? match?.[2] ?? '').trim();
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

const basePages = entries.map(({ path, group }) => {
  const content = normalize(readFileSync(join(repoRoot, path), 'utf8'));
  return {
    slug: slugOf(path),
    title: guideTitles[path] ?? titleOf(content, path),
    group,
    summary: summaryOf(content),
    sourcePath: path,
    contentKind: 'markdown',
  };
});

const manifestPath = join(syscallExportDir, 'manifest.json');
if (!existsSync(manifestPath)) {
  throw new Error(`missing section-2 manifest: ${manifestPath}`);
}
const canonicalSyscallIds = [
  ...Array.from({ length: 71 }, (_, index) => index),
];
const manifest = JSON.parse(readFileSync(manifestPath, 'utf8'));
if (
  manifest.section !== 2 ||
  manifest.count !== canonicalSyscallIds.length ||
  manifest.pages.length !== canonicalSyscallIds.length
) {
  throw new Error(
    `section-2 manifest must contain exactly ${canonicalSyscallIds.length} pages`,
  );
}
const syscallIds = manifest.pages.map((page) => page.id);
const sortedSyscallIds = [...syscallIds].sort((a, b) => a - b);
if (
  new Set(syscallIds).size !== canonicalSyscallIds.length ||
  sortedSyscallIds.some((id, index) => id !== canonicalSyscallIds[index])
) {
  throw new Error(
    `section-2 manifest must contain canonical public IDs ${canonicalSyscallIds.join(', ')}`,
  );
}

const syscallPages = manifest.pages.map((entry) => {
  const content = normalize(readFileSync(join(syscallExportDir, entry.markdown), 'utf8'));
  return {
    slug: entry.slug,
    title: entry.title,
    group: 'Syscall (2)',
    summary: summaryOf(content),
    sourcePath: entry.source,
    contentKind: 'source',
    category: entry.category,
    syscallId: entry.id,
    synopsisKind: entry.synopsis_kind,
    synopsis: normalize(synopsisOf(content)),
  };
});

const pages = [...basePages, ...syscallPages];
const body = pages
  .map((page) => {
    const fields = [
      `slug: ${JSON.stringify(page.slug)}`,
      `title: ${JSON.stringify(page.title)}`,
      `group: ${JSON.stringify(page.group)}`,
      `summary: ${JSON.stringify(page.summary)}`,
      `sourcePath: ${JSON.stringify(page.sourcePath)}`,
      `contentKind: ${JSON.stringify(page.contentKind)}`,
    ];
    if (page.category) {
      fields.push(`category: ${JSON.stringify(page.category)}`);
    }
    if (page.syscallId !== undefined) {
      fields.push(`syscallId: ${page.syscallId}`);
    }
    if (page.synopsisKind) {
      fields.push(`synopsisKind: ${JSON.stringify(page.synopsisKind)}`);
    }
    if (page.synopsis) {
      fields.push(`synopsis: ${JSON.stringify(page.synopsis)}`);
    }
    return `  {
    ${fields.join(',\n    ')},
  }`;
  })
  .join(',\n');

writeFileSync(
  outputPath,
  `import type { DocPage } from './docs.model';\n\nexport const DOCS_MANIFEST: readonly DocPage[] = [\n${body}\n];\n`,
);
console.log(
  `Generated ${basePages.length} repository pages and ${syscallPages.length} section-2 syscall pages as runtime metadata.`,
);
