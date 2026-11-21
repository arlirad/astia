import { Routes } from '@angular/router';
import { DocumentationPage } from './documentation/documentation-page';
import { HomePage } from './home/home';

export const routes: Routes = [
  {
    path: '',
    component: HomePage,
    title: 'Astia: a freestanding x86_64 operating system',
  },
  {
    path: 'documentation',
    component: DocumentationPage,
    title: 'Documentation: Astia',
  },
  {
    path: 'documentation/:slug',
    component: DocumentationPage,
    title: 'Documentation: Astia',
  },
  { path: '**', redirectTo: '' },
];
