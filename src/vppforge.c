/* ============================================================================
 * vppforge.c · native desktop shell for VPP Forge
 *
 * What it does:
 *   1. Serves the embedded VPP Forge app (app_html.h) on 127.0.0.1:<random>.
 *   2. Injects a per-launch auth token into the page (<!--VPP_BRIDGE-->).
 *   3. Exposes a tiny file bridge: the file that was double-clicked, native
 *      Open/Save-As dialogs, and in-place saves (streamed, atomic replace).
 *   4. Launches Chrome or Edge in --app mode (own window, no browser chrome)
 *      with a private profile, waits for the window to close, then exits.
 *
 * Windows build (MinGW-w64):
 *   x86_64-w64-mingw32-windres resources/vppforge.rc -O coff -o res.o
 *   x86_64-w64-mingw32-gcc -O2 -municode -mwindows src/vppforge.c res.o \
 *       -o vppforge.exe -lws2_32 -lcomdlg32 -ladvapi32 -lshell32 -luser32 \
 *       -static -s
 *
 * POSIX test build (serves + env-stub dialogs, used for automated testing):
 *   gcc -O2 -Wall -Wextra -o vppforged src/vppforge.c
 * ==========================================================================*/

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <wincrypt.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <objbase.h>
typedef SOCKET sock_t;
#define CLOSESOCK closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
typedef int sock_t;
#define INVALID_SOCKET (-1)
#define CLOSESOCK close
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app_html.h" /* unsigned char APP_HTML[]; unsigned int APP_HTML_LEN; */

#define MAX_PATHS 64
#define HDR_MAX 16384
#define BRIDGE_MARK "<!--VPP_BRIDGE-->"
#define VPP_VERSION "1.1.0"
#define WIDEN2(x) L ## x
#define WIDEN(x) WIDEN2(x)
#define SETTINGS_MAX 32768

static int strncasecmp_portable(const char *a, const char *b, size_t n);
static const void *memmem_portable(const void *hay, size_t hn, const void *nee, size_t nn);

static char g_token[33];
static char *g_paths[MAX_PATHS]; /* UTF-8 */
static int g_npaths = 0;
static char *g_page = NULL; /* html with injected bridge config */
static size_t g_page_len = 0;
static volatile int g_quit = 0;
static char g_recent_path[4096] = "";
static char g_settings_path[4096] = "";
#ifdef _WIN32
static HANDLE g_browser_proc = NULL;
#endif

/* ---------------------------------------------------------------- utils -- */

static void rand_token(char *out, size_t hexlen) {
    static const char hexd[] = "0123456789abcdef";
    unsigned char raw[16];
    size_t i;
#ifdef _WIN32
    HCRYPTPROV h;
    if (CryptAcquireContextW(&h, NULL, NULL, PROV_RSA_FULL,
                             CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
        CryptGenRandom(h, (DWORD)sizeof raw, raw);
        CryptReleaseContext(h, 0);
    } else {
        for (i = 0; i < sizeof raw; i++)
            raw[i] = (unsigned char)(rand() ^ (GetTickCount() >> (i & 7)));
    }
#else
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) { if (fread(raw, 1, sizeof raw, f) != sizeof raw) { /* fallthrough */ } fclose(f); }
    else for (i = 0; i < sizeof raw; i++) raw[i] = (unsigned char)rand();
#endif
    for (i = 0; i < hexlen && i < sizeof raw * 2; i++)
        out[i] = hexd[(raw[i / 2] >> ((i & 1) ? 0 : 4)) & 15];
    out[i] = 0;
}

static const char *base_name(const char *p) {
    const char *a = strrchr(p, '/');
    const char *b = strrchr(p, '\\');
    const char *m = a > b ? a : b;
    return m ? m + 1 : p;
}

static char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *d = (char *)malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

static int register_path(const char *utf8) {
    if (g_npaths >= MAX_PATHS) return -1;
    g_paths[g_npaths] = xstrdup(utf8);
    return g_paths[g_npaths] ? g_npaths++ : -1;
}

/* JSON-escape into dst (quotes + backslash + control chars) */
static void json_escape(char *dst, size_t cap, const char *src) {
    size_t o = 0;
    for (; *src && o + 6 < cap; src++) {
        unsigned char c = (unsigned char)*src;
        if (c == '"' || c == '\\') { dst[o++] = '\\'; dst[o++] = (char)c; }
        else if (c < 0x20) { o += (size_t)snprintf(dst + o, cap - o, "\\u%04x", c); }
        else dst[o++] = (char)c;
    }
    dst[o] = 0;
}

/* extract query parameter (percent-decoded); returns 1 on success */
static int qget(const char *target, const char *key, char *out, size_t cap) {
    const char *q = strchr(target, '?');
    size_t klen = strlen(key);
    if (!q) return 0;
    q++;
    while (*q) {
        const char *eq = strchr(q, '=');
        const char *amp = strchr(q, '&');
        const char *end = amp ? amp : q + strlen(q);
        if (eq && eq < end && (size_t)(eq - q) == klen && !strncmp(q, key, klen)) {
            size_t o = 0;
            const char *v = eq + 1;
            while (v < end && o + 1 < cap) {
                if (*v == '%' && v + 2 < end) {
                    char hx[3] = { v[1], v[2], 0 };
                    out[o++] = (char)strtol(hx, NULL, 16);
                    v += 3;
                } else if (*v == '+') { out[o++] = ' '; v++; }
                else out[o++] = *v++;
            }
            out[o] = 0;
            return 1;
        }
        if (!amp) break;
        q = amp + 1;
    }
    return 0;
}

