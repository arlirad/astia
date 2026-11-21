export type DocGroup = 'Guide' | 'Manual page' | 'Design note' | 'Syscall (2)';

export interface DocPage {
  readonly slug: string;
  readonly title: string;
  readonly group: DocGroup;
  readonly summary: string;
  readonly source: string;
  readonly content: string;
  readonly category?: string;
  readonly syscallId?: number;
}
