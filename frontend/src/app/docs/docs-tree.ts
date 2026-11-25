import type { DocPage } from './docs.model';

export const DOC_TREE_ALL = 'all';
export const DOC_SUBSYSTEM_ORDER = ['proc', 'fs', 'abi', 'dl'] as const;
export const DOC_CATEGORY_ORDER = [
  'core',
  'ipc',
  'memory',
  'process',
  'signal',
  'thread',
  'fs',
  'abi',
  'dl',
] as const;
const DOC_GROUP_ORDER = ['Guide', 'Manual page', 'Design note'] as const;

export interface DocTreePageNode {
  readonly kind: 'page';
  readonly key: string;
  readonly label: string;
  readonly page: DocPage;
  readonly children: readonly [];
}

export interface DocTreeGroupNode {
  readonly kind: 'group';
  readonly key: string;
  readonly label: string;
  readonly children: readonly DocTreeNode[];
}

export type DocTreeNode = DocTreePageNode | DocTreeGroupNode;

export interface DocTreeRow {
  readonly key: string;
  readonly kind: DocTreeNode['kind'];
  readonly label: string;
  readonly page?: DocPage;
  readonly level: number;
  readonly count: number;
  readonly expanded: boolean;
  readonly posinset: number;
  readonly setsize: number;
}

export const pageMatches = (page: DocPage, query: string): boolean => {
  if (!query) {
    return true;
  }
  const searchable =
    `${page.title} ${page.group} ${page.subsystem ?? ''} ${page.category ?? ''} ${page.syscallId ?? ''} ` +
    `${page.synopsisKind ?? ''} ${page.summary}`;
  return searchable.toLowerCase().includes(query);
};

export const taxonomyMatches = (
  page: DocPage,
  subsystem: string,
  category: string,
): boolean => {
  if (subsystem !== DOC_TREE_ALL && page.subsystem !== subsystem) {
    return false;
  }
  return category === DOC_TREE_ALL || page.category === category;
};

const rank = (name: string, order: readonly string[]): number => {
  const index = order.indexOf(name);
  return index < 0 ? order.length : index;
};

const compareNamed = (left: string, right: string, order: readonly string[]): number =>
  rank(left, order) - rank(right, order) || left.localeCompare(right);

const comparePages = (left: DocPage, right: DocPage): number =>
  (left.syscallId ?? Number.MAX_SAFE_INTEGER) - (right.syscallId ?? Number.MAX_SAFE_INTEGER)
  || left.title.localeCompare(right.title);

const pageNode = (page: DocPage): DocTreePageNode => ({
  kind: 'page',
  key: `page:${page.slug}`,
  label: page.title,
  page,
  children: [],
});

const groupNode = (
  key: string,
  label: string,
  children: readonly DocTreeNode[],
): DocTreeGroupNode => ({ kind: 'group', key, label, children });

const taxonomyKey = (subsystem: string, category?: string): string =>
  category ? `taxonomy:${subsystem}/${category}` : `taxonomy:${subsystem}`;

const isTaxonomyPage = (page: DocPage): boolean =>
  page.group === 'Syscall (2)' && Boolean(page.subsystem);

const buildTaxonomyNodes = (pages: readonly DocPage[]): readonly DocTreeGroupNode[] => {
  const bySubsystem = new Map<string, DocPage[]>();
  for (const page of pages) {
    if (!page.subsystem) {
      continue;
    }
    const entries = bySubsystem.get(page.subsystem) ?? [];
    entries.push(page);
    bySubsystem.set(page.subsystem, entries);
  }

  const subsystems = [...bySubsystem.keys()].sort((left, right) =>
    compareNamed(left, right, DOC_SUBSYSTEM_ORDER),
  );

  return subsystems.map((subsystem) => {
    const subsystemPages = [...(bySubsystem.get(subsystem) ?? [])].sort(comparePages);
    if (subsystem !== 'proc') {
      return groupNode(
        taxonomyKey(subsystem),
        subsystem,
        subsystemPages.map(pageNode),
      );
    }

    const byCategory = new Map<string, DocPage[]>();
    for (const page of subsystemPages) {
      const category = page.category ?? subsystem;
      const entries = byCategory.get(category) ?? [];
      entries.push(page);
      byCategory.set(category, entries);
    }
    const categories = [...byCategory.keys()].sort((left, right) =>
      compareNamed(left, right, DOC_CATEGORY_ORDER),
    );
    return groupNode(
      taxonomyKey(subsystem),
      subsystem,
      categories.map((category) =>
        groupNode(
          taxonomyKey(subsystem, category),
          category,
          (byCategory.get(category) ?? []).map(pageNode),
        ),
      ),
    );
  });
};

