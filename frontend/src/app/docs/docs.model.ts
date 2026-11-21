export type DocGroup = 'Guide' | 'Manual page' | 'Design note';

export interface DocPage {
  readonly slug: string;
  readonly title: string;
  readonly group: DocGroup;
  readonly summary: string;
  readonly source: string;
  readonly content: string;
}
