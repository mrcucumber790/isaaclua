/* Mod resources: like on PC, files in rom:/mods/<mod>/resources/ (and resources-dlc3/, which wins) replace or
 * add to the game's own.
 *
 * Every file the game opens goes through KAGE::Filesys::IContentManager::GetMountedFilePath(path),
 * which maps a game path ("gfx/foo.pcx") to the real one through the prebuilt romfs index
 * (kage_mount_points.dat) and returns it new[]-allocated, or NULL. We hook it: mod files are found
 * in our own index of the mods' resources/ folders (built at boot), and absolute paths into a mod
 * folder ("rom:/mods/eid/main.lua/../resources/font/x.fnt", as EID builds them) are normalised and
 * used as they are. Everything else goes to the game's lookup unchanged. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "isaaclua.h"
#include "nn.h"

void *_Znam(size_t);

typedef struct {
    char *rel;  // lowercase, '/' separated, relative to resources/
    char *real; // full path with the file's own case
    bool off;   // its mod was switched off in the Mods menu (left out of the hash)
} ModFile;

static ModFile *s_files;
static size_t s_count, s_cap;
static u32 *s_hash;  // open addressing, 1 + index into s_files
static size_t s_hash_cap;

static u32 djb2(const char *s) {
    u32 h = 5381;
    while (*s) h = h * 33 + (u8)*s++;
    return h;
}

static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

/* backslashes -> '/', empty and "." segments dropped, "dir/.." removed. out must hold strlen(in) + 1. */
static void normalise(const char *in, char *out) {
    enum { kMaxSegs = 64 };
    const char *seg[kMaxSegs];
    size_t len[kMaxSegs];
    int ns = 0;
    for (const char *p = in; *p;) {
        const char *s = p;
        while (*p && *p != '/' && *p != '\\') p++;
        size_t l = (size_t)(p - s);
        if (*p) p++;
        if (l == 0 || (l == 1 && s[0] == '.')) continue;
        if (l == 2 && s[0] == '.' && s[1] == '.' && ns > 0 && seg[ns - 1][len[ns - 1] - 1] != ':' &&
            !(len[ns - 1] == 2 && seg[ns - 1][0] == '.' && seg[ns - 1][1] == '.')) {
            ns--;
            continue;
        }
        if (ns == kMaxSegs) break;
        seg[ns] = s, len[ns] = l, ns++;
    }
    size_t n = 0;
    if (in[0] == '/' || in[0] == '\\') out[n++] = '/';
    for (int i = 0; i < ns; i++) {
        if (i) out[n++] = '/';
        memcpy(out + n, seg[i], len[i]);
        n += len[i];
    }
    out[n] = 0;
}

/* nn::fs aborts the whole game on a path it cannot parse (no mount name, like Lua's "./x.so", or an
 * unmounted one such as "sdmc:/"), so every path we open goes through this: normalised into out
 * (cap bytes) and only under rom:/, else NULL. */
const char *modfs_safe_path(const char *in, char *out, size_t cap) {
    size_t n = strlen(in);
    if (n == 0 || n + 1 > cap) return NULL;
    normalise(in, out);
    if (strncmp(out, "rom:/", 5) != 0 || strlen(out) >= 0x300) return NULL;
    for (const char *c = out + 5; *c; c++)
        if (*c == ':' || (unsigned char)*c < 0x20) return NULL;
    return out;
}

static char *dupstr(const char *s) {
    size_t n = strlen(s) + 1;
    char *r = (char *)malloc(n);
    if (r) memcpy(r, s, n);
    return r;
}

static void add_file(const char *rel, const char *real) {
    if (s_count == s_cap) {
        s_cap = s_cap ? s_cap * 2 : 64;
        s_files = (ModFile *)realloc(s_files, s_cap * sizeof *s_files);
    }
    ModFile *f = &s_files[s_count++];
    f->rel = dupstr(rel);
    for (char *c = f->rel; *c; c++) *c = lower(*c);
    f->real = dupstr(real);
    f->off = false;
}

