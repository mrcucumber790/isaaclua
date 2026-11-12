/* Remote control (debug build only): the game connects out over Wi-Fi to a relay (tools/relay.py) on a server
 * on the internet, so tests can be run on the console from far away. rom:/isaaclua_remote.ini:
 *     host = 1.2.3.4     the relay's IPv4 address
 *     port = 80
 *     key  = <secret>    the relay only accepts a console that knows it
 * The relay sends Lua chunks; src/remote.lua runs them between frames (as coroutines, so they can wait for the
 * game) and the answer goes back. The network runs on its own thread; Lua only ever runs on the game's thread.
 *
 * Wire format, both ways: "<type> <length>\n<payload>". Console -> relay: H hello ("<key> <version>"),
 * R result ("<id>\n<text>"), P ping. shot() in remote.lua
 * answers with the screen as text: "IMG <w> <h>\n" and base64 RGB. Relay -> console: C chunk ("<id>\n<lua>"), P ping.
 *
 * Sockets and the network: the game's KAGE::Network::Manager::Initialize (EngineStartup) already set up
 * nn::socket (6 MB pool) and nn::nifm; we ask nifm for the connection (SubmitNetworkRequestAndWait) and use
 * the nnsocket* C functions the game itself imports. */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "lua.h"
#include "lauxlib.h"
#include "isaaclua.h"
#include "version.h"
#include "nn.h"

typedef uint8_t u8;
typedef uint32_t u32;

/* nn::os threads */
typedef struct { _Alignas(16) u8 raw[0x400]; } nnos_ThreadType;
u32 nnos_CreateThread(nnos_ThreadType *t, void (*fn)(void *), void *arg, void *stack, size_t size, int prio)
    __asm__("_ZN2nn2os12CreateThreadEPNS0_10ThreadTypeEPFvPvES3_S3_mi");
void nnos_StartThread(nnos_ThreadType *t) __asm__("_ZN2nn2os11StartThreadEPNS0_10ThreadTypeE");
void nnos_SleepThread(int64_t ns) __asm__("_ZN2nn2os11SleepThreadENS_8TimeSpanE");
/* nn::nifm */
void nnifm_SubmitNetworkRequestAndWait(void) __asm__("_ZN2nn4nifm27SubmitNetworkRequestAndWaitEv");
bool nnifm_IsNetworkAvailable(void) __asm__("_ZN2nn4nifm18IsNetworkAvailableEv");
/* nn::socket (BSD layout: sin_len first) */
struct nn_sockaddr_in { u8 len, family; uint16_t port; u32 addr; u8 zero[8]; };
struct nn_pollfd { int fd; short events, revents; };
int nnsocketSocket(int domain, int type, int proto);
int nnsocketConnect(int s, const void *addr, u32 len);
long nnsocketSend(int s, const void *buf, size_t n, int flags);
long nnsocketRecv(int s, void *buf, size_t n, int flags);
int nnsocketPoll(struct nn_pollfd *fds, unsigned long n, int timeout_ms);
int nnsocketClose(int s);
uint16_t nnsocketInetHtons(uint16_t v);
int nnsocketGetLastErrno(void);
u32 nnsocket_Initialize(void *pool, size_t size, size_t alloc, int concurrency) __asm__("_ZN2nn6socket10InitializeEPvmmi");
void nnifm_Initialize(void) __asm__("_ZN2nn4nifm10InitializeEv");
int nn_InetPton(int af, const char *src, void *dst) __asm__("_ZN2nn6socket8InetPtonEiPKcPv");

#define CMD_MAX (1100 * 1024)  // a put() chunk: 768 KB as base64 (the relay takes bodies up to 1 MB)
#define RES_MAX (4 << 20)  // a shot() is about 0.9 MB

static char s_host[64], s_key[80];
static int s_port = 80;
static bool s_enabled, s_started;
/* handed between the threads: the network thread fills s_cmd and sets s_cmd_ready; the game thread runs it,
 * fills s_res and sets s_res_ready; the network thread sends it and clears both */
static char *s_cmd;  // CMD_MAX, malloc'd with s_in and the payload buffer when the thread starts
static char s_cmd_id[24], s_res_id[24];
static volatile int s_cmd_ready, s_res_ready, s_connected;
static char *s_res;  // malloc'd, freed by the network thread once sent
static size_t s_res_n;
static u32 s_press_mask;
static int s_press_frames;