static int token_ok(const char *target) {
    char t[64];
    return qget(target, "t", t, sizeof t) && !strcmp(t, g_token);
}

/* forward decl: plat_fopen is defined in the platform layer below */
static FILE *plat_fopen(const char *p, const char *m);

/* ---------------------------------------------------------- recents ------ */
#define RECENT_MAX 10
static int recents_load(char list[RECENT_MAX][2048]) {
    FILE *f; int n = 0;
    if (!g_recent_path[0]) return 0;
    f = plat_fopen(g_recent_path, "rb");
    if (!f) return 0;
    while (n < RECENT_MAX && fgets(list[n], 2048, f)) {
        size_t l = strlen(list[n]);
        while (l && (list[n][l-1] == '\n' || list[n][l-1] == '\r')) list[n][--l] = 0;
        if (l) n++;
    }
    fclose(f);
    return n;
}
static void add_recent(const char *path) {
    char list[RECENT_MAX][2048];
    int n, i, out;
    FILE *f;
    if (!g_recent_path[0] || strlen(path) >= 2048) return;
    n = recents_load(list);
    f = plat_fopen(g_recent_path, "wb");
    if (!f) return;
    fprintf(f, "%s\n", path);
    for (i = 0, out = 1; i < n && out < RECENT_MAX; i++) {
        int same;
#ifdef _WIN32
        same = !_stricmp(list[i], path);
#else
        same = !strcmp(list[i], path);
#endif
        if (!same) { fprintf(f, "%s\n", list[i]); out++; }
    }
    fclose(f);
}
static void recents_remove(const char *path) {
    char list[RECENT_MAX][2048];
    int n = recents_load(list), i;
    FILE *f = g_recent_path[0] ? plat_fopen(g_recent_path, "wb") : NULL;
    if (!f) return;
    for (i = 0; i < n; i++)
        if (strcmp(list[i], path)) fprintf(f, "%s\n", list[i]);
    fclose(f);
}


/* ------------------------------------------------------ platform layer --- */

#ifdef _WIN32

static wchar_t *utf8_to_wide(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (w) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}
static char *wide_to_utf8(const wchar_t *w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *s = (char *)malloc((size_t)n);
    if (s) WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
    return s;
}

static FILE *plat_fopen(const char *utf8, const char *mode) {
    wchar_t wm[8];
    wchar_t *wp = utf8_to_wide(utf8);
    FILE *f;
    size_t i;
    for (i = 0; mode[i] && i < 7; i++) wm[i] = (wchar_t)mode[i];
    wm[i] = 0;
    f = wp ? _wfopen(wp, wm) : NULL;
    free(wp);
    return f;
}

static int plat_replace(const char *tmp_utf8, const char *final_utf8) {
    wchar_t *a = utf8_to_wide(tmp_utf8), *b = utf8_to_wide(final_utf8);
    BOOL ok = (a && b) ? MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) : FALSE;
    free(a); free(b);
    return ok ? 0 : -1;
}

static int plat_dialog_open(char *out_utf8, size_t cap) {
    wchar_t buf[MAX_PATH * 2] = L"";
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFilter = L"Red Faction archives (*.vpp)\0*.vpp\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH * 2;
    ofn.lpstrTitle = L"Open VPP archive";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return 0;
    { char *u = wide_to_utf8(buf);
      if (!u) return 0;
      snprintf(out_utf8, cap, "%s", u);
      free(u); }
    return 1;
}

static int plat_dialog_save(char *out_utf8, size_t cap, const char *suggest_utf8) {
    wchar_t buf[MAX_PATH * 2] = L"";
    OPENFILENAMEW ofn;
    if (suggest_utf8 && *suggest_utf8) {
        wchar_t *ws = utf8_to_wide(suggest_utf8);
        if (ws) { wcsncpy(buf, ws, MAX_PATH * 2 - 1); free(ws); }
    }
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFilter = L"Red Faction archives (*.vpp)\0*.vpp\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH * 2;
    ofn.lpstrTitle = L"Save VPP archive";
    ofn.lpstrDefExt = L"vpp";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) return 0;
    { char *u = wide_to_utf8(buf);
      if (!u) return 0;
      snprintf(out_utf8, cap, "%s", u);
      free(u); }
    return 1;
}

/* ------------------------- self-install (single exe, no bat files) ------ */

