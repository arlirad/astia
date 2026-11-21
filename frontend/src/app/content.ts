import type { DocPage } from './docs/docs.model';
import { GENERATED_DOCS } from './docs/docs.generated';

export interface NavLink {
  readonly label: string;
  readonly href: string;
}

export type CapabilityTone = 'tag-core' | 'tag-bounded' | 'tag-experimental';

export interface Capability {
  readonly title: string;
  readonly tag: string;
  readonly tone: CapabilityTone;
  readonly description: string;
}

export interface Snapshot {
  readonly value: string;
  readonly label: string;
}

export interface RoadmapItem {
  readonly title: string;
  readonly description: string;
  readonly area: string;
}

export interface CommandStep {
  readonly label: string;
  readonly code: string;
  readonly note: string;
}

export interface ResourceLink {
  readonly title: string;
  readonly description: string;
  readonly href: string;
}

export const GITHUB_URL = 'https://github.com/arlirad/astia';
export const README_URL = `${GITHUB_URL}/blob/main/README.md`;
export const ISSUES_URL = `${GITHUB_URL}/issues`;
export const LICENSE_URL = `${GITHUB_URL}/blob/main/LICENSE`;

const repoBlob = (path: string): string => `${GITHUB_URL}/blob/main/${path}`;

export const DOC_PAGES: readonly DocPage[] = GENERATED_DOCS;

export const NAV_LINKS: readonly NavLink[] = [
  { label: 'Overview', href: '#overview' },
  { label: 'Capabilities', href: '#capabilities' },
  { label: 'Roadmap', href: '#roadmap' },
  { label: 'Documentation', href: '#documentation' },
  { label: 'Get started', href: '#get-started' },
];

export const SNAPSHOT: readonly Snapshot[] = [
  { value: 'C++20', label: 'Freestanding kernel' },
  { value: 'x86_64 virt', label: 'Only supported target' },
  { value: 'QEMU', label: 'PVH direct boot' },
  { value: 'MIT', label: 'Open source license' },
];

export const CAPABILITIES: readonly Capability[] = [
  {
    title: 'Typed kernel core',
    tag: 'Kernel',
    tone: 'tag-core',
    description:
      'A freestanding C++20 kernel with memory management, processes, signals, TTYs, UNIX sockets, and asynchronous I/O.',
  },
  {
    title: 'VFS and filesystems',
    tag: 'Storage',
    tone: 'tag-bounded',
    description:
      'initrd, devfs, tmpfs, and bounded read-only ext4 support, with regular-file and directory reads.',
  },
  {
    title: 'Native userspace',
    tag: 'Userspace',
    tone: 'tag-core',
    description:
      'Astia libc, init, daemond, and a growing set of coreutils, cross-built without the host C library.',
  },
  {
    title: 'Typed loadable modules',
    tag: 'Modules',
    tone: 'tag-bounded',
    description:
      'ET_REL modules use the public C++ registration contracts and generic device adapters.',
  },
  {
    title: 'Device foundations',
    tag: 'Drivers',
    tone: 'tag-experimental',
    description:
      'NVMe and VirtIO block modules, plus bounded RTL8139 and experimental xHCI/HID work for QEMU fixtures.',
  },
  {
    title: 'QEMU-first workflows',
    tag: 'Toolchain',
    tone: 'tag-core',
    description:
      'An interactive native zsh, optional GNU nano, boot tests, and hardware-driver fixtures.',
  },
];

export const SUPPORTED: readonly string[] = [
  'x86_64 with the QEMU virt machine and PVH direct boot',
  'Raw cpio initrds; gzip and zstd signatures are diagnosed but not opened',
  'Read-only ext4 regular-file and directory access',
  'Native userspace built against the Astia libc and ABI',
];

export const NOT_GOALS: readonly string[] = [
  'A production-ready, general-purpose operating system',
  'A complete POSIX implementation',
  'A full TCP/IP stack or end-to-end network socket API',
  'Portable hardware support or a general-purpose USB stack',
];

export const ROADMAP: readonly RoadmapItem[] = [
  {
    title: 'Compressed initrd decoding',
    area: 'Filesystems',
    description:
      'Move beyond raw cpio archives and open gzip- or zstd-compressed initrds.',
  },
  {
    title: 'Safe module-facing filesystem ownership',
    area: 'Kernel',
    description: 'Define safe ownership and mount APIs for loadable filesystem modules.',
  },
  {
    title: 'Broader architecture and platform policy',
    area: 'Platform',
    description: 'Extend policy providers beyond the single supported x86_64 virt target.',
  },
  {
    title: 'Tighter process, AIO, and mapping ownership',
    area: 'Runtime',
    description:
      'Consolidate process, asynchronous I/O, and memory-mapping ownership boundaries.',
  },
];

export const COMMANDS: readonly CommandStep[] = [
  {
    label: 'Configure and build the kernel',
    code: `cmake -S kernel -B kernel/build \\
  -DARCH=x86_64 \\
  -DPLATFORM=virt \\
  -DBOOT=virt
cmake --build kernel/build`,
    note: 'Requires CMake 3.20+ and a C/C++ toolchain.',
  },
  {
    label: 'Boot the kernel in QEMU',
    code: './kernel/scripts/run-qemu.sh',
    note: 'Uses -kernel and a serial console. Stop the idle kernel with Ctrl-C.',
  },
  {
    label: 'Run the interactive userspace',
    code: `toolchain/init-userspace.sh
./toolchain/run-zsh-qemu.sh`,
    note: 'Initializes pinned userspace submodules first; the launcher is interactive.',
  },
];

export const RESOURCES: readonly ResourceLink[] = [
  {
    title: 'Project README',
    description: 'Build, boot, userspace, and testing workflows.',
    href: README_URL,
  },
  {
    title: 'Kernel guide',
    description: 'Kernel layout, APIs, and test conventions.',
    href: repoBlob('kernel/README.md'),
  },
  {
    title: 'Module ABI',
    description: 'Typed module contracts and loader details.',
    href: repoBlob('docs/module-abi.md'),
  },
  {
    title: 'CTest suites',
    description: 'Deterministic test suites and external opt-ins.',
    href: repoBlob('docs/ctest.md'),
  },
  {
    title: 'Toolchain and userspace',
    description: 'Sysroot, zsh/nano builds, and POSIX harnesses.',
    href: repoBlob('toolchain/README.md'),
  },
  {
    title: 'Issues and contributions',
    description: 'Report a problem or follow active work.',
    href: ISSUES_URL,
  },
];

export const PREREQUISITES: readonly string[] = [
  'Git',
  'CMake 3.20+',
  'Clang and a C/C++ toolchain',
  'QEMU',
  'Python 3',
  'cpio, xxd, gzip, zstd',
];