static void set_flag(volatile int *f, int v) { __atomic_store_n(f, v, __ATOMIC_RELEASE); }
static int get_flag(volatile int *f) { return __atomic_load_n(f, __ATOMIC_ACQUIRE); }

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    return s;
}

/* reads rom:/isaaclua_remote.ini; true when remote control is on (the autotest then stays off) */
bool remote_config(void) {
#ifndef REMOTE_INI
#define REMOTE_INI "rom:/isaaclua_remote.ini"
#endif
    FILE *f = fopen(REMOTE_INI, "r");
    if (!f) return false;
    static char buf[1024];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    for (char *line = buf, *next; line && *line; line = next) {
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        char *eq = strchr(line, '=');
        if (!eq || line[0] == '#') continue;
        *eq = 0;
        char *k = trim(line), *v = trim(eq + 1);
        if (strcmp(k, "host") == 0) snprintf(s_host, sizeof s_host, "%s", v);
        else if (strcmp(k, "port") == 0) s_port = (int)strtol(v, NULL, 10);
        else if (strcmp(k, "key") == 0) snprintf(s_key, sizeof s_key, "%s", v);
    }
    s_enabled = s_host[0] && s_key[0] && s_port > 0;
    return s_enabled;
}

/* ------------------------------------------------------------ network thread */

static bool send_all(int s, const char *p, size_t n) {
    while (n) {
        long k = nnsocketSend(s, p, n, 0);
        if (k <= 0) return false;
        p += k, n -= (size_t)k;
    }
    return true;
}
static bool send_frame(int s, char type, const char *a, size_t an, const char *b, size_t bn) {
    char head[32];
    int h = snprintf(head, sizeof head, "%c %zu\n", type, an + bn);
    return send_all(s, head, (size_t)h) && send_all(s, a, an) && (!bn || send_all(s, b, bn));
}

static char *s_in;  // CMD_MAX + 64
static size_t s_in_n;
/* one whole frame from s_in, if there is one: its type and payload (moved out of s_in) */
static int take_frame(char *type, char *payload, size_t cap) {
    char *nl = memchr(s_in, '\n', s_in_n);
    if (!nl) return s_in_n > 64 ? -1 : 0;
    *nl = 0;
    char t = s_in[0];
    if (s_in[1] != ' ') return -1;
    char *end;
    size_t len = (size_t)strtoul(s_in + 2, &end, 10);
    if (end == s_in + 2 || *end || len >= cap) return -1;
    size_t head = (size_t)(nl - s_in) + 1;
    if (s_in_n < head + len) {
        *nl = '\n';
        return 0;
    }
    memcpy(payload, s_in + head, len);
    payload[len] = 0;
    memmove(s_in, s_in + head + len, s_in_n - head - len);
    s_in_n -= head + len;
    *type = t;
    return 1;
}

static void session(int s) {
    char hello[160];
    int hn = snprintf(hello, sizeof hello, "%s %s", s_key, ISAACLUA_VERSION_TEXT);
    if (!send_frame(s, 'H', hello, (size_t)hn, NULL, 0)) return;
    set_flag(&s_connected, 1);
    s_in_n = 0;
    static char *payload;
    if (!payload) payload = malloc(CMD_MAX + 32);
    if (!payload) return;
    int idle = 0;
    for (;;) {
        if (get_flag(&s_res_ready)) {  // "<id>\n<text>" as one frame
            char head[64];
            size_t idn = strlen(s_res_id);
            int h = snprintf(head, sizeof head, "R %zu\n%s\n", idn + 1 + s_res_n, s_res_id);
            bool ok = send_all(s, head, (size_t)h) && send_all(s, s_res, s_res_n);
            free(s_res), s_res = NULL;
            set_flag(&s_res_ready, 0);
            if (!ok) return;
            set_flag(&s_cmd_ready, 0);
        }
        struct nn_pollfd p = {s, 1, 0};
        int r = nnsocketPoll(&p, 1, 50);
        if (r < 0) return;
        if (r > 0) {
            long k = nnsocketRecv(s, s_in + s_in_n, CMD_MAX + 64 - s_in_n, 0);
            if (k <= 0) return;
            s_in_n += (size_t)k;
            idle = 0;
            char type;
            int got;
            while ((got = take_frame(&type, payload, CMD_MAX + 32)) == 1) {
                if (type == 'P') continue;
                if (type == 'C' && !get_flag(&s_cmd_ready)) {
                    char *nl = strchr(payload, '\n');
                    if (!nl) continue;
                    *nl = 0;
                    snprintf(s_cmd_id, sizeof s_cmd_id, "%s", payload);
                    snprintf(s_cmd, CMD_MAX, "%s", nl + 1);
                    set_flag(&s_cmd_ready, 1);
                }
            }
            if (got < 0) return;
        } else if (++idle >= 200) {  // 10 s of silence: a ping keeps NAT and the relay's timeout happy
            idle = 0;
            if (!send_frame(s, 'P', "", 0, NULL, 0)) return;
        }
    }
}