static void build_hash(void) {
    free(s_hash);
    s_hash_cap = 16;
    while (s_hash_cap < s_count * 2) s_hash_cap *= 2;
    s_hash = (u32 *)calloc(s_hash_cap, sizeof *s_hash);
    for (size_t i = 0; i < s_count; i++) {
        if (s_files[i].off) continue;
        size_t h = djb2(s_files[i].rel) & (s_hash_cap - 1);
        while (s_hash[h]) {
            if (strcmp(s_files[s_hash[h] - 1].rel, s_files[i].rel) == 0) break;  // first mod wins
            h = (h + 1) & (s_hash_cap - 1);
        }
        if (!s_hash[h]) s_hash[h] = (u32)i + 1;
    }
}

static const ModFile *find(const char *rel_lower) {
    if (!s_hash) return NULL;
    size_t h = djb2(rel_lower) & (s_hash_cap - 1);
    while (s_hash[h]) {
        const ModFile *f = &s_files[s_hash[h] - 1];
        if (strcmp(f->rel, rel_lower) == 0) return f;
        h = (h + 1) & (s_hash_cap - 1);
    }
    return NULL;
}

/* depth-first walk of dir (full path, no trailing '/'); rel = path below resources/ so far */
static void scan(const char *dir, const char *rel, int depth) {
    char safe[1024];
    if (depth > 12 || !modfs_safe_path(dir, safe, sizeof safe)) return;
    nnfs_DirectoryHandle d;
    if (nnfs_OpenDirectory(&d, safe, NNFS_DIR_DIRECTORIES | NNFS_DIR_FILES) != 0) return;
    enum { kBatch = 32 };
    nnfs_DirectoryEntry *ents = (nnfs_DirectoryEntry *)malloc(kBatch * sizeof *ents);
    size_t total = 0, cap = 0;
    nnfs_DirectoryEntry *all = NULL;
    s64 n = 0;
    while (ents && nnfs_ReadDirectory(&n, ents, d, kBatch) == 0 && n > 0) {  // read all before recursing
        if (total + (size_t)n > cap) {
            cap = (total + (size_t)n) * 2;
            all = (nnfs_DirectoryEntry *)realloc(all, cap * sizeof *all);
        }
        memcpy(all + total, ents, (size_t)n * sizeof *ents);
        total += (size_t)n;
    }
    nnfs_CloseDirectory(d);
    free(ents);
    char path[800], sub[600];
    for (size_t i = 0; i < total; i++) {
        snprintf(path, sizeof path, "%s/%s", dir, all[i].name);
        snprintf(sub, sizeof sub, "%s%s%s", rel, *rel ? "/" : "", all[i].name);
        if (all[i].type == 0) scan(path, sub, depth + 1);
        else add_file(sub, path);
    }
    free(all);
}

static bool same_ci(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if ((*a | (*a >= 'A' && *a <= 'Z' ? 0x20 : 0)) != *b) return false;
    return !*a && !*b;
}

/* dir = mod_dir + the entry matching `lower` in any case ("Resources/": PC is case-insensitive) */
static void sub_dir(const char *mod_dir, const char *lower, char *dir, size_t size) {
    snprintf(dir, size, "%s%s", mod_dir, lower);
    char safe[1024];
    if (!modfs_safe_path(mod_dir, safe, sizeof safe)) return;
    nnfs_DirectoryHandle d;
    if (nnfs_OpenDirectory(&d, safe, NNFS_DIR_DIRECTORIES) != 0) return;
    nnfs_DirectoryEntry *e = (nnfs_DirectoryEntry *)malloc(sizeof *e);
    s64 n = 0;
    while (e && nnfs_ReadDirectory(&n, e, d, 1) == 0 && n > 0)
        if (same_ci(e->name, lower)) {
            snprintf(dir, size, "%s%s", mod_dir, e->name);
            break;
        }
    nnfs_CloseDirectory(d);
    free(e);
}

