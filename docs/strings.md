# Strings

`astia` has exactly two string types, both declared in
`kernel/include/astia/strings.hpp`. `string_view` sits directly in
`namespace astia`; the owning type sits in `namespace astia::strings`:

| Type | Ownership | Representation |
| --- | --- | --- |
| `astia::string_view` | non-owning | `{const char* data, usize size}` |
| `astia::strings::string` | owning, heap-backed | `char*` block, `size`, `capacity` |

The owning type sits in `namespace astia::strings`; `string_view` and the
C-string comparator stay directly in `namespace astia`. There is no
`astia::str` namespace and no other owning string type. The `string_`-prefixed
view helpers (`string_starts_with`,
`string_compare`, `string_before_nul`, and the `string_view` overload of
`string_equal`) are retired: that behaviour lives on `string_view`. The
C-string comparator `string_equal(const char*, const char*)` remains as the
shared NUL-terminated helper introduced by #345.

## `string_view`

`string_view` keeps its two-member layout and brace initialization, so
`{data, size}` remains valid. A null pointer is valid only for an empty range:
`{nullptr, 0}` is valid and `{nullptr, N}` with `N > 0` is invalid.

| Member | Semantics |
| --- | --- |
| `valid()` | `data != nullptr || size == 0` |
| `empty()` | `size == 0` |
| `equals(other)` | byte equality; invalid views never compare equal, not even to another invalid view |
| `starts_with(prefix)` | prefix test; invalid views never match |
| `compare(other)` | unsigned-byte lexicographic order, then length; two invalid views compare equal and a valid view orders above an invalid one |
| `find_nul(output)` | splits at the first NUL inside the range and writes the prefix to `output`; false for an invalid view or when the range holds no NUL |
| `from_c_str(input)` | explicit conversion from a NUL-terminated C string; `nullptr` yields the valid empty view `{nullptr, 0}` |

`string_view` is valid for embedded NULs and non-NUL-terminated ranges.
Conversions are explicit: there is no implicit `string_view` operator on
`string`, and C strings convert through `from_c_str`.

## `astia::strings::string`

`string` owns a NUL-terminated heap block and never throws: allocation failure
is reported through `optional`/`bool`.

| Member | Contract |
| --- | --- |
| `string()` | empty, no allocation; `c_str()` returns a static `""` |
| `~string()` | frees the block |
| `string(string&&)` / `operator=(string&&)` | transfers ownership; the source becomes empty; self-move is safe |
| `try_from(string_view)` | copies into a new string; `error_t::einval` for an invalid view, `error_t::enomem` on allocation failure |
| `try_from(const char*)` | copies a NUL-terminated string; `nullptr` is `error_t::einval` |
| `clone()` | explicit deep copy; same failure values |
| `assign(view)` | replaces the contents; false for an invalid view or OOM, previous contents survive |
| `append(view)` | appends a suffix; false for an invalid view or OOM, previous contents survive; the suffix may be a view of the receiver |
| `clear()` | drops the contents and keeps the allocation |
| `view()` | non-owning view; an empty string yields `{nullptr, 0}` |
| `c_str()` | always non-null; `""` when empty |
| `size()` | bytes, excluding the terminating NUL |
| `empty()` | `size() == 0` |

Invariants:

- `size()` excludes the terminating NUL; the allocation always has room for it
  and a non-empty block is always NUL-terminated.
- An empty string owns no block (`data == nullptr`, `size == 0`,
  `capacity == 0`).
- `view()` of an empty string is `{nullptr, 0}`.
- Allocation goes through `astia::mm::heap::alloc`/`free`. Before
  `mm::heap::init()` every allocating operation fails with `error_t::enomem`,
  matching the heap contract. Nothing throws, panics, or returns a partially
  initialized string.
- No SSO, COW, locale, Unicode, formatting, or growth-factor tuning.

Copy construction is deleted; copying is spelled `clone()`.

## Where the types may appear

`string` and `string_view` are C++-only kernel types:

- No syscall, C header, module entrypoint, vtable, or struct-layout change
  depends on them.
- `astia::strings::string` must not cross the C ABI, appear in
  `kernel/include/astia/api/`, `libc/include/`, or
  `kernel/include/astia/module.hpp`.
- A module can be unloaded while an `astia::strings::string` allocation survives, so the
  owning type never crosses the module boundary. Modules keep C strings and
  `string_view`.
- `libc/include/string.h` and `libc/src/string.c` are the C `str*`/`mem*`
  surface and do not include `astia/strings.hpp`. The dependency direction is
  C++ -> C.

## Forbidden

In `kernel/`, `modules/`, and `libc/`:

- `using string_ = ...`, `using astia_string = ...`, `using String = ...`,
  `using str = ...`
- `using std::string;` in a header or kernel/module source
- `#define string ...`, `#define string_ ...`, or any macro aliasing the type
- a second owning string type (`basic_string`, `small_string`, `text`,
  `string_buffer` as an `astia` type)
- `<string>` or `<string_view>` in kernel/module production translation units
- free functions named `string_*` other than the documented C-string
  `string_equal(const char*, const char*)`
- introducing a `using namespace astia;` next to `using namespace std;`

## Guard

`tools/check_string_naming.sh` enforces the rules above and runs in
`.github/workflows/kernel-ci.yml`. It fails on the retired `string_*` view
helpers, on `<string>`/`std::string` in kernel and module production sources,
and on any `astia::string` qualifier, so the owning type can only be named
through `astia::strings`.

## Example

```cpp
astia::string_view path = astia::string_view::from_c_str("/sbin/init");
astia::string_view name;

if (path.find_nul(name)) {
    // name is the range before the first NUL
}

auto owned = astia::strings::string::try_from(path);

if (!owned) {
    // owned.error is error_t::enomem or error_t::einval
}

if (owned && owned.value.append({"d", 1})) {
    // owned.value.c_str() is NUL-terminated at owned.value.size()
}
```