/* what the network thread is doing, shown on screen by the game thread when it changes */
static char s_status[96];
static volatile int s_status_seq;
static void status(const char *fmt, int a) {
    snprintf(s_status, sizeof s_status, fmt, a);
    __atomic_add_fetch(&s_status_seq, 1, __ATOMIC_RELEASE);
}

/* nn::socket set up by us when the game has not (KAGE::Network::Manager::Initialize from EngineStartup is
 * behind a flag): a 1 MB pool, 4 sockets */
static bool own_socket_init(void) {
    static bool done;
    if (done) return false;
    done = true;
    nnifm_Initialize();
    u8 *mem = malloc(0x100000 + 0x1000);
    if (!mem) return false;
    void *pool = (void *)(((uintptr_t)mem + 0xfff) & ~(uintptr_t)0xfff);
    u32 r = nnsocket_Initialize(pool, 0x100000, 0x20000, 4);
    if (r) status("remote: nn::socket::Initialize failed 0x%x", (int)r);
    return r == 0;
}

static void net_thread(void *arg) {
    (void)arg;
    nnos_SleepThread(5000000000LL);  // let the game finish starting
    s_cmd = malloc(CMD_MAX), s_in = malloc(CMD_MAX + 64);
    if (!s_cmd || !s_in) {
        status("remote: out of memory", 0);
        return;
    }
    for (int tries = 0;; tries++) {
        nnifm_SubmitNetworkRequestAndWait();
        if (!nnifm_IsNetworkAvailable()) {
            if (tries % 12 == 0) status("remote: no network (Wi-Fi?)", 0);
        } else {
            int s = nnsocketSocket(2, 1, 0);  // AF_INET, SOCK_STREAM
            if (s < 0 && own_socket_init()) s = nnsocketSocket(2, 1, 0);
            struct nn_sockaddr_in a = {sizeof a, 2, nnsocketInetHtons((uint16_t)s_port), 0, {0}};
            if (s < 0) {
                if (tries % 12 == 0) status("remote: no socket (errno %d)", nnsocketGetLastErrno());
            } else if (nn_InetPton(2, s_host, &a.addr) != 1) {
                status("remote: host must be an IPv4 address", 0);
            } else if (nnsocketConnect(s, &a, sizeof a) != 0) {
                if (tries % 12 == 0) status("remote: cannot reach the relay (errno %d)", nnsocketGetLastErrno());
            } else {
                status("remote: connected to the relay", 0);
                session(s);
                status("remote: disconnected, retrying", 0);
            }
            if (s >= 0) nnsocketClose(s);
            set_flag(&s_connected, 0);
        }
        nnos_SleepThread(5000000000LL);
    }
}

/* ------------------------------------------------------------ game thread */

static nnos_ThreadType s_thread;
static _Alignas(0x1000) u8 s_stack[0x10000];
static bool s_running;  // a chunk is running (resumed once per frame)

static void post_result(const char *text) {
    size_t n = strlen(text);
    if (n > RES_MAX) n = RES_MAX;
    s_res = malloc(n ? n : 1);
    if (!s_res) n = 0, s_res = malloc(1);
    if (n) memcpy(s_res, text, n);
    s_res_n = n;
    memcpy(s_res_id, s_cmd_id, sizeof s_res_id);
    s_running = false;
    set_flag(&s_res_ready, 1);
}

