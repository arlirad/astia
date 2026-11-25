# Pull request

<!--
Keep this concise. Use N/A only with a reason. Do not include secrets,
credentials, private keys, private hostnames, or unredacted configuration.
-->

## Summary

<!-- What changed and why. -->

## Scope

- In scope:
- Not in scope:
- Subsystems and public surfaces:

## Owner / reviewer

`@handle` or `unassigned`

<!-- Opening this PR does not assign anyone. -->

## Current base and head

| Ref | Branch | Commit |
| --- | --- | --- |
| Base | `main` | `<full-sha>` |
| Head | `<branch>` | `<full-sha>` |

- [ ] Base and head were rechecked immediately before merge. If the base
  moved, all checks were rerun on the final head.

## Tests and evidence

Exact commands run:

```sh
# Paste the commands used, for example:
# ./kernel/scripts/test.sh kernel/build-tests
# ctest --test-dir build-tests --output-on-failure
```

| Check | Result | Evidence |
| --- | --- | --- |
| `build-and-test` | | |
| `asan-ubsan` | | |
| `tsan` | | |
| `clang-tidy` | | |
| `clang-format-pr` | | |

- [ ] Every skipped suite or check is named with a reason.
- [ ] QEMU, hardware, or manual coverage is stated or marked N/A.

## Residual and limitation links

Closes #N
Refs #N
Follow-ups #N

<!-- Link residual risks instead of leaving them implicit. -->

## ABI, security, and compatibility

<!-- Required. Write "None known" with a short reason when there is no impact. -->

- ABI:
- Security:
- Compatibility and versioning:

- [ ] `docs/module-abi.md` was updated if the module contract changed, and
  ET_REL modules were rebuilt.
- [ ] Version derivation follows `docs/versioning.md`; no hard-coded version
  was added.

## Deployment, configuration, and privacy

- [ ] No secrets, tokens, credentials, private keys, private hostnames, or
  unredacted configuration appear in the diff, logs, screenshots, or artifacts.
- [ ] New configuration defaults are safe, and deployment or configuration
  changes are documented or N/A.
- [ ] Test and deployment data are synthetic or redacted.

## Rollback

<!-- Required for boot, ABI, module, deployment, or on-disk format changes. -->

- Revert plan:
- Data or configuration rollback:

## Merge gate

- [ ] All five checks above are green on the exact head SHA.
- [ ] If the merge queue is used, the `merge_group` run is green.
- [ ] Review approval covers the evidence, not only a green CI result.
- [ ] Base and head still match the table above.

## Reviewer notes

<!-- Risks, follow-ups, or approval conditions. -->
