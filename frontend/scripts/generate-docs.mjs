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
const authoredManifestPath = join(docsDir, 'manifest.json');
const guides = [
  'README.md',
  'kernel/README.md',
  'toolchain/README.md',
  'modules/README.md',
  'libc/README.md',
  'pkg/README.md',
];
const EXCLUDED_DOCS = new Set(['aarch64-virt-boot.md']);
const AUTHORED_SYSCALL_IDS = [...Array.from({ length: 71 }, (_, index) => index), 77, 78, 79];
const DL_SYSCALL_IDS = [71, 72];
const MOD_SYSCALL_IDS = [73, 74, 75, 76];
const CANONICAL_SYSCALL_IDS = Array.from({ length: 80 }, (_, index) => index);

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
const fail = (message) => {
  throw new Error(message);
};
const requireValue = (condition, message) => {
  if (!condition) {
    fail(message);
  }
};

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

const readAuthoredManifest = () => {
  requireValue(existsSync(authoredManifestPath), `missing authored docs manifest: ${authoredManifestPath}`);
  const manifest = JSON.parse(readFileSync(authoredManifestPath, 'utf8'));
  requireValue(manifest?.section2?.pages?.length === CANONICAL_SYSCALL_IDS.length, 'authored manifest must contain 74 syscall pages, 2 dl pages, and 4 mod pages');
  const authoredIds = manifest.section2.pages
    .filter((page) => AUTHORED_SYSCALL_IDS.includes(page.id))
    .map((page) => page.id);
  const dlPages = manifest.section2.pages.filter((page) => DL_SYSCALL_IDS.includes(page.id));
  const modPages = manifest.section2.pages.filter((page) => MOD_SYSCALL_IDS.includes(page.id));
  requireValue(authoredIds.length === AUTHORED_SYSCALL_IDS.length, 'authored manifest must contain 74 syscall pages');
  requireValue(dlPages.length === DL_SYSCALL_IDS.length, 'authored manifest must contain 2 dl pages');
  requireValue(modPages.length === MOD_SYSCALL_IDS.length, 'authored manifest must contain 4 mod pages');
  for (const page of dlPages) {
    requireValue(page.subsystem === 'dl' && page.category === 'dl', `${page.name}: dl pages must declare the dl taxonomy`);
    requireValue(page.sourcePath === `docs/2/dl/${page.name}.md`, `${page.name}: dl pages must use docs/2/dl`);
  }
  for (const page of modPages) {
    requireValue(page.subsystem === 'mod' && page.category === 'mod', `${page.name}: mod pages must declare the mod taxonomy`);
    requireValue(page.sourcePath === `docs/2/mod/${page.name}.md`, `${page.name}: mod pages must use docs/2/mod`);
  }
  requireValue(manifest?.section7?.pages?.length === 7, 'authored manifest must contain 7 overview pages');
  for (const page of manifest.section2.pages) {
    requireValue(!('content' in page), 'authored syscall metadata must not embed Markdown bodies');
    requireValue(typeof page.sourcePath === 'string' && page.sourcePath.startsWith('docs/2/'), `${page.name}: missing authored sourcePath`);
    requireValue(typeof page.subsystem === 'string' && page.subsystem.length > 0, `${page.name}: missing subsystem`);
    requireValue(typeof page.category === 'string' && page.category.length > 0, `${page.name}: missing category`);
  }
  for (const page of manifest.section7.pages) {
    requireValue(!('content' in page), 'authored overview metadata must not embed Markdown bodies');
  }
  return manifest;
};

const authoredManifest = readAuthoredManifest();
const authoredByName = new Map(authoredManifest.section2.pages.map((page) => [page.name, page]));
const authoredSection7 = new Map(authoredManifest.section7.pages.map((page) => [page.name, page]));