/* once per frame on the game's thread, outside Lua: starts the network thread, runs the relay's chunks */
void remote_frame(lua_State *L) {
    if (!s_enabled || !L) return;
    if (!s_started) {
        s_started = true;
        if (nnos_CreateThread(&s_thread, net_thread, NULL, s_stack, sizeof s_stack, 16) == 0) nnos_StartThread(&s_thread);
        else overlay_add("remote: no thread", OVERLAY_ERROR);
    }
    if (s_press_frames > 0 && --s_press_frames == 0) s_press_mask = 0;
    static int seen;
    int seq = __atomic_load_n(&s_status_seq, __ATOMIC_ACQUIRE);
    if (seq != seen) seen = seq, overlay_add(s_status, OVERLAY_WARN);
    if (!get_flag(&s_cmd_ready) || get_flag(&s_res_ready)) return;
    int top = lua_gettop(L);
    if (lua_getfield(L, LUA_REGISTRYINDEX, "isaaclua.remote") != LUA_TTABLE) {
        lua_settop(L, top);
        post_result("ERROR remote.lua missing");
        return;
    }
    lua_getfield(L, -1, s_running ? "step" : "start");
    int nargs = 0;
    if (!s_running) lua_pushstring(L, s_cmd), nargs = 1;
    s_running = true;
    if (lua_pcall(L, nargs, 1, 0) != LUA_OK) post_result(lua_tostring(L, -1));
    else if (lua_isstring(L, -1)) post_result(lua_tostring(L, -1));
    lua_settop(L, top);
}

/* pad buttons the relay holds down (press("A", frames)), ORed into the game's npad state */
u32 remote_buttons(void) { return s_press_mask; }
void remote_press(u32 mask, int frames) {
    s_press_mask |= mask;
    if (frames > s_press_frames) s_press_frames = frames;
}
bool remote_connected(void) { return get_flag(&s_connected); }

/* ------------------------------------------------------------ screenshots */
typedef struct { u8 raw[0x20]; } AlbumFileEntry;  // size, then AlbumFileId: app id, datetime (+0x10), storage, contents
uint32_t nnalbum_Initialize(void) __asm__("_ZN2nn5album10InitializeEv");
uint32_t nnalbum_SaveCurrentScreenshot(int report) __asm__("_ZN2nn5album21SaveCurrentScreenshotENS0_17AlbumReportOptionE");
uint32_t nnalbum_GetAlbumScreenshotFileList(int *count, AlbumFileEntry *out, int max)
    __asm__("_ZN2nn5album26GetAlbumScreenshotFileListEPiPNS0_14AlbumFileEntryEi");
uint32_t nnalbum_LoadAlbumImage(int *w, int *h, void *buf, size_t size, void *work, size_t worksize, const AlbumFileEntry *e)
    __asm__("_ZN2nn5album14LoadAlbumImageEPiS1_PvmS2_mRKNS0_14AlbumFileEntryE");

static uint64_t entry_time(const AlbumFileEntry *e) {
    const u8 *d = e->raw + 0x10;  // year (u16), month, day, hour, minute, second, id
    return ((uint64_t)(d[0] | d[1] << 8) << 48) | (uint64_t)d[2] << 40 | (uint64_t)d[3] << 32 | (uint64_t)d[4] << 24 |
           (uint64_t)d[5] << 16 | (uint64_t)d[6] << 8 | d[7];
}
/* core.remote_shot() -> width, height, base64 RGB (the screen saved to the album and read back, nn::album lets a
 * game read its own screenshots; halved to 640x360), or nil and what failed */
