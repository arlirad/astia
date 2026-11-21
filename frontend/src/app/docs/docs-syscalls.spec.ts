import { TestBed } from '@angular/core/testing';
import { DOC_PAGES } from '../content';
import { DocsBrowser } from './docs-browser';

const SYSCALL_PAGES = DOC_PAGES.filter((page) => page.group === 'Syscall (2)');

describe('Generated section-2 syscall docs', () => {
  beforeEach(async () => {
    await TestBed.configureTestingModule({
      imports: [DocsBrowser],
    }).compileComponents();
  });

  it('contains all 70 canonical syscall pages with stable IDs and slugs', () => {
    expect(SYSCALL_PAGES.length).toBe(70);
    expect(SYSCALL_PAGES.map((page) => page.syscallId).sort((a, b) => (a ?? 0) - (b ?? 0))).toEqual(
      Array.from({ length: 70 }, (_, index) => index),
    );

    const slugs = new Set(SYSCALL_PAGES.map((page) => page.slug));
    expect(slugs.size).toBe(70);
    for (const page of SYSCALL_PAGES) {
      expect(page.slug).toBe(
        `syscall-${page.syscallId}-${page.title.replace(/\(2\)$/, '').replace(/_/g, '-')}`,
      );
    }
  });

  it('carries real Markdown bodies and authoritative source links', () => {
    const ioSetup = SYSCALL_PAGES.find((page) => page.syscallId === 0);
    const alarm = SYSCALL_PAGES.find((page) => page.syscallId === 55);
    const fchdir = SYSCALL_PAGES.find((page) => page.syscallId === 69);

    expect(ioSetup?.title).toBe('io_setup(2)');
    expect(ioSetup?.content).toContain('## SYNOPSIS');
    expect(ioSetup?.content).toContain('Allocates a ring');
    expect(ioSetup?.category).toBe('fs');
    expect(ioSetup?.source).toBe(
      'https://github.com/arlirad/astia/blob/main/kernel/src/fs/syscalls/io_setup.hpp',
    );

    expect(alarm?.title).toBe('alarm(2)');
    expect(alarm?.content).toContain('System call handler for `alarm`');
    expect(alarm?.source).toContain('kernel/src/proc/syscalls/alarm.cpp');

    expect(fchdir?.title).toBe('fchdir(2)');
    expect(fchdir?.source).toContain('kernel/src/proc/syscalls/process/fchdir.cpp');

    for (const page of SYSCALL_PAGES) {
      expect(page.category).toBeTruthy();
      expect(page.source).toMatch(
        /^https:\/\/github\.com\/arlirad\/astia\/blob\/main\/(kernel\/src|kernel\/include)\//,
      );
      expect(page.content).toContain('## DESCRIPTION');
    }
  });

  it('renders a syscall page through the existing Markdown parser', async () => {
    const fixture = TestBed.createComponent(DocsBrowser);
    fixture.componentRef.setInput('pages', DOC_PAGES);
    fixture.componentRef.setInput('selectedSlug', 'syscall-0-io-setup');
    fixture.detectChanges();
    await fixture.whenStable();

    const element = fixture.nativeElement as HTMLElement;
    const headings = [...element.querySelectorAll('.doc-markdown h2')].map((node) =>
      node.textContent?.trim(),
    );

    expect(element.querySelector('.docs-page-title')?.textContent).toContain('io_setup(2)');
    expect(headings).toContain('SYNOPSIS');
    expect(element.querySelector('.doc-markdown pre code')?.textContent).toContain(
      'long io_setup',
    );
    expect(element.querySelector('.docs-source')?.getAttribute('href')).toContain(
      'kernel/src/fs/syscalls/io_setup.hpp',
    );
    expect(element.querySelectorAll('.docs-page-top .tag').length).toBeGreaterThanOrEqual(3);
  });

  it('filters syscall pages by title', () => {
    const fixture = TestBed.createComponent(DocsBrowser);
    fixture.componentRef.setInput('pages', DOC_PAGES);
    fixture.detectChanges();

    const input = fixture.nativeElement.querySelector(
      '.docs-filter input',
    ) as HTMLInputElement;
    input.value = 'sigwait';
    input.dispatchEvent(new Event('input', { bubbles: true }));
    fixture.detectChanges();

    expect(fixture.nativeElement.querySelectorAll('.docs-index-link').length).toBe(1);
    expect(
      fixture.nativeElement.querySelector('.docs-index-link')?.textContent,
    ).toContain('sigwait(2)');
  });
});