const entries = [
  ...guides
    .filter((path) => existsSync(join(repoRoot, path)))
    .map((path) => ({ path, group: 'Guide' })),
  ...readdirSync(docsDir)
    .filter((name) => name.endsWith('.md') && !EXCLUDED_DOCS.has(name))
    .sort()
    .map((name) => ({
      path: `docs/${name}`,
      group: 'Design note',
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

for (const name of authoredSection7.keys()) {
  requireValue(existsSync(join(repoRoot, 'docs/7', `${name}.md`)), `missing docs/7/${name}.md`);
}

const overviewPages = [...authoredSection7.values()].map((page) => {
  const content = normalize(readFileSync(join(repoRoot, page.source), 'utf8'));
  const result = {
    slug: page.slug,
    title: page.title,
    group: 'Manual page',
    summary: summaryOf(content),
    sourcePath: page.source,
    contentKind: 'markdown',
  };
  if (page.aliases?.length) {
    result.aliases = page.aliases;
  }
  return result;
});

const manifestPath = join(syscallExportDir, 'manifest.json');
requireValue(existsSync(manifestPath), `missing section-2 manifest: ${manifestPath}`);
const manifest = JSON.parse(readFileSync(manifestPath, 'utf8'));
requireValue(manifest.section === 2, 'section-2 export has the wrong section');
requireValue(manifest.count === CANONICAL_SYSCALL_IDS.length, 'section-2 export has the wrong count');
requireValue(manifest.pages.length === CANONICAL_SYSCALL_IDS.length, 'section-2 export is incomplete');

const syscallIds = manifest.pages.map((page) => page.id);
const sortedSyscallIds = [...syscallIds].sort((a, b) => a - b);
requireValue(new Set(syscallIds).size === CANONICAL_SYSCALL_IDS.length, 'section-2 export has duplicate IDs');
requireValue(sortedSyscallIds.every((id, index) => id === CANONICAL_SYSCALL_IDS[index]), 'section-2 export IDs must be 0..79');

const seenSlugs = new Set(basePages.map((page) => page.slug));
const seenSourcePaths = new Set(basePages.map((page) => page.sourcePath));
for (const page of overviewPages) {
  requireValue(!seenSlugs.has(page.slug), `duplicate overview slug: ${page.slug}`);
  seenSlugs.add(page.slug);
  for (const alias of page.aliases ?? []) {
    requireValue(!seenSlugs.has(alias), `duplicate overview alias: ${alias}`);
    seenSlugs.add(alias);
  }
  requireValue(!seenSourcePaths.has(page.sourcePath), `duplicate overview source: ${page.sourcePath}`);
  seenSourcePaths.add(page.sourcePath);
}

const syscallPages = manifest.pages.map((entry) => {
  const authored = authoredByName.get(entry.name);
  requireValue(authored, `section-2 export contains unknown syscall: ${entry.name}`);
  requireValue(authored.id === entry.id, `${entry.name}: exported ID differs from the authored manifest`);
  requireValue(authored.slug === entry.slug, `${entry.name}: exported slug differs from the authored manifest`);
  requireValue(authored.source === entry.source, `${entry.name}: exported source differs from the authored manifest`);
  requireValue(authored.sourcePath === entry.source_path, `${entry.name}: exported source path differs from the authored manifest`);
  requireValue(authored.subsystem === entry.subsystem, `${entry.name}: exported subsystem differs from the authored manifest`);
  requireValue(authored.category === entry.category, `${entry.name}: exported category differs from the authored manifest`);
  requireValue(authored.synopsisKind === entry.synopsis_kind, `${entry.name}: exported synopsis kind differs from the authored manifest`);
  requireValue(authored.synopsis === entry.synopsis, `${entry.name}: exported synopsis differs from the authored manifest`);
  requireValue(!seenSlugs.has(entry.slug), `duplicate syscall slug: ${entry.slug}`);
  seenSlugs.add(entry.slug);
  const sourcePath = entry.source_path;
  requireValue(!seenSourcePaths.has(sourcePath), `duplicate syscall source: ${sourcePath}`);
  seenSourcePaths.add(sourcePath);
  requireValue(existsSync(join(repoRoot, sourcePath)), `missing authored syscall page: ${sourcePath}`);
  const content = normalize(readFileSync(join(repoRoot, sourcePath), 'utf8'));
  requireValue(synopsisOf(content) === entry.synopsis, `${entry.name}: authored Markdown synopsis differs`);
  return {
    slug: entry.slug,
    title: entry.title,
    group: 'Syscall (2)',
    summary: summaryOf(content),
    sourcePath,
    contentKind: 'markdown',
    implementationPath: entry.source,
    subsystem: entry.subsystem,
    category: entry.category,
    syscallId: entry.id,
    synopsisKind: entry.synopsis_kind,
    synopsis: normalize(entry.synopsis),
  };
});

const pages = [...basePages, ...overviewPages, ...syscallPages];
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
    if (page.implementationPath) {
      fields.push(`implementationPath: ${JSON.stringify(page.implementationPath)}`);
    }
    if (page.aliases?.length) {
      fields.push(`aliases: ${JSON.stringify(page.aliases)}`);
    }
    if (page.subsystem) {
      fields.push(`subsystem: ${JSON.stringify(page.subsystem)}`);
    }
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
  `Generated ${basePages.length} repository pages, ${overviewPages.length} overview pages, and ${syscallPages.length} section-2 syscall pages as runtime metadata.`,
);
