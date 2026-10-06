/*
 * dinput_trace.c - name and watch the DirectInput 8 objects the game uses.
 *
 * Input goes through real dinput8.dll (keyboard/mouse/joystick devices,
 * SetCooperativeLevel on the game window, Acquire, GetDeviceState/Data).
 * Every call is logged the first few times and every failing HRESULT is
 * logged always, because a device that silently fails to acquire looks like
 * "the buttons do nothing".
 */
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "native32.h"
#include "nfs_log.h"
#include "nfs_runtime.h"

static const char *k_di8[11] = { "QueryInterface", "AddRef", "Release", "CreateDevice", "EnumDevices",
    "GetDeviceStatus", "RunControlPanel", "Initialize", "FindDevice", "EnumDevicesBySemantics",
    "ConfigureDevices" };
#define NDEVM 32
static const char *k_did8[NDEVM] = { "QueryInterface", "AddRef", "Release", "GetCapabilities", "EnumObjects",
    "GetProperty", "SetProperty", "Acquire", "Unacquire", "GetDeviceState", "GetDeviceData",
    "SetDataFormat", "SetEventNotification", "SetCooperativeLevel", "GetObjectInfo", "GetDeviceInfo",
    "RunControlPanel", "Initialize", "CreateEffect", "EnumEffects", "GetEffectInfo",
    "GetForceFeedbackState", "SendForceFeedbackCommand", "EnumCreatedEffectObjects", "Escape", "Poll",
    "SendDeviceData", "EnumEffectsInFile", "WriteEffectToFile", "BuildActionMap", "SetActionMap",
    "GetImageInfo" };

static uint32_t g_di_vt[11];
#define MAXDEV 8
static struct { uint32_t obj, vt[NDEVM]; uint32_t calls[NDEVM], fails[NDEVM]; } g_dev[MAXDEV];
static int g_ndev;

void nfs_dinput_register(uint32_t di8) {
    const uint32_t *vt = *(const uint32_t **)(uintptr_t)di8;
    for (int i = 0; i < 11; i++) { g_di_vt[i] = vt[i]; native32_add_name(vt[i], "IDirectInput8", k_di8[i]); }
}

int nfs_dinput_post(uint32_t fn, const uint32_t *a, uint32_t ret) {
    if (g_di_vt[3] && fn == g_di_vt[3]) {                 /* IDirectInput8::CreateDevice(this, rguid, out, outer) */
        const uint32_t *g = (const uint32_t *)(uintptr_t)a[1];
        uint32_t dev = ret == 0 ? *(uint32_t *)(uintptr_t)a[2] : 0;
        NFS_LOG(WIN32, "DirectInput CreateDevice guid {%08X-...} -> %08X dev %08X", g ? g[0] : 0, ret, dev);
        if (dev && g_ndev < MAXDEV) {
            const uint32_t *vt = *(const uint32_t **)(uintptr_t)dev;
            g_dev[g_ndev].obj = dev;
            for (int i = 0; i < NDEVM; i++) { g_dev[g_ndev].vt[i] = vt[i]; native32_add_name(vt[i], "IDirectInputDevice8", k_did8[i]); }
            g_ndev++;
        }
        return 1;
    }
    for (int i = 0; i < 11; i++)
        if (g_di_vt[i] && fn == g_di_vt[i] && i != 1 && i != 2) {
            NFS_LOG(WIN32, "IDirectInput8 %08X %s(%08X %08X %08X %08X) -> %08X", a[0], k_di8[i], a[1], a[2], a[3], a[4], ret);
            return 1;
        }
    /* Devices handed to the game by EnumDevicesBySemantics' callback were never
     * returned by CreateDevice: recognise any object by its vtable, and give
     * each distinct `this` its own slot. */
    if (!g_ndev) return 0;
    int i;
    for (i = 0; i < NDEVM; i++) if (g_dev[0].vt[i] == fn) break;
    if (i == NDEVM) return 0;
    int d;
    for (d = 0; d < g_ndev; d++) if (g_dev[d].obj == a[0]) break;
    if (d == g_ndev) {
        if (g_ndev == MAXDEV) return 1;
        g_dev[d].obj = a[0];
        memcpy(g_dev[d].vt, g_dev[0].vt, sizeof g_dev[0].vt);
        g_ndev++;
        NFS_LOG(WIN32, "dinput: new device object dev%d = %08X (first call %s)", d, a[0], k_did8[i]);
    }
    uint32_t n = ++g_dev[d].calls[i];
    if (ret & 0x80000000u) g_dev[d].fails[i]++;
    if (n <= 8 || ((ret & 0x80000000u) && g_dev[d].fails[i] <= 64))
        NFS_LOG(WIN32, "dinput dev%d %s(%08X %08X %08X) -> %08X%s", d, k_did8[i], a[1], a[2], a[3],
                ret, (ret & 0x80000000u) ? "  <-- FAILED" : "");
    return 1;
}