static void reg_set_str(HKEY root, const wchar_t *path, const wchar_t *name, const wchar_t *val) {
    HKEY k;
    if (RegCreateKeyExW(root, path, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(k, name, 0, REG_SZ, (const BYTE *)val,
                       (DWORD)((wcslen(val) + 1) * sizeof(wchar_t)));
        RegCloseKey(k);
    }
}
static void reg_set_dw(HKEY root, const wchar_t *path, const wchar_t *name, DWORD v) {
    HKEY k;
    if (RegCreateKeyExW(root, path, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE *)&v, sizeof v);
        RegCloseKey(k);
    }
}
static void install_dir(wchar_t *out, size_t cap) {
    wchar_t la[MAX_PATH] = L"";
    GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH);
    _snwprintf(out, cap - 1, L"%s\\Programs\\VPPForge", la);
    out[cap - 1] = 0;
}
static int is_installed(void) {
    wchar_t val[MAX_PATH * 2];
    DWORD sz = sizeof val;
    if (RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Classes\\VPPForge.Archive\\shell\\open\\command",
        NULL, RRF_RT_REG_SZ, NULL, val, &sz) != ERROR_SUCCESS) return 0;
    /* value looks like "C:\...\vppforge.exe" "%1" · check the exe exists */
    if (val[0] == L'"') {
        wchar_t *e = wcschr(val + 1, L'"');
        if (e) { *e = 0; return GetFileAttributesW(val + 1) != INVALID_FILE_ATTRIBUTES; }
    }
    return 0;
}
static void create_start_menu_shortcut(const wchar_t *exe) {
    wchar_t progs[MAX_PATH], lnk[MAX_PATH * 2];
    IShellLinkW *sl = NULL;
    IPersistFile *pf = NULL;
    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED))) return;
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, progs))) {
        _snwprintf(lnk, MAX_PATH * 2 - 1, L"%s\\VPP Forge.lnk", progs);
        lnk[MAX_PATH * 2 - 1] = 0;
        if (SUCCEEDED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                                       &IID_IShellLinkW, (void **)&sl))) {
            IShellLinkW_SetPath(sl, exe);
            IShellLinkW_SetIconLocation(sl, exe, 0);
            IShellLinkW_SetDescription(sl, L"Red Faction VPP archive workbench");
            if (SUCCEEDED(IShellLinkW_QueryInterface(sl, &IID_IPersistFile, (void **)&pf))) {
                IPersistFile_Save(pf, lnk, TRUE);
                IPersistFile_Release(pf);
            }
            IShellLinkW_Release(sl);
        }
    }
    CoUninitialize();
}
static void write_associations(const wchar_t *exe) {
    wchar_t buf[MAX_PATH * 2 + 16];
    reg_set_str(HKEY_CURRENT_USER, L"Software\\Classes\\.vpp", NULL, L"VPPForge.Archive");
    reg_set_str(HKEY_CURRENT_USER, L"Software\\Classes\\VPPForge.Archive", NULL, L"Red Faction Archive");
    _snwprintf(buf, MAX_PATH * 2 + 15, L"\"%s\",0", exe); buf[MAX_PATH * 2 + 15] = 0;
    reg_set_str(HKEY_CURRENT_USER, L"Software\\Classes\\VPPForge.Archive\\DefaultIcon", NULL, buf);
    reg_set_str(HKEY_CURRENT_USER, L"Software\\Classes\\VPPForge.Archive\\shell\\open", NULL, L"Open with VPP Forge");
    _snwprintf(buf, MAX_PATH * 2 + 15, L"\"%s\" \"%%1\"", exe); buf[MAX_PATH * 2 + 15] = 0;
    reg_set_str(HKEY_CURRENT_USER, L"Software\\Classes\\VPPForge.Archive\\shell\\open\\command", NULL, buf);
}
static void write_uninstall_entry(const wchar_t *exe, const wchar_t *dir) {
    const wchar_t *k = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\VPPForge";
    wchar_t buf[MAX_PATH * 2 + 16];
    reg_set_str(HKEY_CURRENT_USER, k, L"DisplayName", L"VPP Forge");
    reg_set_str(HKEY_CURRENT_USER, k, L"DisplayVersion", WIDEN(VPP_VERSION));
    reg_set_str(HKEY_CURRENT_USER, k, L"Publisher", L"VPP Forge");
    reg_set_str(HKEY_CURRENT_USER, k, L"DisplayIcon", exe);
    reg_set_str(HKEY_CURRENT_USER, k, L"InstallLocation", dir);
    _snwprintf(buf, MAX_PATH * 2 + 15, L"\"%s\" /uninstall", exe); buf[MAX_PATH * 2 + 15] = 0;
    reg_set_str(HKEY_CURRENT_USER, k, L"UninstallString", buf);
    reg_set_dw(HKEY_CURRENT_USER, k, L"NoModify", 1);
    reg_set_dw(HKEY_CURRENT_USER, k, L"NoRepair", 1);
    reg_set_dw(HKEY_CURRENT_USER, k, L"EstimatedSize", 320);
}
static int do_install(void) {
    wchar_t self[MAX_PATH * 2], dir[MAX_PATH * 2], target[MAX_PATH * 2];
    GetModuleFileNameW(NULL, self, MAX_PATH * 2);
    install_dir(dir, MAX_PATH * 2);
    _snwprintf(target, MAX_PATH * 2 - 1, L"%s\\vppforge.exe", dir); target[MAX_PATH * 2 - 1] = 0;
    SHCreateDirectoryExW(NULL, dir, NULL);
    if (_wcsicmp(self, target) != 0) {
        if (!CopyFileW(self, target, FALSE)) {
            MessageBoxW(NULL, L"Could not copy VPP Forge into place.\nClose other VPP Forge windows and try again.",
                        L"VPP Forge Setup", MB_ICONERROR);
            return 0;
        }
    }
    write_associations(target);
    write_uninstall_entry(target, dir);
    create_start_menu_shortcut(target);
    reg_set_dw(HKEY_CURRENT_USER, L"Software\\VPPForge", L"Installed", 1);
    return 1;
}
static void do_uninstall(void) {
    wchar_t dir[MAX_PATH * 2], progs[MAX_PATH], lnk[MAX_PATH * 2], cmd[MAX_PATH * 5];
    wchar_t val[64]; DWORD sz = sizeof val;
    if (MessageBoxW(NULL, L"Remove VPP Forge and its .vpp file association?",
                    L"Uninstall VPP Forge", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    /* only release .vpp if it still points at us */
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Classes\\.vpp", NULL,
                     RRF_RT_REG_SZ, NULL, val, &sz) == ERROR_SUCCESS &&
        !wcscmp(val, L"VPPForge.Archive"))
        RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\.vpp");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\VPPForge.Archive");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\VPPForge");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\VPPForge");
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, progs))) {
        _snwprintf(lnk, MAX_PATH * 2 - 1, L"%s\\VPP Forge.lnk", progs); lnk[MAX_PATH * 2 - 1] = 0;
        DeleteFileW(lnk);
    }
    install_dir(dir, MAX_PATH * 2);
    _snwprintf(cmd, MAX_PATH * 5 - 1,
        L"cmd /c ping -n 3 127.0.0.1 >nul & rmdir /S /Q \"%s\"", dir);
    cmd[MAX_PATH * 5 - 1] = 0;
    { STARTUPINFOW si; PROCESS_INFORMATION pi;
      memset(&si, 0, sizeof si); si.cb = sizeof si;
      si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
      if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
          CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
      } }
    MessageBoxW(NULL, L"VPP Forge has been removed.", L"Uninstall VPP Forge", MB_ICONINFORMATION);
}
static int installed_version_matches(void) {
    wchar_t v[64];
    DWORD sz = sizeof v;
    if (RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\VPPForge",
        L"DisplayVersion", RRF_RT_REG_SZ, NULL, v, &sz) != ERROR_SUCCESS) return 0;
    return !wcscmp(v, WIDEN(VPP_VERSION));
}
static void maybe_offer_setup(void) {
    DWORD declined = 0, sz = sizeof declined;
    if (is_installed()) {
        /* an older version is installed: refresh it in place so the .vpp
           association, Start Menu entry and Apps listing point at this build */
        if (!installed_version_matches()) do_install();
        return;
    }
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\VPPForge", L"SetupDeclined",
                 RRF_RT_REG_DWORD, NULL, &declined, &sz);
    if (declined) return;
    if (MessageBoxW(NULL,
        L"Set up VPP Forge on this PC?\n\n"
        L"\x2022 Double-clicking .vpp files will open them here\n"
        L"\x2022 Adds a Start Menu entry\n"
        L"\x2022 Listed in Windows Apps for easy uninstall\n\n"
        L"Choose No to run without installing (it won't ask again).",
        L"VPP Forge Setup", MB_YESNO | MB_ICONQUESTION) == IDYES)
        do_install();
    else
        reg_set_dw(HKEY_CURRENT_USER, L"Software\\VPPForge", L"SetupDeclined", 1);
}

