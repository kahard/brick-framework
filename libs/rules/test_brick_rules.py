"""Self-test for brick_rules.py: python libs/rules/test_brick_rules.py"""

import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import brick_rules  # noqa: E402

CASES = {
    "lambda": [
        ("auto f = [this]() { run(); };", True),
        ("call([&](int x) { return x; });", True),
        ("std::sort(a, b, [](int l, int r) -> bool { return l < r; });", True),
        ("values[index] = 1;", False),
        ("int table[4] = {1, 2, 3, 4};", False),
        ("attribute [[nodiscard]] bool f();", False),
    ],
    "new-delete": [("auto* p = new Widget();", True), ("delete p;", True), ("bool renew = true;", False)],
    "malloc": [
        ("p = malloc(4);", True),
        ("p = heap_caps_malloc(4, MALLOC_CAP_DMA);", True),
        ("p = heap_caps_aligned_alloc(64, 4, MALLOC_CAP_SPIRAM);", True),
        ("freeze();", False),
    ],
    "std-container": [("std::vector<int> v;", True), ("std::array<int, 4> a;", False)],
    "double": [("double x = 1.0;", True), ("float x = 1.0f;", False)],
    "volatile": [
        ("    volatile bool ready = false;", True),
        ("    auto* reg = reinterpret_cast<volatile const std::uint32_t*>(base + 0x440U);", False),
        ("    volatile std::uint32_t* const port = registers;", False),
    ],
    "time-compare": [
        ("    if (time_.millis() < next_ms_)", True),
        ("    if (deadline >= clock.micros())", True),
        ("    if (static_cast<std::int32_t>(time_.millis() - next_ms_) < 0)", False),
        ("    const auto elapsed = static_cast<std::uint32_t>(time_.millis()) - start;", False),
        ("    log(\"%u\", millis());", False),
    ],
    "static-local": [
        ("    static Widget widget;", True),
        ("    static constexpr int kSize = 4;", False),
        ("static Widget widget;", False),
        ("    static bool parse(const char* text);", False),
        ("    static std::uint8_t buffer[256];", True),
    ],
}
CLASS_MEMBERS = """class Decoder
{
private:
    static std::uint16_t read_u16(const std::uint8_t* data);
    static int instances;
};
"""


def findings_for(line: str) -> set[str]:
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "sample.cpp"
        path.write_text("void f()\n{\n" + line + "\n}\n", encoding="utf-8")
        return {rule_id for _, rule_id, _ in brick_rules.check_file(path)[1]}


def main() -> int:
    failures = 0
    for rule_id, cases in CASES.items():
        for line, expected in cases:
            found = rule_id in findings_for(line)
            if found != expected:
                failures += 1
                print(f"FAIL {rule_id}: expected {'a finding' if expected else 'no finding'} for: {line}")
    suppressed = findings_for("    p = malloc(4); // brick-rules: allow(malloc) allocated once at start-up")
    if "malloc" in suppressed:
        failures += 1
        print("FAIL suppression with a reason was ignored")
    unjustified = findings_for("    p = malloc(4); // brick-rules: allow(malloc)")
    if "malloc" not in unjustified:
        failures += 1
        print("FAIL suppression without a reason was accepted")
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "Decoder.h"
        path.write_text(CLASS_MEMBERS, encoding="utf-8")
        if brick_rules.check_file(path)[1]:
            failures += 1
            print("FAIL static class members were reported as function-local statics")
    with tempfile.TemporaryDirectory() as directory:
        app = Path(directory)
        for relative in ("src/app/Test.cpp", "components/vendor_driver/driver.c"):
            (app / relative).parent.mkdir(parents=True)
            (app / relative).write_text("void f() {}\n", encoding="utf-8")
        checked = {path.relative_to(app).as_posix() for path in brick_rules.sources([app])}
        if checked != {"src/app/Test.cpp"}:
            failures += 1
            print(f"FAIL vendored components/ should be skipped, checked: {sorted(checked)}")
    with tempfile.TemporaryDirectory() as directory:
        app = Path(directory) / "src" / "app" / "Test.cpp"
        app.parent.mkdir(parents=True)
        app.write_text("void Test::update()\n{\n    time_.delay_ms(10);\n}\n", encoding="utf-8")
        target = Path(directory) / "src" / "target" / "main.cpp"
        target.parent.mkdir(parents=True)
        target.write_text("void loop()\n{\n    board.time().delay_ms(10);\n}\n", encoding="utf-8")
        if "app-delay" not in {rule for _, rule, _ in brick_rules.check_file(app)[1]}:
            failures += 1
            print("FAIL delay in src/app was not reported")
        if "app-delay" in {rule for _, rule, _ in brick_rules.check_file(target)[1]}:
            failures += 1
            print("FAIL delay in src/target was reported")
    print("brick_rules self-test:", "OK" if failures == 0 else f"{failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