void modfs_add_mod(const char *mod_dir) {  // "rom:/mods/<name>/"
    char dir[700];
    size_t before = s_count;
    /* as on PC Repentance: resources-dlc3/ first (the first file of a path wins), then resources/ */
    sub_dir(mod_dir, "resources-dlc3", dir, sizeof dir);
    scan(dir, "", 0);
    sub_dir(mod_dir, "resources", dir, sizeof dir);
    scan(dir, "", 0);
    if (s_count != before) build_hash();
}

/* the Mods menu switched a mod while the game runs: its files stop (or start) replacing the game's for what
 * is loaded from now on (the game keeps what it already read, as sprites in its caches). A mod first switched
 * on now is scanned now; its files come after the mods scanned at the start. */
void modfs_set_mod_off(const char *mod_dir, bool off) {
    size_t n = strlen(mod_dir), found = 0;
    for (size_t i = 0; i < s_count; i++)
        if (strncmp(s_files[i].real, mod_dir, n) == 0) s_files[i].off = off, found++;
    if (!found && !off) modfs_add_mod(mod_dir);
    else build_hash();
}

/* forget every mod's files (the switches became known after the first scan: modfs_add_mod again for the mods on) */
void modfs_reset(void) {
    for (size_t i = 0; i < s_count; i++) free(s_files[i].rel), free(s_files[i].real);
    s_count = 0;
    free(s_hash);
    s_hash = NULL, s_hash_cap = 0;
}
size_t modfs_count(void) { return s_count; }

static bool file_exists(const char *path) {
    char safe[1024];
    nnfs_FileHandle f;
    if (!modfs_safe_path(path, safe, sizeof safe) || nnfs_OpenFile(&f, safe, NNFS_OPEN_READ) != 0) return false;
    nnfs_CloseFile(f);
    return true;
}

static char *game_dup(const char *s) {  // new[] copy: the game frees the result with delete[]
    size_t n = strlen(s) + 1;
    char *r = (char *)_Znam(n);
    memcpy(r, s, n);
    return r;
}

static bool eq_ci(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (lower(*a) != lower(*b)) return false;
    return !*a && !*b;
}

/* A path into a mod folder in any case: the game asks for "content/gfx/CharacterMenu.anm2" (PC's file
 * system ignores case), the mod ships "charactermenu.anm2". Each segment after rom:/mods/ is looked up
 * in its folder ignoring case; results (found or not) are cached by the lowercased path. */
#define kCiCache 512
static struct { char *key; char *real; bool found; } s_ci[kCiCache];  // key = lowercased path
static bool resolve_ci(char *buf, size_t cap) {
    char low[1024];
    size_t n = strlen(buf);
    if (n >= sizeof low) return false;
    for (size_t i = 0; i <= n; i++) low[i] = lower(buf[i]);
    size_t slot = djb2(low) & (kCiCache - 1);
    if (s_ci[slot].key && strcmp(s_ci[slot].key, low) == 0) {
        if (!s_ci[slot].found) return false;
        if (strlen(s_ci[slot].real) + 1 > cap) return false;
        strcpy(buf, s_ci[slot].real);
        return true;
    }
    char out[1024];
    size_t o = 10;
    memcpy(out, buf, 10);  // "rom:/mods/"
    out[o] = 0;
    bool ok = true;
    const char *p = buf + 10;
    nnfs_DirectoryEntry *e = (nnfs_DirectoryEntry *)malloc(sizeof *e);
    while (ok && *p && e) {
        const char *q = strchr(p, '/');
        size_t l = q ? (size_t)(q - p) : strlen(p);
        char seg[300];
        if (l >= sizeof seg) { ok = false; break; }
        memcpy(seg, p, l);
        seg[l] = 0;
        char safe[1024], dir[1024];
        snprintf(dir, sizeof dir, "%.*s", (int)(o > 10 ? o - 1 : o), out);  // parent, no trailing '/'
        if (o == 10) snprintf(dir, sizeof dir, "rom:/mods/");
        bool hit = false;
        nnfs_DirectoryHandle d;
        if (modfs_safe_path(dir, safe, sizeof safe) && nnfs_OpenDirectory(&d, safe, NNFS_DIR_DIRECTORIES | NNFS_DIR_FILES) == 0) {
            s64 k = 0;
            while (nnfs_ReadDirectory(&k, e, d, 1) == 0 && k > 0)
                if (eq_ci(e->name, seg)) {
                    hit = true;
                    break;
                }
            nnfs_CloseDirectory(d);
        }
        if (!hit || o + strlen(e->name) + 2 >= sizeof out) { ok = false; break; }
        o += (size_t)snprintf(out + o, sizeof out - o, "%s%s", e->name, q ? "/" : "");
        p = q ? q + 1 : p + l;
    }
    free(e);
    ok = ok && file_exists(out) && strlen(out) + 1 <= cap;
    free(s_ci[slot].key);
    free(s_ci[slot].real);
    s_ci[slot].key = dupstr(low), s_ci[slot].found = ok, s_ci[slot].real = ok ? dupstr(out) : NULL;
    if (ok && !s_ci[slot].real) free(s_ci[slot].key), s_ci[slot].key = NULL;
    if (ok) strcpy(buf, out);
    return ok;
}