/* App Paths lookup for a Chromium browser; Edge ships with Win10/11 */
static int find_browser(wchar_t *out, DWORD cap) {
    static const wchar_t *names[2] = { L"chrome.exe", L"msedge.exe" };
    static const HKEY roots[2] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    wchar_t key[128];
    int n, r;
    for (n = 0; n < 2; n++)
        for (r = 0; r < 2; r++) {
            DWORD sz = cap * sizeof(wchar_t);
            _snwprintf(key, 127, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\%s", names[n]);
            key[127] = 0;
            if (RegGetValueW(roots[r], key, NULL, RRF_RT_REG_SZ, NULL, out, &sz) == ERROR_SUCCESS)
                return 1;
        }
    return 0;
}

static void plat_reveal(const char *utf8path) {
    wchar_t *wp = utf8_to_wide(utf8path);
    wchar_t args[MAX_PATH * 2 + 16];
    if (!wp) return;
    _snwprintf(args, MAX_PATH * 2 + 15, L"/select,\"%s\"", wp);
    args[MAX_PATH * 2 + 15] = 0;
    ShellExecuteW(NULL, L"open", L"explorer.exe", args, NULL, SW_SHOWNORMAL);
    free(wp);
}

static void remove_tree(const wchar_t *dir); /* fwd */
static void remove_tree(const wchar_t *dir) {
    wchar_t pat[MAX_PATH * 2], sub[MAX_PATH * 2];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    _snwprintf(pat, MAX_PATH * 2 - 1, L"%s\\*", dir); pat[MAX_PATH * 2 - 1] = 0;
    h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            _snwprintf(sub, MAX_PATH * 2 - 1, L"%s\\%s", dir, fd.cFileName); sub[MAX_PATH * 2 - 1] = 0;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) remove_tree(sub);
            else DeleteFileW(sub);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir);
}