int remote_shot(lua_State *L) {
    static bool inited;
    if (!inited) inited = true, nnalbum_Initialize();
    /* the list must hold the whole album: with more files than kMax the new shot may not be listed (HW 2026-11-05) */
    enum { kMax = 10000, W = 1280, H = 720, kWork = 4 << 20 };
    AlbumFileEntry *list = malloc(kMax * sizeof *list);
    u8 *rgba = malloc((size_t)W * H * 4), *work = malloc(kWork);
    const char *err = NULL;
    int count = 0, w = 0, h = 0, best = -1;
    uint32_t rc = 0;
    /* the newest shot before saving: the album gets the new file a little later (HW 0.99.12: shot() answered
     * with an old picture), so wait until one newer than it shows up */
    uint64_t before = 0;
    if (list && nnalbum_GetAlbumScreenshotFileList(&count, list, kMax) == 0)
        for (int i = 0; i < count; i++)
            if (entry_time(&list[i]) > before) before = entry_time(&list[i]);
    if (!list || !rgba || !work) err = "out of memory";
    else if ((rc = nnalbum_SaveCurrentScreenshot(0)) != 0) err = "SaveCurrentScreenshot";
    else {
        for (int tries = 0; tries < 30 && best < 0; tries++) {
            if (tries) nnos_SleepThread(100000000LL);
            if (nnalbum_GetAlbumScreenshotFileList(&count, list, kMax) != 0) continue;
            for (int i = 0; i < count; i++)
                if (entry_time(&list[i]) > before && (best < 0 || entry_time(&list[i]) > entry_time(&list[best]))) best = i;
        }
        if (best < 0) err = count >= kMax ? "no new screenshot in the album (over 10000 files?)" : "no new screenshot in the album (album full?)";
        else if ((rc = nnalbum_LoadAlbumImage(&w, &h, rgba, (size_t)W * H * 4, work, kWork, &list[best])) != 0) err = "LoadAlbumImage";
        else if (w != W || h != H) err = "unexpected size";
    }
    free(list), free(work);
    if (err) {
        free(rgba);
        return lua_pushnil(L), lua_pushfstring(L, "%s (result %d)", err, (int)rc), 2;
    }
    /* halve in place (RGB, 2x2 average), then base64 */
    int ow = w / 2, oh = h / 2;
    u8 *o = rgba;
    for (int y = 0; y < oh; y++)
        for (int x = 0; x < ow; x++) {
            const u8 *a = rgba + ((size_t)(2 * y) * W + 2 * x) * 4, *b = a + (size_t)W * 4;
            u8 px[3];
            for (int c = 0; c < 3; c++) px[c] = (u8)((a[c] + a[4 + c] + b[c] + b[4 + c] + 2) / 4);
            memcpy(o, px, 3), o += 3;
        }
    size_t n = (size_t)(o - rgba);
    static const char k64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char *out = malloc((n + 2) / 3 * 4);
    if (!out) return free(rgba), lua_pushnil(L), lua_pushliteral(L, "out of memory"), 2;
    size_t k = 0;
    for (size_t i = 0; i < n; i += 3) {
        u32 v = (u32)rgba[i] << 16 | (u32)(i + 1 < n ? rgba[i + 1] : 0) << 8 | (i + 2 < n ? rgba[i + 2] : 0);
        out[k++] = k64[v >> 18 & 63], out[k++] = k64[v >> 12 & 63];
        out[k++] = i + 1 < n ? k64[v >> 6 & 63] : '=';
        out[k++] = i + 2 < n ? k64[v & 63] : '=';
    }
    free(rgba);
    lua_pushinteger(L, ow);
    lua_pushinteger(L, oh);
    lua_pushlstring(L, out, k);
    free(out);
    return 3;
}

