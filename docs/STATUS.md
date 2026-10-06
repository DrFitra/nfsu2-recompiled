# Status

Last update: 2026-10-06. Only what has evidence is marked DONE.

| Milestone | State | Evidence |
|---|---|---|
| M0 C generated | **DONE** | `run_lift.py lift`: 27,742 functions, 70 files, 8,935,801 lines, 0 lift failures |
| M1 C compiles | IN PROGRESS | `recomp_0010.c` (400 functions) compiles with MSVC 14.51 x86 `/O1` in 1.4 s; the largest file (`recomp_0034.c`, 1.6 M lines) was still compiling when work stopped; no CMake project yet |
| M2 executable links | BLOCKED on M1 | needs host (`src/host`), runtime globals (`g_xmm`, `g_mxcsr`, `recomp_unimpl`, `recomp_unresolved`), CMake |
| M3 CRT initializes | not started | |
| M4 original entry point runs | not started | |
| M5 WinMain | not started | |
| M6 game window | not started | |
| M7 D3D9 init | not started | |
| M8 first frame | not started | |
| M9 EA logo / intro | not started | |
| M10 main menu | not started | |
| M11 garage | not started | |
| M12 race | not started | |

## Viability (lifter), so far

| Metric | Result |
|---|---|
| `.text` bytes | 3,676,913 |
| Functions recovered | 27,742 (incl. 4,599 aliases: 2,220 interior labels, 2,379 C++ EH stubs/funclets) |
| Byte coverage | 96.4 % of `.text`; of the 132,204 uncovered bytes, 96,170 are padding and 33,914 are pointer tables (relocation-backed) inside `.text`; ~2.1 KB unexplained → **≈99.9 % of real code** |
| Lifted functions | 27,742 / 27,742 |
| `RECOMP_UNIMPL` sites | 2,923: 2,890 3DNow! (CPU-dispatched, avoidable via CPUID), 4 `fnstenv/fldenv` (CRT), 17 `int`, 12 in one false function at 0x6D5610 (data decoded as code), 2 `lahf` |
| ICALL / ITAIL sites | 13,271 / 3,126 (resolved at runtime through the dispatch table) |
| Compile errors | not measured yet (see M1) |
| difftest (pcrecomp lift32, patched) | 242 / 242 match Unicorn (202 base + 40 new SIMD) |
| difftest on real NFSU2 functions | not run yet |

Classification so far: **GREEN/YELLOW** — coverage and lift are complete; the
open questions are compile time of the very large files, C++ exception
dispatch across the native boundary, and threading.

## Next steps (specific)

1. Lift `lahf` (2 sites) and `fnstenv/fldenv` (CRT, 0x76A496 / 0x76A74E) in
   lift32; inspect the 17 `int` sites (0x55FF54, 0x769DBA..) and drop the false
   function at 0x6D5610 from the catalog.
2. Measure `recomp_0034.c` compile time; if MSVC is too slow on its giant
   functions, build that TU at `/Od` or split by function size.
3. Write the host: `native32` map/bind of `SPEED2.EXE` at 0x400000, shims for
   `GetModuleHandleA(NULL)` / `GetModuleFileNameA` (return the guest image and
   its real path), CPUID without 3DNow!, crash report + `logs/crash_*.txt`.
4. CMake (Ninja + MSVC x86, `/BASE:0x60000000 /DYNAMICBASE:NO /LARGEADDRESSAWARE`), link → M2, run → M3/M4.
