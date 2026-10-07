# nfsu2-recompiled

Static recompilation of **Need for Speed Underground 2** (PC, `SPEED2.EXE`,
x86-32) into C, using [pcrecomp](https://github.com/sp00nznet/pcrecomp).
The long-term target is a native ARM64 (Android) build; the first target is a
native Windows build that still calls the real Win32 / Direct3D 9.

```
SPEED2.EXE (x86-32) -> pcrecomp disasm32 -> lift32 (+ simd32) -> generated C -> MSVC/Clang -> native
```

**This repository contains no game code or data.** You must own the game and
point the tools at your own installation. Everything derived from the binary
(the generated C, analysis copies, logs) is produced locally and is gitignored.

## Status

Windows bring-up, updated **2026-10-07**. Input works and the user confirmed
correct car rendering after the signed-flags fix. Performance still needs work;
complete quick-race and career-race validation remains pending.

| Milestone | State |
|---|---|
| M0 C generated | **DONE**: 27,742 functions, 8.9 M lines, 0 lift failures |
| M1 C compiles / M2 links | **DONE**: MSVC x86, 0 errors, ~4.5 min with -j16 |
| M3 CRT / M4 entry point / M5 WinMain | **DONE**: the game's startup runs recompiled (registry, CPU detection, timer and file threads) |
| M6 window / M7 D3D9 / M8 first frame | **DONE**: window creation, device initialization and rendering |
| M9 intros / M10 menu input | **WORKING**: intros render and buttons respond, confirmed by the user |
| Car rendering | **IMPROVED**: the user confirmed correct visuals with the rebuilt executable |
| Race loading | **FIX APPLIED**: the previous null-read crash was traced to miscompiled signed 16-bit tests; the new build survived a 55-second smoke test |
| Complete races / performance | **PENDING**: verify both race modes end to end and profile the reported lag |

The smoke test reached 4,462 presented frames and ended deliberately at its
time limit. It does not certify a complete race. See
[the fix and validation report](docs/NARROW_FLAGS_MSVC.md) and
[the project review](docs/REVIEW_2026-10-07.md). Older milestone details in
[docs/STATUS.md](docs/STATUS.md) describe the earlier bring-up.

## Tested game build

| | |
|---|---|
| File | `SPEED2.EXE`, 4,800,512 bytes |
| SHA-256 | `f9dd86c054878ce6276beb07c1fd61874f7a1e4bf1f241b084c65b73e24168a7` |
| Embedded build date | `Feb 9 2005` (online `VERS` field) |
| Shape | already-unpacked image: `.text` plain code, rebuilt import table, original `.reloc` data kept in an unnamed section |

Other builds are untested. Details: [docs/NFSU2_BINARY_REPORT.md](docs/NFSU2_BINARY_REPORT.md).

## Requirements

- Windows, Visual Studio 2022/2026 with the C++ x86 toolset (MSVC), Windows SDK
- LLVM/clang (for pcrecomp's differential tests)
- Python 3.10+: `pip install capstone pefile unicorn keystone-engine`
- pcrecomp checked out next to this repo, with `patches/pcrecomp/*.patch` applied:

```bash
git clone https://github.com/sp00nznet/pcrecomp.git ../pcrecomp
cd ../pcrecomp
git checkout 35548b8
# Patches 0001-0008 are git-format-patch messages.
git am ../nfsu2-recompiled/patches/pcrecomp/000[1-8]-*.patch
# Patches 0009-0010 are plain diffs.
git apply ../nfsu2-recompiled/patches/pcrecomp/0009-*.patch
git apply ../nfsu2-recompiled/patches/pcrecomp/0010-*.patch
```

## Pipeline

```bash
source scripts/vsenv.sh x86                     # MSVC + clang in Git Bash
GAME="/path/to/Need for Speed Underground 2"

# 1. analysis copy with the relocation directory restored (original untouched)
python scripts/normalize_exe.py "$GAME/SPEED2.EXE" work/SPEED2.analysis.exe

# 2. seeds: vtables (+RTTI)
python ../pcrecomp/tools/cpp/vtable_scan.py work/SPEED2.analysis.exe --seeds work/vtable_seeds.json
python ../pcrecomp/tools/cpp/rtti.py        work/SPEED2.analysis.exe --seeds work/rtti_seeds.json

# 3. function catalog with basic blocks (~5 min)
python scripts/run_lift.py catalog work/SPEED2.analysis.exe work/seeds_all.json work/catalog.json

# 4. lift to C (~90 s) -> src/recomp/gen/ (gitignored)
python scripts/run_lift.py lift work/SPEED2.analysis.exe work/catalog.json src/recomp/gen

# reports
python scripts/recon.py work/SPEED2.analysis.exe work/catalog.json > work/recon.json
python scripts/coverage_gaps.py work/SPEED2.analysis.exe work/catalog.json
```

`seeds_all.json` is the union of the two seed files.

Build and run (the host finds the game by walking up to a folder named
`Need for Speed Underground 2`, or use `--game-root` / `NFSU2_ROOT`):

```bash
scripts/build.sh                     # cmake + ninja, MSVC x86
scripts/run.sh [--trace-native]      # logs/run_<stamp>.log, logs/crash_<stamp>.txt on failure
```

The host runs windowed by default; pass `--fullscreen` for fullscreen.

## Signed-flags regression test

MSVC 19.51 `/O1` miscompiled signed tests of left-aligned 16-bit values,
allowing negative identifiers into a lookup that then dereferenced NULL.
Patch 0010 uses explicit sign-bit tests and unsigned signed-order comparisons.
The regression passes with `/Od`, `/O1` and `/O2`; the baseline fails for
32,768 negative values with `/O1`.

From Git Bash in this repository:

```bash
source scripts/vsenv.sh x86
mkdir -p ../work
cl -nologo -O1 -I../pcrecomp/runtime/recomp32 \
  -Fe../work/narrow_flags.exe -Fo../work/narrow_flags.obj tests/narrow_flags.c
../work/narrow_flags.exe
```

## Design decisions

- **Lifter: pcrecomp `lift32` (global registers) + `native32` host** for the
  Windows bring-up: it models x87 control-word rounding, DF, lazy flags, is
  covered by pcrecomp's Unicorn differential test, and native32 already runs a
  whole game with real Win32/COM. lift32 had **no SSE** and silently dropped
  unknown instructions; both are fixed in `patches/pcrecomp` (see below).
- **Fail loudly**: unimplemented instructions lift to `RECOMP_UNIMPL(va, text)`
  and unresolved indirect calls go to a host hook (`RECOMP_STRICT_ICALL`)
  instead of returning 0.
- **Portability**: generated code reaches memory only through `ADDR(va)`
  (`g_mem_base`), so the same C can run on a 64-bit host with the guest image
  elsewhere. 3DNow! is not modelled; the runtime will report no 3DNow! in
  CPUID so CPU-dispatched code takes the SSE/x87 paths.

## pcrecomp changes made for this project

| Patch | What |
|---|---|
| 0001 | `pe_analyze`: code range = adjacent executable sections holding the entry point (SPEED2's rebuilt import section made 1.7 MB of data "code"); `stdcall_argc`: skip a partial Windows SDK |
| 0002 | `lift32`: MMX/SSE/SSE2 via new `simd32.py`; `RECOMP_UNIMPL` instead of silent no-ops; difftest +40 SIMD cases (242/242 vs Unicorn) |
| 0003 | `RECOMP_STRICT_ICALL`: unresolved ICALL/ITAIL go to a host hook |
| 0004 | Lazy-flag `lahf` and x87 environment save/restore |
| 0005 | Backward-branch yields for guest-thread progress |
| 0006 | Post-call hooks and API names for tracing |
| 0007 | Pre-call hooks for native argument inspection/rewriting |
| 0008 | x87 TOP reporting, fair FIFO machine lock and hashed name lookup |
| 0009 | Optional native execution by address range for hybrid debugging |
| 0010 | Explicit sign handling for narrow flags to avoid the MSVC optimization regression |

## Android

`ICONO/ICONO.png` is the icon of the future Android APK ("Android Evolved").

There is no Android APK or ARM64 host yet. The current build requires MSVC
x86 and real Win32/Direct3D 9. Android needs a portable guest-memory and
callback model, platform API implementations, graphics translation, audio,
input and lifecycle integration. The next Windows milestone is a reproducible
complete race with performance measurements before moving those subsystems.

## Legal

Code here is MIT-licensed tooling and documentation. Need for Speed and all
game content belong to Electronic Arts. Do not open issues or PRs containing
game files, generated C, or dumps.