/* ------------------------------------------------------------ save data check */
uint32_t nnfs_GetFreeSpaceSize(int64_t *out, const char *path) __asm__("_ZN2nn2fs16GetFreeSpaceSizeEPlPKc");
uint32_t nnfs_GetTotalSpaceSize(int64_t *out, const char *path) __asm__("_ZN2nn2fs17GetTotalSpaceSizeEPlPKc");
static uint32_t sd_mount(void);
/* core.remote_fs(dir) -> text: free/total space of the save data, the folder's files, and a test write step by step */
int remote_fs(lua_State *L) {
    const char *dir = luaL_optstring(L, 1, "sdmc:/isaaclua");
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    char line[1024];
    int64_t fr = -1, tot = -1;
    char mnt[16] = "sdmc:/";
    const char *colon = strchr(dir, ':');
    if (colon && colon - dir < 10) snprintf(mnt, sizeof mnt, "%.*s:/", (int)(colon - dir), dir);
    if (strncmp(dir, "sd:/", 4) == 0) sd_mount();
    uint32_t r1 = nnfs_GetFreeSpaceSize(&fr, mnt), r2 = nnfs_GetTotalSpaceSize(&tot, mnt);
    snprintf(line, sizeof line, "free %lld (0x%x) total %lld (0x%x)\n", (long long)fr, r1, (long long)tot, r2);
    luaL_addstring(&b, line);
    nnfs_DirectoryHandle d;
    if (nnfs_OpenDirectory(&d, dir, 3) == 0) {
        enum { kMax = 64 };
        nnfs_DirectoryEntry *e = malloc(kMax * sizeof *e);
        int64_t n = 0, total = 0, files = 0;
        while (e && nnfs_ReadDirectory(&n, e, d, kMax) == 0 && n > 0)
            for (int64_t i = 0; i < n; i++) {
                files++, total += e[i].size;
                if (files <= 400) {
                    snprintf(line, sizeof line, "%c %lld %s\n", e[i].type ? 'f' : 'd', (long long)e[i].size, e[i].name);
                    luaL_addstring(&b, line);
                }
            }
        free(e);
        nnfs_CloseDirectory(d);
        snprintf(line, sizeof line, "%lld entries, %lld bytes\n", (long long)files, (long long)total);
        luaL_addstring(&b, line);
    } else luaL_addstring(&b, "cannot open the folder\n");
    if (strncmp(dir, "sdmc:/", 6) != 0) return luaL_pushresult(&b), 1;
    const char *p = "sdmc:/isaaclua/_fstest.dat";
    nnfs_DeleteFile(p);
    uint32_t c = nnfs_CreateFile(p, 16), o = 1, w = 1, k;
    nnfs_FileHandle h;
    if (!c && !(o = nnfs_OpenFile(&h, p, NNFS_OPEN_WRITE))) {
        nnfs_WriteOption opt = {1};
        w = nnfs_WriteFile(h, 0, "0123456789abcdef", 16, &opt);
        nnfs_CloseFile(h);
    }
    k = nnfs_CommitSaveData("sdmc");
    nnfs_DeleteFile(p);
    uint32_t k2 = nnfs_CommitSaveData("sdmc");
    snprintf(line, sizeof line, "test write: create 0x%x open 0x%x write 0x%x commit 0x%x, delete+commit 0x%x\n", c, o, w, k, k2);
    luaL_addstring(&b, line);
    luaL_pushresult(&b);
    return 1;
}
uint32_t nnfs_DeleteDirectoryRecursively(const char *path) __asm__("_ZN2nn2fs26DeleteDirectoryRecursivelyEPKc");
/* core.remote_rm(path) -> nn::Result: deletes a folder (and all in it) or a file in the game's save data, and
 * commits. Only "sdmc:/<name>" paths without "..". */
int remote_rm(lua_State *L) {
    const char *p = luaL_checkstring(L, 1);
    bool sd = strncmp(p, "sd:/", 4) == 0 && p[4];
    if (!sd && (strncmp(p, "sdmc:/", 6) != 0 || !p[6])) return luaL_error(L, "only sdmc:/<path> or sd:/<path>");
    if (strstr(p, "..")) return luaL_error(L, "no ..");
    uint32_t r = nnfs_DeleteDirectoryRecursively(p);
    if (r) r = nnfs_DeleteFile(p);
    uint32_t c = sd ? 0 : nnfs_CommitSaveData("sdmc");
    lua_pushinteger(L, r ? r : c);
    return 1;
}

/* ------------------------------------------------------------ the SD card ("sd:/")
 * The game itself has no SD card access; the remote build's exefs/main.npdm grants it (tools/npdm_sd.py), so
 * the relay can put mods and new builds on the card. */
uint32_t nnfs_MountSdCard(const char *mount) __asm__("_ZN2nn2fs11MountSdCardEPKc");
uint32_t nnfs_RenameFile(const char *from, const char *to) __asm__("_ZN2nn2fs10RenameFileEPKcS2_");
uint32_t nnfs_RenameDirectory(const char *from, const char *to) __asm__("_ZN2nn2fs15RenameDirectoryEPKcS2_");
static uint32_t sd_mount(void) {
    static uint32_t r = 1;
    if (r && !nnfs_IsMounted("sd")) r = nnfs_MountSdCard("sd");
    else r = 0;
    return r;
}
/* core.remote_sd() -> nn::Result of mounting the card as "sd" (0 = ok) */
int remote_sd(lua_State *L) {
    lua_pushinteger(L, sd_mount());
    return 1;
}
static int b64v(int c) {
    return c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52
         : c == '+' ? 62 : c == '/' ? 63 : -1;
}
static bool sd_path(const char *p) { return strncmp(p, "sd:/", 4) == 0 && p[4] && !strstr(p, ".."); }
/* parent folders of a file, made one by one ("already exists" is fine) */
static void make_parents(const char *p) {
    char tmp[768];
    snprintf(tmp, sizeof tmp, "%s", p);
    for (char *c = tmp + 4; (c = strchr(c, '/')); c++) {
        *c = 0;
        nnfs_CreateDirectory(tmp);
        *c = '/';
    }
}
/* core.remote_put(path, base64, offset) -> nn::Result, size written: writes the decoded bytes at offset into
 * sd:/<path>; offset 0 starts the file anew (folders made as needed). Chunks are sent in order. */