const buildGroupNodes = (pages: readonly DocPage[]): readonly DocTreeGroupNode[] => {
  const byGroup = new Map<string, DocPage[]>();
  for (const page of pages) {
    const entries = byGroup.get(page.group) ?? [];
    entries.push(page);
    byGroup.set(page.group, entries);
  }
  return [...byGroup.keys()]
    .sort((left, right) => compareNamed(left, right, DOC_GROUP_ORDER))
    .map((group) =>
      groupNode(
        `group:${group}`,
        group,
        (byGroup.get(group) ?? []).map(pageNode),
      ),
    );
};

export const buildDocTree = (pages: readonly DocPage[]): readonly DocTreeNode[] => {
  const taxonomyPages = pages.filter(isTaxonomyPage);
  const groupPages = pages.filter((page) => !isTaxonomyPage(page));
  return [...buildTaxonomyNodes(taxonomyPages), ...buildGroupNodes(groupPages)];
};

export const filterDocTree = (
  nodes: readonly DocTreeNode[],
  matches: (page: DocPage) => boolean,
): readonly DocTreeNode[] => {
  const filtered: DocTreeNode[] = [];
  for (const node of nodes) {
    if (node.kind === 'page') {
      if (matches(node.page)) {
        filtered.push(node);
      }
      continue;
    }
    const children = filterDocTree(node.children, matches);
    if (children.length > 0) {
      filtered.push(groupNode(node.key, node.label, children));
    }
  }
  return filtered;
};

export const countDocTreePages = (node: DocTreeNode): number =>
  node.kind === 'page'
    ? 1
    : node.children.reduce((count, child) => count + countDocTreePages(child), 0);

export const flattenDocTree = (
  nodes: readonly DocTreeNode[],
  expandedKeys: ReadonlySet<string>,
): readonly DocTreeRow[] => {
  const rows: DocTreeRow[] = [];
  const walk = (
    siblings: readonly DocTreeNode[],
    level: number,
  ): void => {
    siblings.forEach((node, index) => {
      const expanded = node.kind === 'group' && expandedKeys.has(node.key);
      rows.push({
        key: node.key,
        kind: node.kind,
        label: node.label,
        page: node.kind === 'page' ? node.page : undefined,
        level,
        count: countDocTreePages(node),
        expanded,
        posinset: index + 1,
        setsize: siblings.length,
      });
      if (node.kind === 'group' && expanded) {
        walk(node.children, level + 1);
      }
    });
  };
  walk(nodes, 1);
  return rows;
};

export const docTreeAncestorKeys = (page: DocPage): readonly string[] => {
  if (isTaxonomyPage(page) && page.subsystem) {
    const keys = [taxonomyKey(page.subsystem)];
    if (page.subsystem === 'proc' && page.category) {
      keys.push(taxonomyKey(page.subsystem, page.category));
    }
    return keys;
  }
  return [`group:${page.group}`];
};

export const docTreeGroupKeys = (nodes: readonly DocTreeNode[]): readonly string[] => {
  const keys: string[] = [];
  const walk = (siblings: readonly DocTreeNode[]): void => {
    for (const node of siblings) {
      if (node.kind === 'group') {
        keys.push(node.key);
        walk(node.children);
      }
    }
  };
  walk(nodes);
  return keys;
};
