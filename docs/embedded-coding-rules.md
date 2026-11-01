# Embedded coding rules

These rules apply to all code that runs on a microcontroller: `libs/core`,
`libs/lvgl`, `libs/interfaces`, `boards/`, `platforms/` (except
`platforms/pc`) and the application layer (`src/app`) of every test
application. They do **not** apply to `platforms/pc`, `libs/mocks`, tests and
host tools, which run on a desktop OS.

The goal is predictable behaviour on small targets: no hidden heap use, no
unbounded waits, no failure that only shows up after hours of uptime. Each
rule says why it exists; [libs/rules](../libs/rules/README.md) checks the
ones that can be detected mechanically (marked **[checked]**).

A rule can be broken where it is justified. Mark the line with
`// brick-rules: allow(<rule-id>) <reason>`; the reason is mandatory and is
reviewed like code.

## 1. Memory

**M1. No dynamic allocation after initialization. [checked: `new-delete`, `malloc`]**
Allocate everything a component needs in its constructor or `begin()` /
`initialize()`, or better, hold it as a member or a static buffer. Heap
fragmentation and out-of-memory errors appear late and at random; a device
that has started successfully must not be able to fail by running out of
memory.
Large or special memory (DMA-capable, PSRAM) is requested once through
`IMemoryProvider` during initialization and released only at shutdown.

**M2. No allocating standard containers. [checked: `std-container`, `std-string`, `std-function`]**
`std::vector`, `std::string`, `std::map`, `std::list`, `std::deque`,
`std::function`, `std::shared_ptr` and `std::make_*` allocate behind the
caller's back. Use `std::array`, fixed-capacity buffers owned by the caller,
pointer + size pairs (or a span type) and `const char*` for strings.

**M3. No large objects on the stack.**
Task stacks are a few kilobytes. Arrays bigger than about 256 bytes belong in
members or static storage, never in local variables. No recursion.

**M4. Constant data is `constexpr` / `const` at namespace scope**, so it is
placed in flash, not copied to RAM (on ESP8266 add `PROGMEM` for big tables
and read them with `pgm_read_*`/`memcpy_P`).

## 2. Language features

**L1. No lambdas. [checked: `lambda`]**
A lambda hides what it captures and for how long. Captured references dangle
when the lambda outlives the frame (typical with callbacks registered in a
driver or an RTOS task), a capturing lambda cannot be passed where the SDK
expects a C function pointer, and converting it to `std::function` allocates.
Use a named member function, a small named class implementing an interface,
or a C callback with a `void* context` that points to the owning object.

**L2. No exceptions and no RTTI. [checked: `exceptions`]**
Build with `-fno-exceptions -fno-rtti` where the SDK allows it. Report errors
through return values (`bool` or an error enum) and mark functions whose
result must be used with `[[nodiscard]]`. No `dynamic_cast`, no `typeid`.

**L3. No `double`; `float` only where an FPU exists and outside interrupts. [checked: `double`]**
ESP8266 and STM32F1 have no FPU, the ESP32 family has single precision only.
Prefer fixed-point or integer arithmetic (permille, Q16) in drivers and hot
paths.

**L4. No function-local statics with non-trivial construction. [checked: `static-local`]**
They add hidden thread-safe initialization guards and a construction order
nobody controls. Make the object a member of its owner. `static constexpr`
locals are fine.

**L5. No global objects with non-trivial constructors.**
Static initialization order across translation units is undefined and runs
before the SDK is ready. Create objects in `app_main()` / `setup()` /
`main()` (a `static` object inside the entry function is the accepted way to
give it static storage).

**L6. Templates for compile-time configuration only.**
`Features` structs, board templates and buffer sizes as template parameters
are fine. Avoid deep metaprogramming: every instantiation costs flash.

**L7. Fixed-width integers for anything that crosses an interface,** an
explicit `static_cast` for every narrowing conversion, and `-Wall -Wextra
-Werror`.

## 3. Hardware access and timing

**H1. Every wait has a timeout.** No `while (!ready) {}`. Waits return a
failure after their timeout, and loops that may run long feed the watchdog.

**H2. Every hardware operation reports failure** and the caller checks it.
`begin()`, transfers and mounts return `brick::Status` (section 6); errors
are logged once at the point where they are handled, not swallowed.

**H3. Interrupt handlers are minimal.** No logging, no allocation, no
blocking calls, no floating point. Copy the data, set a flag or notify a
task, return. Place handlers and the data they touch in IRAM where the
platform requires it.

**H4. Shared state between tasks or between a task and an ISR uses
`std::atomic` or an RTOS primitive. [checked: `volatile`]** `volatile` is for
memory-mapped registers only (a `volatile` pointer cast to a register
address); it makes no read-modify-write atomic.

**H5. Buffers handed to a driver stay valid until the driver says it is
done** (`wait_for_transfer_complete()`); the interfaces document this
contract and code must respect it.

