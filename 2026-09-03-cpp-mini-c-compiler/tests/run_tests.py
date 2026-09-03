#!/usr/bin/env python3
"""End-to-end test runner.

For every tests/programs/*.c: compile it two independent ways --
  A) mcc -> assembly -> assembled/linked with gcc + the runtime
  B) gcc compiling the .c file directly, alongside the same runtime
and assert the resulting binaries agree on stdout and exit code. Since these
are ordinary standard-C source files, gcc's build is a trustworthy oracle:
if mcc's output ever disagrees with it, that's a real mcc bug.

For every tests/invalid/*.c: assert mcc rejects it (nonzero exit).
"""
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
MCC = ROOT / "build" / "mcc"
RUNTIME = ROOT / "runtime" / "runtime.c"
TMP = ROOT / "build" / "test_tmp"


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def compile_with_mcc(src: pathlib.Path, tag: str) -> pathlib.Path:
    asm = TMP / f"{src.stem}.mcc.s"
    r = run([str(MCC), str(src), "-o", str(asm)])
    if r.returncode != 0:
        raise AssertionError(f"mcc rejected valid program {src.name}:\n{r.stderr}")
    binpath = TMP / f"{src.stem}.mcc.bin"
    r = run(["gcc", str(asm), str(RUNTIME), "-o", str(binpath)])
    if r.returncode != 0:
        raise AssertionError(f"assembling mcc's output for {src.name} failed:\n{r.stderr}")
    return binpath


def compile_with_gcc(src: pathlib.Path) -> pathlib.Path:
    binpath = TMP / f"{src.stem}.gcc.bin"
    r = run(["gcc", str(src), str(RUNTIME), "-o", str(binpath)])
    if r.returncode != 0:
        raise AssertionError(f"reference gcc build of {src.name} failed:\n{r.stderr}")
    return binpath


def execute(binpath: pathlib.Path):
    r = run([str(binpath)])
    return r.stdout, r.returncode


def test_valid_programs() -> tuple[int, int]:
    programs = sorted((ROOT / "tests" / "programs").glob("*.c"))
    passed = 0
    for src in programs:
        try:
            mcc_bin = compile_with_mcc(src, "mcc")
            gcc_bin = compile_with_gcc(src)
            mcc_out, mcc_code = execute(mcc_bin)
            gcc_out, gcc_code = execute(gcc_bin)
            if mcc_out != gcc_out or mcc_code != gcc_code:
                print(f"FAIL {src.name}: mcc(stdout={mcc_out!r}, exit={mcc_code}) "
                      f"!= gcc(stdout={gcc_out!r}, exit={gcc_code})")
                continue
            print(f"PASS {src.name}  (exit={mcc_code}, {len(mcc_out.splitlines())} line(s) of output)")
            passed += 1
        except AssertionError as e:
            print(f"FAIL {src.name}: {e}")
    return passed, len(programs)


def test_invalid_programs() -> tuple[int, int]:
    programs = sorted((ROOT / "tests" / "invalid").glob("*.c"))
    passed = 0
    for src in programs:
        r = run([str(MCC), str(src), "-o", str(TMP / f"{src.stem}.s")])
        if r.returncode == 0:
            print(f"FAIL {src.name}: mcc accepted an invalid program")
            continue
        print(f"PASS {src.name}  (rejected: {r.stderr.strip()})")
        passed += 1
    return passed, len(programs)


def main() -> int:
    TMP.mkdir(parents=True, exist_ok=True)
    if not MCC.exists():
        print("build/mcc not found -- run `make all` first", file=sys.stderr)
        return 1

    print("== valid programs (differential vs gcc) ==")
    v_passed, v_total = test_valid_programs()
    print("\n== invalid programs (must be rejected) ==")
    i_passed, i_total = test_invalid_programs()

    total_passed, total = v_passed + i_passed, v_total + i_total
    print(f"\n{total_passed}/{total} test programs passed")
    return 0 if total_passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