static void plat_launch_and_wait(const char *url_utf8) {
    wchar_t browser[MAX_PATH * 2];
    wchar_t profile[MAX_PATH * 2];
    wchar_t tmp[MAX_PATH];
    wchar_t cmd[MAX_PATH * 5];
    wchar_t *wurl = utf8_to_wide(url_utf8);
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    if (!wurl) return;
    if (!find_browser(browser, MAX_PATH * 2)) {
        /* no Chromium found: fall back to the default browser as a tab */
        ShellExecuteW(NULL, L"open", wurl, NULL, NULL, SW_SHOWNORMAL);
        free(wurl);
        for (;;) Sleep(60000); /* keep serving until the user closes us */
    }
    GetTempPathW(MAX_PATH, tmp);
    _snwprintf(profile, MAX_PATH * 2 - 1, L"%sVPPForge-Profile-%lu", tmp,
               (unsigned long)GetCurrentProcessId());
    profile[MAX_PATH * 2 - 1] = 0;
    _snwprintf(cmd, MAX_PATH * 5 - 1,
               L"\"%s\" --app=%s --user-data-dir=\"%s\" --no-first-run "
               L"--no-default-browser-check --new-window --window-size=1360,900",
               browser, wurl, profile);
    cmd[MAX_PATH * 5 - 1] = 0;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        g_browser_proc = pi.hProcess;
        WaitForSingleObject(pi.hProcess, INFINITE);
        g_browser_proc = NULL;
        CloseHandle(pi.hProcess);
    } else {
        ShellExecuteW(NULL, L"open", wurl, NULL, NULL, SW_SHOWNORMAL);
        Sleep(600000);
    }
    remove_tree(profile);
    free(wurl);
}

#else /* ------------------------- POSIX test build ------------------------ */

static FILE *plat_fopen(const char *p, const char *m) { return fopen(p, m); }
static int plat_replace(const char *tmp, const char *fin) { return rename(tmp, fin); }
static int plat_dialog_open(char *out, size_t cap) {
    const char *p = getenv("VPPFORGE_TEST_OPEN");
    if (!p) return 0;
    snprintf(out, cap, "%s", p);
    return 1;
}
static int plat_dialog_save(char *out, size_t cap, const char *suggest) {
    const char *p = getenv("VPPFORGE_TEST_SAVE");
    (void)suggest;
    if (!p) return 0;
    snprintf(out, cap, "%s", p);
    return 1;
}
static void plat_reveal(const char *p) { (void)p; }

#endif

/* ------------------------------------------------------------ http core -- */

static int send_all(sock_t s, const char *buf, size_t len) {
    while (len) {
#ifdef _WIN32
        int n = send(s, buf, (int)len, 0);
#else
        ssize_t n = send(s, buf, len, 0);
#endif
        if (n <= 0) return -1;
        buf += n;
        len -= (size_t)n;
    }
    return 0;
}

static void resp(sock_t s, const char *status, const char *ctype,
                 const char *body, size_t blen) {
    char hdr[512];
    int hl = snprintf(hdr, sizeof hdr,
        "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
        status, ctype, (unsigned long)blen);
    send_all(s, hdr, (size_t)hl);
    if (blen) send_all(s, body, blen);
}
static void resp_json(sock_t s, const char *body) {
    resp(s, "200 OK", "application/json", body, strlen(body));
}
static void resp_err(sock_t s, const char *status) {
    resp(s, status, "text/plain", status, strlen(status));
}

static void serve_data(sock_t s, int id) {
    FILE *f = plat_fopen(g_paths[id], "rb");
    long sz;
    char hdr[256];
    char buf[65536];
    size_t n;
    if (!f) { resp_err(s, "404 Not Found"); return; }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    snprintf(hdr, sizeof hdr,
        "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
        "Content-Length: %ld\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n", sz);
    if (send_all(s, hdr, strlen(hdr))) { fclose(f); return; }
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        if (send_all(s, buf, n)) break;
    fclose(f);
}

static void handle_save(sock_t s, int id, unsigned long long clen,
                        const char *left, size_t leftn) {
    char tmp[4096];
    FILE *f;
    char buf[65536];
    unsigned long long got = 0;
    snprintf(tmp, sizeof tmp, "%s.vfnew", g_paths[id]);
    f = plat_fopen(tmp, "wb");
    if (!f) { resp_err(s, "500 Cannot Write"); return; }
    if (leftn) {
        size_t take = leftn > clen ? (size_t)clen : leftn;
        if (fwrite(left, 1, take, f) != take) { fclose(f); remove(tmp); resp_err(s, "500 Write Failed"); return; }
        got = take;
    }
    while (got < clen) {
        size_t want = sizeof buf;
        if (clen - got < want) want = (size_t)(clen - got);
#ifdef _WIN32
        { int n = recv(s, buf, (int)want, 0);
          if (n <= 0) { fclose(f); remove(tmp); return; }
          if (fwrite(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); remove(tmp); resp_err(s, "500 Write Failed"); return; }
          got += (unsigned long long)n; }
#else
        { ssize_t n = recv(s, buf, want, 0);
          if (n <= 0) { fclose(f); remove(tmp); return; }
          if (fwrite(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); remove(tmp); resp_err(s, "500 Write Failed"); return; }
          got += (unsigned long long)n; }
#endif
    }
    if (fflush(f) != 0) { fclose(f); remove(tmp); resp_err(s, "500 Write Failed"); return; }
    fclose(f);
    if (plat_replace(tmp, g_paths[id]) != 0) { remove(tmp); resp_err(s, "500 Replace Failed"); return; }
    resp_json(s, "{\"ok\":1}");
}

