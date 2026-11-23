export const escapeHtml = (value: string): string =>
  value.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');

export const escapeAttribute = (value: string): string =>
  escapeHtml(value).replace(/"/g, '&quot;');

export const slugify = (value: string): string =>
  value
    .replace(/<[^>]+>/g, '')
    .replace(/&[^;]+;/g, '')
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-+|-+$/g, '') || 'section';
