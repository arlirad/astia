import { Routes, UrlMatcher } from '@angular/router';
import { DocumentationPage } from './documentation/documentation-page';
import { HomePage } from './home/home';

const documentationMatcher: UrlMatcher = (segments) => {
  if (segments.length === 0 || segments.length > 2 || segments[0].path !== 'documentation') {
    return null;
  }
  return {
    consumed: segments,
    posParams: segments.length === 2 ? { slug: segments[1] } : undefined,
  };
};

export const routes: Routes = [
  {
    path: '',
    component: HomePage,
    title: 'Astia: a freestanding x86_64 operating system',
  },
  {
    matcher: documentationMatcher,
    component: DocumentationPage,
    title: 'Documentation: Astia',
  },
  { path: '**', redirectTo: '' },
];
