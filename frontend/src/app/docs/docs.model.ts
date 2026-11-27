export type DocGroup = 'Guide' | 'Manual page' | 'Design note' | 'Syscall (2)';
export const DOC_GROUP_LABELS: Readonly<Record<DocGroup, string>> = {
  Guide: 'Guide',
  'Manual page': 'Design (7)',
  'Design note': 'Design note',
  'Syscall (2)': 'Syscall (2)',
};
export const docGroupLabel = (group: DocGroup): string => DOC_GROUP_LABELS[group];
export type DocContentKind = 'markdown' | 'source';
export type DocSynopsisKind = 'public' | 'raw';
export type DocsLoadStatus = 'idle' | 'loading' | 'ready' | 'not-found' | 'error';

export interface DocPage {
  readonly slug: string;
  readonly title: string;
  readonly group: DocGroup;
  readonly summary: string;
  readonly sourcePath: string;
  readonly contentKind: DocContentKind;
  readonly implementationPath?: string;
  readonly aliases?: readonly string[];
  readonly subsystem?: string;
  readonly category?: string;
  readonly syscallId?: number;
  readonly synopsisKind?: DocSynopsisKind;
  readonly synopsis?: string;
}