int remote_put(lua_State *L) {
    size_t n;
    const char *p = luaL_checkstring(L, 1), *s = luaL_checklstring(L, 2, &n);
    int64_t off = luaL_optinteger(L, 3, 0);
    if (!sd_path(p)) return luaL_error(L, "only sd:/<path>");
    uint32_t r = sd_mount();
    if (r) return lua_pushinteger(L, r), 1;
    u8 *buf = malloc(n / 4 * 3 + 3);
    if (!buf) return luaL_error(L, "out of memory");
    size_t k = 0;
    u32 acc = 0;
    int bits = 0;
    for (size_t i = 0; i < n; i++) {
        int v = b64v((u8)s[i]);
        if (v < 0) continue;
        acc = acc << 6 | (u32)v, bits += 6;
        if (bits >= 8) bits -= 8, buf[k++] = (u8)(acc >> bits);
    }
    if (off == 0) {
        make_parents(p);
        nnfs_DeleteFile(p);
        r = nnfs_CreateFile(p, 0);
    }
    nnfs_FileHandle h;
    if (!r && !(r = nnfs_OpenFile(&h, p, NNFS_OPEN_WRITE | 4))) {  // 4 = append: the file grows
        nnfs_WriteOption opt = {1};
        r = nnfs_WriteFile(h, off, buf, k, &opt);
        nnfs_CloseFile(h);
    }
    free(buf);
    lua_pushinteger(L, r);
    lua_pushinteger(L, (lua_Integer)k);
    return 2;
}
/* core.remote_get(path, offset, size) -> base64 of up to size bytes (default 2 MB) at offset, file size; or
 * nil, nn::Result. Any mounted path (sd:/, sdmc:/, rom:/). */
int remote_get(lua_State *L) {
    const char *p = luaL_checkstring(L, 1);
    int64_t off = luaL_optinteger(L, 2, 0), want = luaL_optinteger(L, 3, 2 << 20);
    if (strncmp(p, "sd:/", 4) == 0) sd_mount();
    nnfs_FileHandle h;
    int64_t size = 0;
    uint32_t r = nnfs_OpenFile(&h, p, NNFS_OPEN_READ);
    if (r) return lua_pushnil(L), lua_pushinteger(L, r), 2;
    nnfs_GetFileSize(&size, h);
    if (want > (2 << 20)) want = 2 << 20;
    if (off > size) off = size;
    if (want > size - off) want = size - off;
    u8 *buf = malloc((size_t)want + 1);
    char *out = buf ? malloc(((size_t)want + 2) / 3 * 4 + 1) : NULL;
    size_t got = 0;
    if (out) r = nnfs_ReadFile(&got, h, off, buf, (size_t)want);
    nnfs_CloseFile(h);
    if (!out) return free(buf), luaL_error(L, "out of memory");
    static const char k64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t k = 0;
    for (size_t i = 0; i < got; i += 3) {
        u32 v = (u32)buf[i] << 16 | (i + 1 < got ? (u32)buf[i + 1] << 8 : 0) | (i + 2 < got ? buf[i + 2] : 0);
        out[k++] = k64[v >> 18 & 63], out[k++] = k64[v >> 12 & 63];
        out[k++] = i + 1 < got ? k64[v >> 6 & 63] : '=';
        out[k++] = i + 2 < got ? k64[v & 63] : '=';
    }
    lua_pushlstring(L, out, k);
    lua_pushinteger(L, (lua_Integer)size);
    free(buf), free(out);
    return 2;
}
/* core.remote_mv(from, to) -> nn::Result: renames a file on the card (a finished upload into place) */
int remote_mv(lua_State *L) {
    const char *a = luaL_checkstring(L, 1), *b = luaL_checkstring(L, 2);
    if (!sd_path(a) || !sd_path(b)) return luaL_error(L, "only sd:/<path>");
    sd_mount();
    make_parents(b);
    nnfs_DeleteFile(b);
    uint32_t r = nnfs_RenameFile(a, b);
    if (r) r = nnfs_RenameDirectory(a, b);  // a folder (a mod moved in or out of romfs/mods)
    lua_pushinteger(L, r);
    return 1;
}
