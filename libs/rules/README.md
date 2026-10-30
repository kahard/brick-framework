# BRICK rules

`brick_rules.py` checks the mechanically detectable part of the
[embedded coding rules](../../docs/embedded-coding-rules.md): lambdas, heap
allocation, allocating standard types, exceptions/RTTI, `double`,
function-local statics and, in the application layer, includes of platform,
board, SDK or OS headers.

```text
python libs/rules/brick_rules.py libs boards platforms          # framework report
python libs/rules/brick_rules.py --strict boards platforms      # framework CI gate
python libs/brick/libs/rules/brick_rules.py --strict apps       # test apps CI gate
```

Without `--strict` it only reports, so it can run over code that is still
being migrated (`libs/core`, see below). Code for the desktop (`platforms/pc`,
`libs/mocks`, `tests`, `tools`, an application's `pc/` directory) is not
checked, nor are third-party ESP-IDF components kept as published in an
application's `components/` or `managed_components/`.

A justified exception is marked in the source:

```cpp
// brick-rules: allow(malloc) frame buffers are allocated once in begin()
buffer_ = static_cast<std::uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM));
```

`libs/core` is still checked in report mode only: `AudioBuffer` and
`WavDecoder` use `std::vector`, `BmpDecoder` allocates a row buffer and
`Screen::Canvas` uses `new`.

The checker is regex based and deliberately simple; it ignores comments and
string literals but does not parse C++. Report false positives by improving
the pattern rather than adding suppressions.