static void handle_settings_get(sock_t s) {
    char buf[SETTINGS_MAX];
    size_t n = 0;
    FILE *f = g_settings_path[0] ? plat_fopen(g_settings_path, "rb") : NULL;
    if (f) { n = fread(buf, 1, sizeof buf, f); fclose(f); }
    if (!n) { resp_json(s, "{}"); return; }
    resp(s, "200 OK", "application/json", buf, n);
}

static void handle_settings_set(sock_t s, unsigned long long clen,
                                const char *left, size_t leftn) {
    char tmp[4200];
    FILE *f;
    char buf[8192];
    unsigned long long got = 0;
    if (!g_settings_path[0] || clen == 0 || clen > SETTINGS_MAX) {
        resp_err(s, "500 Cannot Write");
        return;
    }
    snprintf(tmp, sizeof tmp, "%s.new", g_settings_path);
    f = plat_fopen(tmp, "wb");
    if (!f) { resp_err(s, "500 Cannot Write"); return; }
    if (leftn) {
        size_t take = leftn > clen ? (size_t)clen : leftn;
        if (fwrite(left, 1, take, f) != take) { fclose(f); remove(tmp); resp_err(s, "500 Write Failed"); return; }
        got = take;
    }
    while (got < clen) {
        size_t want = sizeof buf;
        if (clen - got < want) want = (size_t)(clen - got);
#ifdef _WIN32
        { int n = recv(s, buf, (int)want, 0);
          if (n <= 0) { fclose(f); remove(tmp); return; }
          if (fwrite(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); remove(tmp); resp_err(s, "500 Write Failed"); return; }
          got += (unsigned long long)n; }
#else
        { ssize_t n = recv(s, buf, want, 0);
          if (n <= 0) { fclose(f); remove(tmp); return; }
          if (fwrite(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); remove(tmp); resp_err(s, "500 Write Failed"); return; }
          got += (unsigned long long)n; }
#endif
    }
    if (fflush(f) != 0) { fclose(f); remove(tmp); resp_err(s, "500 Write Failed"); return; }
    fclose(f);
    if (plat_replace(tmp, g_settings_path) != 0) { remove(tmp); resp_err(s, "500 Replace Failed"); return; }
    resp_json(s, "{\"ok\":1}");
}

static int parse_id(const char *target) {
    char v[16];
    int id;
    if (!qget(target, "id", v, sizeof v)) return -1;
    id = atoi(v);
    return (id >= 0 && id < g_npaths) ? id : -1;
}

