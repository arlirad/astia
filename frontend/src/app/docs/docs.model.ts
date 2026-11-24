export type DocGroup = 'Guide' | 'Manual page' | 'Design note' | 'Syscall (2)';
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
  readonly aliases?: readonly string[];
  readonly category?: string;
  readonly syscallId?: number;
  readonly synopsisKind?: DocSynopsisKind;
  readonly synopsis?: string;
}
