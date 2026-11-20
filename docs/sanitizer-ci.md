# Host sanitizer coverage

The sanitizer workflow runs the `sanitizer-host` executable tests, the
`sanitizer-host-partial` executable tests whose support objects intentionally
remain unsanitized, and the `sanitizer-host-support` static/fixture checks.
The workflow prints each category separately so partial and support checks are
not counted as fully instrumented executables.

The libc host tests carry the `sanitizer-unsupported-libc` label and remain in
the normal CTest suite only. They deliberately define libc ABI symbols such as
`malloc`, `memcpy`, `sigaltstack`, and `getrlimit` in the executable. ASan and
TSan runtime startup/interceptors bind those symbols before the test begins,
causing bootstrap failures or changing the libc behavior under test. Applying
sanitizer flags to those targets therefore produces misleading coverage rather
than a valid sanitizer run. A separate libc sanitizer-porting change is
required before moving that label into the sanitizer workflow.