/* the real path of a mod file for the game path p, or NULL (then the game looks it up itself) */
const char *modfs_resolve(const char *p, char *buf, size_t cap) {
    size_t n = strlen(p);
    if (n == 0 || n + 1 > cap) return NULL;
    normalise(p, buf);
    if (strncmp(buf, "rom:/mods/", 10) == 0)
        return file_exists(buf) || resolve_ci(buf, cap) ? buf : NULL;
    if (!s_count) return NULL;
    for (char *c = buf; *c; c++) *c = lower(*c);
    const char *rel = buf;
    if (strncmp(rel, "resources/", 10) == 0) rel += 10;
    const ModFile *f = find(rel);
    return f ? f->real : NULL;
}

typedef char *(*GetMountedFn)(void *cm, const char *path);
static GetMountedFn s_orig_mounted;

#ifdef ISAACLUA_DEBUG
enum { kMiss = 32 };
static char s_miss[kMiss][256];
static u8 s_miss_ready[kMiss];
static unsigned s_miss_w, s_miss_r;
const char *modfs_take_missing(void) {  // main thread: the next missing path, or NULL
    unsigned i = s_miss_r % kMiss;
    if (!__atomic_load_n(&s_miss_ready[i], __ATOMIC_ACQUIRE)) return NULL;
    static char out[256];
    memcpy(out, s_miss[i], sizeof out);
    __atomic_store_n(&s_miss_ready[i], 0, __ATOMIC_RELAXED);
    s_miss_r++;
    return out;
}
#endif
static char *hk_get_mounted(void *cm, const char *path) {
    if (path) {
        char buf[1024];
        const char *r = modfs_resolve(path, buf, sizeof buf);
        if (r) return game_dup(r);
    }
#ifdef ISAACLUA_DEBUG
    /* the debug build: a file neither a mod nor the game has (a sprite that stays empty, a sound that is
     * silent) goes to errors.log and autotest.log. Files can be opened off the main thread: the path waits
     * here until modfs_take_missing (main thread). */
    char *r = s_orig_mounted(cm, path);
    if (!r && path) {
        unsigned i = __atomic_fetch_add(&s_miss_w, 1, __ATOMIC_RELAXED) % kMiss;
        snprintf(s_miss[i], sizeof s_miss[i], "%s", path);
        __atomic_store_n(&s_miss_ready[i], 1, __ATOMIC_RELEASE);
    }
    return r;
#else
    return s_orig_mounted(cm, path);
#endif
}

void *modfs_hook(void *orig) {  // returns our hook; orig = what the GOT slot held
    s_orig_mounted = (GetMountedFn)orig;
    return (void *)hk_get_mounted;
}
