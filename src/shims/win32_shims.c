/*
 * win32_shims.c - the few imports that cannot go straight to Windows.
 *
 * Everything else in SPEED2.EXE's import table is bound to the real function
 * by native32. A shim exists only where the host process differs from the
 * original one:
 *
 *   - the "main module" is the host exe, not SPEED2.EXE, so module-handle and
 *     module-path queries for NULL must answer with the guest image.
 *
 * Shims run inside the lifted model: arguments at g_esp+4.., result in g_eax,
 * and they pop their own stdcall arguments (g_esp += 4 + 4*n).
 */
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <string.h>
#include "recomp_types.h"
#include "native32.h"
#include "nfs_runtime.h"
#include "nfs_log.h"

#define ARG(i) (*(uint32_t *)(uintptr_t)(g_esp + 4 + 4 * (i)))
#define RET(v, nargs) do { g_eax = (uint32_t)(v); g_esp += 4 + 4 * (nargs); } while (0)

static int is_guest_name(const char *s) {
    const char *b = strrchr(s, '\\'), *c = strrchr(s, '/');
    if (c > b) b = c;
    b = b ? b + 1 : s;
    return !_stricmp(b, "SPEED2.EXE") || !_stricmp(b, "SPEED2");
}

/* HMODULE GetModuleHandleA(LPCSTR name) */
static void shim_GetModuleHandleA(void) {
    const char *name = (const char *)(uintptr_t)ARG(0);
    uint32_t r;
    if (!name || is_guest_name(name)) r = NFS_GUEST_BASE;
    else r = (uint32_t)(uintptr_t)GetModuleHandleA(name);
    NFS_LOG(WIN32, "GetModuleHandleA(%s) -> %08X", name ? name : "NULL", r);
    RET(r, 1);
}

/* DWORD GetModuleFileNameA(HMODULE m, LPSTR buf, DWORD n) */
static void shim_GetModuleFileNameA(void) {
    uint32_t m = ARG(0);
    char *buf = (char *)(uintptr_t)ARG(1);
    DWORD n = ARG(2), r;
    if (m == 0 || m == NFS_GUEST_BASE) {
        size_t len = strlen(g_nfs_exe_path);
        if (n == 0) { r = 0; }
        else if (len >= n) { memcpy(buf, g_nfs_exe_path, n - 1); buf[n - 1] = 0; r = n; SetLastError(ERROR_INSUFFICIENT_BUFFER); }
        else { memcpy(buf, g_nfs_exe_path, len + 1); r = (DWORD)len; }
    } else {
        r = GetModuleFileNameA((HMODULE)(uintptr_t)m, buf, n);
    }
    NFS_LOG(WIN32, "GetModuleFileNameA(%08X) -> \"%s\"", m, r ? buf : "");
    RET(r, 3);
}

native32_shim_t g_nfs_shims[] = {
    { "GetModuleHandleA", shim_GetModuleHandleA },
    { "GetModuleFileNameA", shim_GetModuleFileNameA },
};
int g_nfs_nshims = sizeof g_nfs_shims / sizeof g_nfs_shims[0];
