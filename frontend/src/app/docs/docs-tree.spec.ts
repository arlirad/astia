import type { DocPage } from './docs.model';
import {
  buildDocTree,
  countDocTreePages,
  docTreeAncestorKeys,
  filterDocTree,
  flattenDocTree,
  pageMatches,
  taxonomyMatches,
} from './docs-tree';

const page = (
  slug: string,
  title: string,
  group: DocPage['group'],
  overrides: Partial<DocPage> = {},
): DocPage => ({
  slug,
  title,
  group,
  summary: `${title} summary.`,
  sourcePath: `docs/${slug}.md`,
  contentKind: 'markdown',
  ...overrides,
});

const SYSCALL = 'Syscall (2)';

const PAGES: readonly DocPage[] = [
  page('syscall-0-alpha', 'alpha(2)', SYSCALL, {
    subsystem: 'proc',
    category: 'core',
    syscallId: 0,
  }),
  page('syscall-1-beta', 'beta(2)', SYSCALL, {
    subsystem: 'proc',
    category: 'process',
    syscallId: 1,
  }),
  page('syscall-2-gamma', 'gamma(2)', SYSCALL, {
    subsystem: 'proc',
    category: 'thread',
    syscallId: 2,
  }),
  page('syscall-3-delta', 'delta(2)', SYSCALL, {
    subsystem: 'fs',
    category: 'fs',
    syscallId: 3,
  }),
  page('syscall-4-epsilon', 'epsilon(2)', SYSCALL, {
    subsystem: 'abi',
    category: 'abi',
    syscallId: 4,
  }),
  page('syscall-5-dlmap', 'dlmap(2)', SYSCALL, {
    subsystem: 'dl',
    category: 'dl',
    syscallId: 5,
  }),
  page('guide', 'Guide', 'Guide'),
];

describe('docs tree', () => {
  it('builds proc categories and top-level abi, fs, dl, and doc groups', () => {
    const tree = buildDocTree(PAGES);

    expect(tree.map((node) => node.label)).toEqual([
      'proc',
      'fs',
      'abi',
      'dl',
      'Guide',
    ]);
    expect(tree.map((node) => countDocTreePages(node))).toEqual([3, 1, 1, 1, 1]);
    expect(tree[0].kind).toBe('group');
    expect(tree[0].children.map((node) => node.label)).toEqual([
      'core',
      'process',
      'thread',
    ]);
    expect(tree[1].children[0].label).toBe('delta(2)');
    expect(tree[2].children[0].label).toBe('epsilon(2)');
    expect(tree[3].children[0].label).toBe('dlmap(2)');
    expect(tree[4].children[0].label).toBe('Guide');
    expect(tree.reduce((count, node) => count + countDocTreePages(node), 0)).toBe(
      PAGES.length,
    );
  });

  it('flattens only expanded branches with exact levels and positions', () => {
    const tree = buildDocTree(PAGES);

    const collapsed = flattenDocTree(tree, new Set());
    expect(collapsed.length).toBe(5);
    expect(collapsed.every((row) => row.level === 1)).toBeTrue();
    expect(collapsed.every((row) => row.expanded === false)).toBeTrue();

    const expanded = flattenDocTree(
      tree,
      new Set(['taxonomy:proc', 'taxonomy:proc/process']),
    );
    const proc = expanded[0];
    expect(proc).toEqual(
      jasmine.objectContaining({
        label: 'proc',
        level: 1,
        count: 3,
        expanded: true,
        posinset: 1,
        setsize: 5,
      }),
    );
    const process = expanded.find((row) => row.key === 'taxonomy:proc/process');
    expect(process).toEqual(
      jasmine.objectContaining({
        label: 'process',
        level: 2,
        count: 1,
        expanded: true,
        posinset: 2,
        setsize: 3,
      }),
    );
    const beta = expanded.find((row) => row.key === 'page:syscall-1-beta');
    expect(beta).toEqual(
      jasmine.objectContaining({
        label: 'beta(2)',
        level: 3,
        count: 1,
        expanded: false,
        posinset: 1,
        setsize: 1,
      }),
    );
    expect(expanded.some((row) => row.key === 'page:syscall-2-gamma')).toBeFalse();
  });

  it('filters whole branches and reports matching descendant counts', () => {
    const filtered = filterDocTree(buildDocTree(PAGES), (candidate) =>
      pageMatches(candidate, 'beta'),
    );

    expect(filtered.length).toBe(1);
    expect(filtered[0].label).toBe('proc');
    expect(filtered[0].kind).toBe('group');
    expect(countDocTreePages(filtered[0])).toBe(1);
    if (filtered[0].kind === 'group') {
      expect(filtered[0].children[0].label).toBe('process');
      expect(filtered[0].children[0].kind).toBe('group');
      expect(filtered[0].children[0].children[0].label).toBe('beta(2)');
    }

    const none = filterDocTree(buildDocTree(PAGES), (candidate) =>
      pageMatches(candidate, 'missing'),
    );
    expect(none).toEqual([]);
  });

  it('combines taxonomy and text filters', () => {
    expect(taxonomyMatches(PAGES[0], 'proc', 'all')).toBeTrue();
    expect(taxonomyMatches(PAGES[0], 'proc', 'core')).toBeTrue();
    expect(taxonomyMatches(PAGES[0], 'proc', 'process')).toBeFalse();
    expect(taxonomyMatches(PAGES[6], 'all', 'all')).toBeTrue();
    expect(taxonomyMatches(PAGES[6], 'proc', 'all')).toBeFalse();
    expect(pageMatches(PAGES[0], 'alpha')).toBeTrue();
    expect(pageMatches(PAGES[0], 'proc core')).toBeTrue();
    expect(pageMatches(PAGES[0], 'missing')).toBeFalse();
  });

  it('exposes the ancestor nodes needed for deep links and filtering', () => {
    expect(docTreeAncestorKeys(PAGES[1])).toEqual([
      'taxonomy:proc',
      'taxonomy:proc/process',
    ]);
    expect(docTreeAncestorKeys(PAGES[3])).toEqual(['taxonomy:fs']);
    expect(docTreeAncestorKeys(PAGES[5])).toEqual(['taxonomy:dl']);
    expect(docTreeAncestorKeys(PAGES[6])).toEqual(['group:Guide']);
  });
});