static void handle_conn(sock_t s) {
    char hdr[HDR_MAX];
    size_t got = 0;
    char *body = NULL;
    char method[8] = "", target[2048] = "";
    unsigned long long clen = 0;
    /* read header */
    while (got < sizeof hdr - 1) {
#ifdef _WIN32
        int n = recv(s, hdr + got, (int)(sizeof hdr - 1 - got), 0);
#else
        ssize_t n = recv(s, hdr + got, sizeof hdr - 1 - got, 0);
#endif
        if (n <= 0) return;
        got += (size_t)n;
        hdr[got] = 0;
        if ((body = strstr(hdr, "\r\n\r\n")) != NULL) { body += 4; break; }
    }
    if (!body) return;
    sscanf(hdr, "%7s %2047s", method, target);
    {   /* content-length (case-insensitive scan) */
        char *p = hdr;
        while ((p = strchr(p, '\n')) != NULL) {
            p++;
            if (!strncasecmp_portable(p, "Content-Length:", 15)) { clen = strtoull(p + 15, NULL, 10); break; }
        }
    }

    if (!strcmp(method, "GET") && (!strncmp(target, "/?", 2) || !strcmp(target, "/"))) {
        if (!token_ok(target)) { resp_err(s, "403 Forbidden"); return; }
        resp(s, "200 OK", "text/html; charset=utf-8", g_page, g_page_len);
        return;
    }
    if (!strcmp(method, "GET") && !strncmp(target, "/favicon.ico", 12)) {
        resp_err(s, "204 No Content");
        return;
    }
    if (!token_ok(target)) { resp_err(s, "403 Forbidden"); return; }

    if (!strcmp(method, "GET") && !strncmp(target, "/open", 5)) {
        if (g_npaths > 0) {
            char nm[1024], out[1200];
            json_escape(nm, sizeof nm, base_name(g_paths[0]));
            snprintf(out, sizeof out, "{\"id\":0,\"name\":\"%s\"}", nm);
            resp_json(s, out);
        } else resp_json(s, "{}");
        return;
    }
    if (!strcmp(method, "GET") && !strncmp(target, "/data", 5)) {
        int id = parse_id(target);
        if (id < 0) { resp_err(s, "404 Not Found"); return; }
        serve_data(s, id);
        return;
    }
    if (!strcmp(method, "POST") && !strncmp(target, "/save", 5)) {
        int id = parse_id(target);
        size_t leftn = got - (size_t)(body - hdr);
        if (id < 0) { resp_err(s, "404 Not Found"); return; }
        handle_save(s, id, clen, body, leftn);
        return;
    }
    if (!strcmp(method, "GET") && !strncmp(target, "/dialog/open", 12)) {
        char path[4096];
        if (!plat_dialog_open(path, sizeof path)) { resp_json(s, "{\"cancel\":1}"); return; }
        {   int id = register_path(path);
            char nm[1024], out[1200];
            if (id < 0) { resp_err(s, "500 Too Many Files"); return; }
            add_recent(path);
            json_escape(nm, sizeof nm, base_name(path));
            snprintf(out, sizeof out, "{\"id\":%d,\"name\":\"%s\"}", id, nm);
            resp_json(s, out); }
        return;
    }
    if (!strcmp(method, "GET") && !strncmp(target, "/recents", 8)) {
        char list[RECENT_MAX][2048];
        int n = recents_load(list), i;
        char out[8192];
        size_t o = 0;
        o += (size_t)snprintf(out + o, sizeof out - o, "{\"list\":[");
        for (i = 0; i < n && o + 1200 < sizeof out; i++) {
            char nm[1024];
            json_escape(nm, sizeof nm, base_name(list[i]));
            o += (size_t)snprintf(out + o, sizeof out - o, "%s{\"name\":\"%s\"}", i ? "," : "", nm);
        }
        snprintf(out + o, sizeof out - o, "]}");
        resp_json(s, out);
        return;
    }
    if (!strcmp(method, "GET") && !strncmp(target, "/recent/open", 12)) {
        char v[16], list[RECENT_MAX][2048];
        int n, i;
        if (!qget(target, "i", v, sizeof v)) { resp_err(s, "404 Not Found"); return; }
        i = atoi(v);
        n = recents_load(list);
        if (i < 0 || i >= n) { resp_err(s, "404 Not Found"); return; }
        {   FILE *chk = plat_fopen(list[i], "rb");
            if (!chk) { recents_remove(list[i]); resp_json(s, "{\"gone\":1}"); return; }
            fclose(chk); }
        {   int id = register_path(list[i]);
            char nm[1024], out[1200];
            if (id < 0) { resp_err(s, "500 Too Many Files"); return; }
            add_recent(list[i]);
            json_escape(nm, sizeof nm, base_name(list[i]));
            snprintf(out, sizeof out, "{\"id\":%d,\"name\":\"%s\"}", id, nm);
            resp_json(s, out); }
        return;
    }
    if (!strcmp(method, "GET") && !strncmp(target, "/reveal", 7)) {
        int id = parse_id(target);
        if (id < 0) { resp_err(s, "404 Not Found"); return; }
        plat_reveal(g_paths[id]);
        resp_json(s, "{\"ok\":1}");
        return;
    }
    if (!strcmp(method, "GET") && !strncmp(target, "/settings", 9)) {
        handle_settings_get(s);
        return;
    }
    if (!strcmp(method, "POST") && !strncmp(target, "/settings", 9)) {
        size_t leftn = got - (size_t)(body - hdr);
        handle_settings_set(s, clen, body, leftn);
        return;
    }
    if (!strcmp(method, "GET") && !strncmp(target, "/quit", 5)) {
        resp_json(s, "{\"ok\":1}");
        g_quit = 1;
        return;
    }
    if (!strcmp(method, "GET") && !strncmp(target, "/dialog/saveas", 14)) {
        char suggest[512] = "archive.vpp";
        char path[4096];
        qget(target, "name", suggest, sizeof suggest);
        if (!plat_dialog_save(path, sizeof path, suggest)) { resp_json(s, "{\"cancel\":1}"); return; }
        add_recent(path);
        {   int id = register_path(path);
            char nm[1024], out[1200];
            if (id < 0) { resp_err(s, "500 Too Many Files"); return; }
            json_escape(nm, sizeof nm, base_name(path));
            snprintf(out, sizeof out, "{\"id\":%d,\"name\":\"%s\"}", id, nm);
            resp_json(s, out); }
        return;
    }
    resp_err(s, "404 Not Found");
}

/* case-insensitive strncmp without relying on nonstandard headers */
static int strncasecmp_portable(const char *a, const char *b, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        int ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb || !ca) return ca - cb;
    }
    return 0;
}

/* ------------------------------------------------------------- startup --- */

static void build_page(void) {
    char inject[160];
    const char *mark;
    size_t before, marklen = strlen(BRIDGE_MARK);
    int il = snprintf(inject, sizeof inject,
        "<script>window.VPP_BRIDGE={token:\"%s\"};</script>", g_token);
    mark = (const char *)memmem_portable(APP_HTML, APP_HTML_LEN, BRIDGE_MARK, marklen);
    if (!mark) { g_page = (char *)APP_HTML; g_page_len = APP_HTML_LEN; return; }
    before = (size_t)((const unsigned char *)mark - APP_HTML);
    g_page_len = APP_HTML_LEN - marklen + (size_t)il;
    g_page = (char *)malloc(g_page_len);
    memcpy(g_page, APP_HTML, before);
    memcpy(g_page + before, inject, (size_t)il);
    memcpy(g_page + before + (size_t)il, APP_HTML + before + marklen,
           APP_HTML_LEN - before - marklen);
}
static const void *memmem_portable(const void *hay, size_t hn, const void *nee, size_t nn) {
    const unsigned char *h = (const unsigned char *)hay;
    size_t i;
    if (nn > hn) return NULL;
    for (i = 0; i + nn <= hn; i++)
        if (!memcmp(h + i, nee, nn)) return h + i;
    return NULL;
}

