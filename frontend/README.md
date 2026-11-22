# Astia frontend

Static project portal for [Astia](https://github.com/arlirad/astia), built with Angular 20 standalone components and SCSS.

The portal presents the project overview, capabilities, near-term roadmap, build instructions, and documentation links. It is a static site: there is no backend or project API.

## Development

```sh
npm install
npm start
```

The dev server defaults to `http://localhost:4200`.

## Checks

```sh
npm run build
npm run test:ci
```

`npm run build` uses the production configuration. `npm run test:ci` runs the Karma/Jasmine suite in headless Chrome.

## Documentation content

The in-app docs browser reads bundled Markdown from the repository `docs/` directory and the project README files. Regenerate the bundle with:

```sh
npm run docs:generate
```

The same generator runs automatically before `npm run build` and before the test runners. It also invokes `tools/man/generate.py` with `--export-section2`, which writes the section-2 Markdown pages and manifest for all 71 canonical syscall IDs into the ignored `frontend/.cache` directory. Repository-relative Markdown links are rewritten to GitHub blob or tree URLs at generation time, while external URLs, anchors, and mailto links are preserved. Edit the source Markdown or syscall documentation in the repository, then rebuild.

Generated `man7` and section-2 export trees are published transactionally. Each output directory is staged as a private sibling directory, fully written and fsynced, then activated with one Linux `renameat2` exchange. A first generation uses `renameat2` with no-replace semantics. The two trees are separate transactions rather than one invocation-wide atomic unit; the frontend reads the section-2 export only after the generator exits successfully. This requires Linux and a filesystem that supports `renameat2` directory exchange for sibling directories, such as ext4, XFS, Btrfs, or tmpfs.

A final active-tree snapshot is checked immediately before activation. Snapshots include regular-file content hashes, ctime, ownership, mode, and extended attributes; any entry added or changed after staging aborts before the directory exchange, leaving the old manifest, pages, and new unrelated entry in the active tree. Hash open/read failures and entries that change type during hashing or copying fail closed without blocking or activating. A remaining OS-level race between the last snapshot and `renameat2` cannot be removed without a cooperative lock; if it occurs, the retired tree is retained as a `.astia-retired-<id>` sibling rather than deleted, so unexpected files are preserved for recovery. Cooperating generators should still run one at a time. Staging directories are created mode `0700`, opened and pinned immediately, and marked owned only after the opened FD identity matches the directory entry. Cleanup uses an atomic quarantine rename plus pinned-FD removal; uncertain identity preserves the path and reports it. Diagnostics use lexical absolute paths and never resolve symlinks. Staging collisions are never removed; if allocation fails after a collision, the pre-existing paths are reported and left untouched.

Regular files are copied with owner, group, mode, ACLs, and extended attributes preserved. Recreated directories, FIFOs, and the staging root preserve owner, group, mode, and extended attributes, including POSIX ACLs. Transactions fail closed if metadata cannot be read or applied. Destination-inherited ACLs and xattrs are cleared before source metadata is applied. Metadata copying depends on `/proc/self/fd`; if procfs is unavailable, the transaction fails closed before activation. Symlink metadata is not cloned. The retired tree is deleted only when its post-exchange snapshot still matches the pre-staging snapshot, ignoring only the root ctime change caused by the rename itself. Post-exchange verification, retired-name allocation, rename, or cleanup failures after activation emit a RuntimeWarning containing the actual retained path (retired or existing staging) and the original cause or causes, falling back to stderr when warnings are configured as errors; activation remains successful.

## Assets

Brand assets are copied from the repository `img/` directory into `public/img/`. The banner is used in the overview; the 64px logo is used for the header and favicon.
