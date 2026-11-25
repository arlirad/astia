import type { DocPage } from './docs.model';
import {
  DOC_SYSCALLS_KEY,
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
  page('manual', 'Manual', 'Manual page'),
  page('note', 'Design note', 'Design note'),
];

describe('docs tree', () => {
  it('builds one Syscalls root above nested categories and the doc groups', () => {
    const tree = buildDocTree(PAGES);

    expect(tree.map((node) => node.label)).toEqual([
      'Syscalls (2)',
      'Guide',
      'Manual page',
      'Design note',
    ]);
    expect(tree.map((node) => countDocTreePages(node))).toEqual([6, 1, 1, 1]);

    const syscalls = tree[0];
    expect(syscalls.kind).toBe('group');
    if (syscalls.kind === 'group') {
      expect(syscalls.key).toBe(DOC_SYSCALLS_KEY);
      expect(syscalls.children.map((node) => node.label)).toEqual([
        'proc',
        'fs',
        'abi',
        'dl',
      ]);
      expect(syscalls.children.map((node) => countDocTreePages(node))).toEqual([
        3, 1, 1, 1,
      ]);

      const proc = syscalls.children[0];
      expect(proc.kind).toBe('group');
      if (proc.kind === 'group') {
        expect(proc.children.map((node) => node.label)).toEqual([
          'core',
          'process',
          'thread',
        ]);
        expect(proc.children.map((node) => countDocTreePages(node))).toEqual([1, 1, 1]);
      }

      expect(syscalls.children[1].kind === 'group' && syscalls.children[1].children[0].label)
        .toBe('delta(2)');
      expect(syscalls.children[2].kind === 'group' && syscalls.children[2].children[0].label)
        .toBe('epsilon(2)');
      expect(syscalls.children[3].kind === 'group' && syscalls.children[3].children[0].label)
        .toBe('dlmap(2)');
    }

    expect(tree[1].kind === 'group' && tree[1].children[0].label).toBe('Guide');
    expect(tree[2].kind === 'group' && tree[2].children[0].label).toBe('Manual');
    expect(tree[3].kind === 'group' && tree[3].children[0].label).toBe('Design note');
    expect(tree.reduce((count, node) => count + countDocTreePages(node), 0)).toBe(
      PAGES.length,
    );
  });

  it('omits the Syscalls root when no syscall pages are present', () => {
    const tree = buildDocTree(PAGES.slice(6));

    expect(tree.map((node) => node.label)).toEqual([
      'Guide',
      'Manual page',
      'Design note',
    ]);
  });

  it('flattens only expanded branches with exact levels and positions', () => {
    const tree = buildDocTree(PAGES);

    const collapsed = flattenDocTree(tree, new Set());
    expect(collapsed.length).toBe(4);
    expect(collapsed.every((row) => row.level === 1)).toBeTrue();
    expect(collapsed.every((row) => row.expanded === false)).toBeTrue();

    const expanded = flattenDocTree(
      tree,
      new Set([DOC_SYSCALLS_KEY, 'taxonomy:proc', 'taxonomy:proc/process']),
    );
    expect(expanded[0]).toEqual(
      jasmine.objectContaining({
        key: DOC_SYSCALLS_KEY,
        label: 'Syscalls (2)',
        level: 1,
        count: 6,
        expanded: true,
        posinset: 1,
        setsize: 4,
      }),
    );
    const proc = expanded.find((row) => row.key === 'taxonomy:proc');
    expect(proc).toEqual(
      jasmine.objectContaining({
        label: 'proc',
        level: 2,
        count: 3,
        expanded: true,
        posinset: 1,
        setsize: 4,
      }),
    );
    const process = expanded.find((row) => row.key === 'taxonomy:proc/process');
    expect(process).toEqual(
      jasmine.objectContaining({
        label: 'process',
        level: 3,
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
        level: 4,
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
    expect(filtered[0].label).toBe('Syscalls (2)');
    expect(filtered[0].kind).toBe('group');
    expect(countDocTreePages(filtered[0])).toBe(1);
    if (filtered[0].kind === 'group') {
      const proc = filtered[0].children[0];
      expect(proc.label).toBe('proc');
      expect(proc.kind).toBe('group');
      if (proc.kind === 'group') {
        const process = proc.children[0];
        expect(process.label).toBe('process');
        expect(process.kind).toBe('group');
        if (process.kind === 'group') {
          expect(process.children[0].label).toBe('beta(2)');
        }
      }
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

  it('exposes the full ancestor chain for deep links and filtering', () => {
    expect(docTreeAncestorKeys(PAGES[1])).toEqual([
      DOC_SYSCALLS_KEY,
      'taxonomy:proc',
      'taxonomy:proc/process',
    ]);
    expect(docTreeAncestorKeys(PAGES[3])).toEqual([
      DOC_SYSCALLS_KEY,
      'taxonomy:fs',
    ]);
    expect(docTreeAncestorKeys(PAGES[5])).toEqual([
      DOC_SYSCALLS_KEY,
      'taxonomy:dl',
    ]);
    expect(docTreeAncestorKeys(PAGES[6])).toEqual(['group:Guide']);
    expect(docTreeAncestorKeys(PAGES[7])).toEqual(['group:Manual page']);
    expect(docTreeAncestorKeys(PAGES[8])).toEqual(['group:Design note']);
  });
});