static sock_t make_listener(unsigned short *port_out) {
    sock_t ls;
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
#ifdef _WIN32
    { WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); }
#endif
    ls = socket(AF_INET, SOCK_STREAM, 0);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    if (bind(ls, (struct sockaddr *)&a, sizeof a) != 0) return INVALID_SOCKET;
    if (listen(ls, 16) != 0) return INVALID_SOCKET;
    {   struct sockaddr_in got;
#ifdef _WIN32
        int gl = sizeof got;
#else
        socklen_t gl = sizeof got;
#endif
        getsockname(ls, (struct sockaddr *)&got, &gl);
        *port_out = ntohs(got.sin_port);
    }
    return ls;
}

static void serve_loop(sock_t ls) {
    for (;;) {
        sock_t c = accept(ls, NULL, NULL);
        if (c == INVALID_SOCKET) continue;
        handle_conn(c);
        CLOSESOCK(c);
        if (g_quit) {
#ifdef _WIN32
            if (g_browser_proc) TerminateProcess(g_browser_proc, 0);
            else ExitProcess(0);
            return;
#else
            exit(0);
#endif
        }
    }
}

#ifdef _WIN32

static DWORD WINAPI server_thread(LPVOID p) {
    serve_loop((sock_t)(SIZE_T)p);
    return 0;
}

int APIENTRY wWinMain(HINSTANCE hi, HINSTANCE hp, LPWSTR cl, int ns) {
    unsigned short port = 0;
    sock_t ls;
    char url[256];
    int argc = 0;
    LPWSTR *argv;
    (void)hi; (void)hp; (void)cl; (void)ns;
    srand((unsigned)time(NULL) ^ GetCurrentProcessId());
    rand_token(g_token, 32);
    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc > 1) {
        if (!_wcsicmp(argv[1], L"/install")) {
            if (do_install())
                MessageBoxW(NULL, L"VPP Forge is installed. Double-click any .vpp file to open it.",
                            L"VPP Forge Setup", MB_ICONINFORMATION);
            LocalFree(argv);
            return 0;
        }
        if (!_wcsicmp(argv[1], L"/uninstall")) {
            do_uninstall();
            LocalFree(argv);
            return 0;
        }
        if (argv[1][0] != L'/') {
            char *u = wide_to_utf8(argv[1]);
            if (u) { register_path(u); free(u); }
        }
    }
    if (argv) LocalFree(argv);
    {   wchar_t la[MAX_PATH] = L"", dirw[MAX_PATH * 2];
        char *u;
        GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH);
        _snwprintf(dirw, MAX_PATH * 2 - 1, L"%s\\VPPForge", la);
        dirw[MAX_PATH * 2 - 1] = 0;
        SHCreateDirectoryExW(NULL, dirw, NULL);
        u = wide_to_utf8(dirw);
        if (u) {
            snprintf(g_recent_path, sizeof g_recent_path, "%s\\recent.txt", u);
            snprintf(g_settings_path, sizeof g_settings_path, "%s\\settings.json", u);
            free(u);
        }
    }
    if (g_npaths > 0) add_recent(g_paths[0]);
    maybe_offer_setup();
    build_page();
    ls = make_listener(&port);
    if (ls == INVALID_SOCKET || port == 0) {
        MessageBoxW(NULL, L"Could not start the local bridge.", L"VPP Forge", MB_ICONERROR);
        return 1;
    }
    CreateThread(NULL, 0, server_thread, (LPVOID)(SIZE_T)ls, 0, NULL);
    snprintf(url, sizeof url, "http://127.0.0.1:%u/?t=%s", port, g_token);
    plat_launch_and_wait(url);
    return 0;
}

#else

int main(int argc, char **argv) {
    unsigned short port = 0;
    sock_t ls;
    srand((unsigned)time(NULL) ^ (unsigned)getpid());
    rand_token(g_token, 32);
    { const char *rp = getenv("VPPFORGE_RECENT_FILE");
      snprintf(g_recent_path, sizeof g_recent_path, "%s", rp ? rp : "./vppforge_recent.txt"); }
    { const char *sp = getenv("VPPFORGE_SETTINGS_FILE");
      snprintf(g_settings_path, sizeof g_settings_path, "%s", sp ? sp : "./vppforge_settings.json"); }
    if (argc > 1) register_path(argv[1]);
    if (g_npaths > 0) add_recent(g_paths[0]);
    build_page();
    ls = make_listener(&port);
    if (ls == INVALID_SOCKET || port == 0) { fprintf(stderr, "bind failed\n"); return 1; }
    printf("URL: http://127.0.0.1:%u/?t=%s\n", port, g_token);
    fflush(stdout);
    serve_loop(ls);
    return 0;
}

#endif