**H6. No busy delays in the application layer. [checked: `app-delay`]** The
target loop decides how long to sleep; the application reports what it needs
(for example a frame period) instead of calling `delay_ms()` deep inside its
logic. `update()` never blocks.

**H7. Time is compared by difference, never with `<` or `>`. [checked: `time-compare`]**
A 32-bit millisecond counter wraps after about 49.7 days; `millis() <
deadline` then stays true for another 49 days and the device seems to hang.
Use `brick::core::time::Timer` or
`static_cast<std::int32_t>(now - deadline) < 0`.

## 4. Layering

**A1. The application layer includes only interfaces, core and LVGL. [checked: `layer-include`]**
No `brick/platform/*`, `brick/boards/*`, `esp_*`, `driver/*`, `freertos/*`,
`Arduino.h`, `stm32*`, HAL or OS headers in `src/app`. This is what makes a
PC variant of every application possible; see
[application-architecture.md](application-architecture.md).

**A2. Interfaces contain contracts only:** pure virtual functions,
plain-data types and trivial default implementations. No SDK types in
interface signatures.

**A3. One owner per resource.** The board owns peripherals, the application
owns its buffers. Non-owning access is a reference or a pointer, never a
copy of a handle.

## 5. Logging

**G1. Log through `ILogger` only,** with a constant tag and a constant
format string (never a format built at runtime or passed in from data).
No logging in ISRs or per-pixel / per-sample loops; log a summary instead.

## 6. Results and errors

**S1. An operation returns `brick::Status`** (`#include "brick/Status.h"`):
`Status::ok` or the reason it failed. A function that answers a question
(`is_pressed()`, `mounted()`, `valid()`) returns `bool`; a function that
cannot fail returns `void`. `Status` is `[[nodiscard]]` and has no conversion
to `bool`: compare with `Status::ok` or use `is_ok()`.

| Code | Meaning | Typical caller reaction |
|---|---|---|
| `ok` | done | continue |
| `error` | failed, nothing more specific (bus or SDK error, corrupt data) | log, give up or retry later |
| `invalid_argument` | the caller passed something unusable: a bug in the caller | fix the caller |
| `not_ready` | `begin()` / `mount()` missing or failed | start or mount first |
| `busy` | resource in use (pending transfer, every file object open) | try later |
| `timeout` | a wait ran out of time | retry |
| `no_data` | a polled input has nothing new; not an error | do nothing |
| `not_found` | file, asset or partition does not exist | fall back |
| `no_memory` | allocation failed or the caller's buffer is too small | bigger buffer / config |
| `not_supported` | device, mode or format lacks the operation | choose another path |

**S2. Return the most specific code, and pass a callee's status on
unchanged** (`if (const Status status = x.begin(); !is_ok(status)) return status;`).
Drivers translate SDK results with `to_status()` (`EspStatus.h`,
`HalStatus.h`) instead of flattening them to `error`.

**S3. Never drop a status.** Where it cannot be returned (an SDK or LVGL
callback), keep it for diagnostics, as `LvglDisplayAdapter::flush_status()`
does.

**S4. Log a failure once, where it is handled,** with
`to_string(status)` in the message.

## 7. Class design

**D1. Depend on interfaces.** A class receives its collaborators as
interface references in the constructor (in an application: a `Services`
struct). It never creates hardware itself; no singletons, no global
accessors.

**D2. Implementations are `final`.** Inherit only to implement an
interface, one level deep; no `protected` state.

**D3. One responsibility per class.** As a guide a file stays below about
200 lines and a function below about 40; split anything bigger.

**D4. Clear life cycle.** The constructor does not touch hardware;
`begin()` / `initialize()` returns `Status`; the object is usable only after
it succeeded; `update()` does not block (H6).

**D5. A buffer travels with its capacity:** a pointer with a size, or a
`std::array`. Overflow is reported (`Status::no_memory`), never silently
truncated.

**D6. No magic numbers.** Name constants (`constexpr`) and put the unit in
the name: `kTimeoutMs`, `kStripeBytes`.

**D7. State is an `enum class`,** not several `bool` flags that can reach a
combination nobody planned for.

## 8. Examples (test applications)

**E1. One example shows one feature** of the library.

**E2. Every example uses the portable layout:** `src/app` + `src/target` +
`pc/`, a `Services` struct, and a headless PC smoke test that runs in CI
([application-architecture.md](application-architecture.md)).

**E3. The README says what to expect** on the screen and in the log, so a
person can tell a pass from a failure on the hardware.

**E4. Examples use the public API only;** a workaround in an example is a
bug report against the library.

## Allowed outside the target code

`platforms/pc`, `libs/mocks`, `tests/` and `tools/` may use the standard
library freely (containers, `std::thread`, `std::filesystem`, lambdas),
because they run on a desktop OS. They still must not leak those types into
interfaces used by target code.
