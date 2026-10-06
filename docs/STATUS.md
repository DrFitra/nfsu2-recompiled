# Status

Last update: 2026-10-06. Only what has evidence is marked DONE.

| Milestone | State | Evidence |
|---|---|---|
| M0 C generated | **DONE** | `run_lift.py lift`: 27,742 functions, 70 files, 8.9 M lines, 0 lift failures |
| M1 C compiles | **DONE** | CMake + Ninja + MSVC 14.51 x86 `/O1`: all 70 TUs, 0 errors, 4 min 20 s with -j16 (largest TU 207 s) |
| M2 executable links | **DONE** | `build/NFSU2-Recompiled.exe`, 115 MB, 0 unresolved symbols |
| M3 CRT initializes | **DONE** | trace: `GetVersionExA`, `HeapCreate`, `TlsAlloc`, `GetStartupInfoA`, environment and locale setup, `_initterm` run through lifted code; `SetUnhandledExceptionFilter(0x766A55)` |
| M4 original entry point runs | **DONE** | `entering original entry point 0x0075BCC7`; 249 imports bound to Windows, 3 shimmed, 0 unresolved |
| M5 WinMain | **DONE** | the game's main (0x580E00 → 0x57ED10 → 0x5B7690) runs: single-instance check (Toolhelp process walk), D3DX `DisablePSGP` registry probe, CPU detection, registry settings (`Install Dir`, `Language`…), the multimedia-timer thread (callbacks Windows → lifted code via `timeSetEvent`, ~257/s), the file-system thread |
| M6 game window | **BLOCKED** | see below |
| M7 D3D9 init … M12 race | not started | |

## Current blocker: the game asks for Disc 2

- **Where:** `0x005C0D30` (called from `0x005B76B3` in the game's startup at `0x005B7690`) shows `MessageBoxA("Please insert Disc 2", "NFS Underground 2")`; cancelling calls `exit(0)` (`0x0075D5A4`).
- **Why:** `0x005BF450` walks drive letters and accepts only `GetDriveTypeA == DRIVE_CDROM` holding the game files; a global at `0x0079DC60` that would also accept other drive types is 0 in the file and is never written by any code. The remaining bypass is the existence of a file named `foobar` in the game folder (`0x0057CAC0("foobar")`), a developer switch.
- **Evidence that this is the game, not the recompilation:** the **original** `SPEED2.EXE` of this installation, started on the same machine, shows the same `#32770 "NFS Underground 2"` dialog after 10 s (oracle run on 2026-10-06; nothing was modified). The recompiled and original binaries agree up to this point.
- **Not done on purpose:** no patch or host-side fake of the disc check. Passing it legitimately needs the user's Disc 2 in a drive (or a mounted image of the user's own disc). Whether to use the game's own `foobar` developer switch is the owner's decision.

## Viability (lifter)

| Metric | Result |
|---|---|
| `.text` bytes | 3,676,913 |
| Functions recovered | 27,742 (incl. 4,599 aliases) |
| Real code coverage | ≈ 99.9 % (uncovered bytes are padding and pointer tables; ~2.1 KB unexplained) |
| Lifted functions | 27,742 (1 replaced by a documented override, 1 excluded as data) |
| `RECOMP_UNIMPL` sites | 2,907: 2,890 3DNow! (unreachable: CPUID reports no 3DNow!), 17 `int` in data/unreachable FDIV-workaround tables |
| Compile errors | 0 |
| Link errors | 0 |
| ICALL / ITAIL sites | 13,271 / 3,126; **0 unresolved at runtime so far** |
| difftest (lift32 + simd32 vs Unicorn) | 246 / 246 |

**Classification: GREEN** for the lifter: complete coverage, everything lifts
and compiles, differential tests pass, and the recompiled code reproduces the
original's behaviour as far as it has been run.

## Runtime issues found and fixed during bring-up

1. The guest range 0x400000–0x932000 was taken before `main` (first by a 64 MB
   main-thread stack, then by NLS/locale mappings and heaps). Fix: the host
   relaunches itself suspended and reserves the range in the child with
   `VirtualAllocEx` before the child's loader runs; the game runs on its own
   256 MB-stack thread.
2. lift32 emitted no `RECOMP_BACKEDGE`, so a lifted spin-wait could starve other
   guest threads under the machine lock (pcrecomp patch 0005).
3. The Cyrix probe at 0x6F5FC5 depends on DIV's undefined flags → documented
   override returning "not Cyrix".

## Known open issues

- `LoadIconA(0x400000, …)` returns NULL: Windows does not know the manually
  mapped guest module, so resource APIs on it need a shim (icon, and the 23
  RT_RCDATA resources).
- C++ exceptions thrown by guest code reach the native `RaiseException`, and
  Windows will not find the guest's handlers (they live in the simulated TIB).
  Not hit yet.

## Next steps

1. Pass the disc check legitimately (owner's disc), then reach `CreateWindowExA` (M6).
2. Shim `LoadIconA`/`FindResourceA`/`LoadResource`/`SizeofResource`/`LockResource` for the guest HMODULE.
3. D3D9 trace (`D3D_TRACE=summary|full`) around `Direct3DCreate9` and the device vtable → `docs/D3D9_USAGE.md`.
