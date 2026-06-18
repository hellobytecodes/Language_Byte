/*
 * byteget.c – Byte Package Manager v3.1 (Final)
 *
 * فقط فایل .so رو دانلود می‌کنه، بدون پوشه اضافی
 *
 * کامپایل:
 *   gcc -o byteget byteget.c -lcurl -O2 -w
 *
 * نصب:
 *   Termux:  cp byteget $PREFIX/bin/
 *   Linux:   sudo cp byteget /usr/local/bin/
 *   Kali:    sudo cp byteget /usr/local/bin/
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <curl/curl.h>

#define RAW_URL      "https://raw.githubusercontent.com/hellobytecodes/Byte_library/main"
#define API_URL      "https://api.github.com/repos/hellobytecodes/Byte_library/contents"
#define INSTALL_DIR  "/data/data/com.termux/files/home/byte_lang/byte-1.1.0-termux-amd64/libs/C/"
#define MAX_BUF      16384

/* ── curl write callback ── */
struct mem { char *data; size_t size; };

static size_t write_callback(void *c, size_t s, size_t n, void *u) {
    size_t r = s * n;
    struct mem *m = (struct mem *)u;
    char *p = realloc(m->data, m->size + r + 1);
    if (!p) return 0;
    m->data = p;
    memcpy(&(m->data[m->size]), c, r);
    m->size += r;
    m->data[m->size] = 0;
    return r;
}

/* ── progress bar ── */
static int progress_callback(void *c, double dl, double dn, double ul, double un) {
    (void)c; (void)ul; (void)un;
    if (dl > 0) {
        int p = (int)(dn * 100 / dl);
        printf("\r  [");
        for (int i = 0; i < 50; i++) printf(i < p / 2 ? "=" : " ");
        printf("] %d%%", p);
        fflush(stdout);
    }
    return 0;
}

/* ── HTTP GET ── */
static int http_get(const char *url, char **resp) {
    CURL *curl = curl_easy_init();
    if (!curl) return 1;
    struct mem chunk = {0};
    chunk.data = malloc(1); chunk.size = 0;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "ByteGet/3.1");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    CURLcode r = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (r != CURLE_OK) { free(chunk.data); return 1; }
    *resp = chunk.data;
    return 0;
}

/* ── download .so file ── */
static int download_so(const char *url, const char *path) {
    CURL *curl = curl_easy_init();
    if (!curl) return 1;
    FILE *f = fopen(path, "wb");
    if (!f) { curl_easy_cleanup(curl); return 1; }
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    CURLcode r = curl_easy_perform(curl);
    fclose(f);
    curl_easy_cleanup(curl);
    printf("\n");
    return r != CURLE_OK;
}

/* ── check library exists on GitHub ── */
static int lib_exists(const char *name) {
    char url[512];
    snprintf(url, sizeof(url), "%s/%s", API_URL, name);
    CURL *curl = curl_easy_init();
    if (!curl) return 0;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "ByteGet/3.1");
    curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);
    return code == 200;
}

/* ── get installed version ── */
static char* installed_ver(const char *name) {
    static char v[64];
    char p[512];
    snprintf(p, sizeof(p), "%s.%s.version", INSTALL_DIR, name);
    FILE *f = fopen(p, "r");
    if (!f) return NULL;
    if (fgets(v, sizeof(v), f)) { v[strcspn(v, "\n")] = 0; fclose(f); return v; }
    fclose(f);
    return NULL;
}

/* ── get latest version from GitHub ── */
static char* latest_ver(const char *name) {
    static char v[64];
    char url[512];
    snprintf(url, sizeof(url), "%s/%s/.version", RAW_URL, name);
    char *r = NULL;
    if (http_get(url, &r)) return NULL;
    if (!r) return NULL;
    strncpy(v, r, 63);
    v[strcspn(v, "\n")] = 0;
    free(r);
    return v;
}

/* ═══════════════════════════════════════
   COMMANDS
   ═══════════════════════════════════════ */

static void cmd_install(const char *name) {
    if (!lib_exists(name)) {
        printf("Error: Library '%s' not found.\n", name);
        return;
    }

    char *latest = latest_ver(name);
    char *current = installed_ver(name);

    if (current && latest && !strcmp(current, latest)) {
        printf("Requirement already satisfied: %s (version %s)\n", name, current);
        return;
    }

    if (current)
        printf("Upgrading %s (%s -> %s)...\n", name, current, latest ? latest : "?");
    else
        printf("Installing %s (%s)...\n", name, latest ? latest : "?");

    char so_url[512], so_path[512], tmp[512];
    snprintf(so_url, sizeof(so_url), "%s/%s/%s.so", RAW_URL, name, name);
    snprintf(so_path, sizeof(so_path), "%s%s.so", INSTALL_DIR, name);
    snprintf(tmp, sizeof(tmp), "%s%s.so.tmp", INSTALL_DIR, name);

    if (download_so(so_url, tmp)) {
        printf("Error: Download failed. Check your internet.\n");
        unlink(tmp);
        return;
    }
    rename(tmp, so_path);

    /* save version */
    if (latest) {
        char vp[512];
        snprintf(vp, sizeof(vp), "%s.%s.version", INSTALL_DIR, name);
        FILE *f = fopen(vp, "w");
        if (f) { fprintf(f, "%s\n", latest); fclose(f); }
    }

    printf("Successfully installed %s %s\n", name, latest ? latest : "");
}

static void cmd_remove(const char *name) {
    char so[512], vp[512];
    snprintf(so, sizeof(so), "%s%s.so", INSTALL_DIR, name);
    snprintf(vp, sizeof(vp), "%s.%s.version", INSTALL_DIR, name);
    int ok = (unlink(so) == 0);
    unlink(vp);
    if (ok) printf("Removed %s\n", name);
    else printf("Error: '%s' is not installed.\n", name);
}

static void cmd_list(int verbose) {
    if (verbose) {
        printf("Installed libraries:\n");
        char cmd[512];
        snprintf(cmd, sizeof(cmd),
            "for f in %s*.so; do [ -f \"$f\" ] && n=$(basename $f .so) && "
            "v=$(cat %s.$n.version 2>/dev/null || echo '?') && "
            "echo \"  $n ($v)\"; done 2>/dev/null", INSTALL_DIR, INSTALL_DIR);
        system(cmd);
    } else {
        printf("Installed libraries:\n");
        char cmd[512];
        snprintf(cmd, sizeof(cmd),
            "for f in %s*.so; do [ -f \"$f\" ] && echo \"  $(basename $f .so)\"; done 2>/dev/null", INSTALL_DIR);
        int r = system(cmd);
        if (r != 0) printf("  (none)\n");
    }
}

static void cmd_search(void) {
    printf("Fetching library list...\n");
    char *resp = NULL;
    if (http_get(API_URL, &resp)) { printf("Error: Cannot reach GitHub.\n"); return; }
    printf("\nAvailable libraries:\n");
    char *p = resp;
    while ((p = strstr(p, "\"name\":\""))) {
        p += 8;
        char *e = strchr(p, '"');
        if (e) { *e = 0;
            if (strcmp(p, "README.md") && strcmp(p, ".gitignore")) printf("  - %s\n", p);
            p = e + 1; }
    }
    free(resp);
}

static void cmd_update(void) {
    printf("Updating all libraries...\n");
    char *resp = NULL;
    if (http_get(API_URL, &resp)) { printf("Error: Cannot reach GitHub.\n"); return; }
    char *p = resp;
    int total = 0, upd = 0;
    while ((p = strstr(p, "\"name\":\""))) {
        p += 8;
        char *e = strchr(p, '"');
        if (e) { *e = 0;
            if (strcmp(p, "README.md") && strcmp(p, ".gitignore")) {
                char sp[512]; snprintf(sp, sizeof(sp), "%s%s.so", INSTALL_DIR, p);
                if (access(sp, F_OK) == 0) {
                    total++;
                    char *lv = latest_ver(p), *cv = installed_ver(p);
                    if (!cv || (lv && strcmp(cv, lv))) { printf("  Updating %s...\n", p); cmd_install(p); upd++; }
                }
            }
            p = e + 1; }
    }
    free(resp);
    if (!total) printf("  No libraries installed.\n");
    else if (!upd) printf("  All %d libraries are up-to-date.\n", total);
    else printf("  Updated %d/%d libraries.\n", upd, total);
}

static void cmd_outdated(void) {
    printf("Checking for updates...\n");
    char *resp = NULL;
    if (http_get(API_URL, &resp)) { printf("Error: Cannot reach GitHub.\n"); return; }
    char *p = resp;
    int out = 0;
    while ((p = strstr(p, "\"name\":\""))) {
        p += 8;
        char *e = strchr(p, '"');
        if (e) { *e = 0;
            if (strcmp(p, "README.md") && strcmp(p, ".gitignore")) {
                char sp[512]; snprintf(sp, sizeof(sp), "%s%s.so", INSTALL_DIR, p);
                if (access(sp, F_OK) == 0) {
                    char *lv = latest_ver(p), *cv = installed_ver(p);
                    if (cv && lv && strcmp(cv, lv)) { printf("  %s (%s -> %s)\n", p, cv, lv); out++; }
                }
            }
            p = e + 1; }
    }
    free(resp);
    if (!out) printf("  All up-to-date.\n");
    else printf("  %d library(s) can be updated. Run 'byteget update'.\n", out);
}

static void cmd_info(const char *name) {
    if (!lib_exists(name)) { printf("Library '%s' not found.\n", name); return; }
    char *lv = latest_ver(name), *cv = installed_ver(name);
    printf("Name:    %s\n", name);
    printf("Latest:  %s\n", lv ? lv : "?");
    printf("Status:  %s\n", cv ? "Installed" : "Not installed");
    if (cv) printf("Version: %s\n", cv);
    if (cv && lv && strcmp(cv, lv)) printf("         (outdated – run 'byteget install %s')\n", name);
}

static void cmd_help(void) {
    printf("ByteGet – Package Manager for Byte Language\n\n");
    printf("Usage:\n");
    printf("  byteget install <lib>     Install/upgrade\n");
    printf("  byteget remove <lib>      Remove\n");
    printf("  byteget list              List installed\n");
    printf("  byteget list -v           List with versions\n");
    printf("  byteget search            Search on GitHub\n");
    printf("  byteget update            Update all\n");
    printf("  byteget outdated          Show outdated\n");
    printf("  byteget info <lib>        Show details\n");
    printf("  byteget help              This help\n\n");
    printf("Repo: https://github.com/hellobytecodes/Byte_library\n");
}

/* ═══════════════════════════════════════
   MAIN
   ═══════════════════════════════════════ */

int main(int argc, char **argv) {
    if (argc < 2) { cmd_help(); return 0; }

    const char *cmd = argv[1];

    if      (!strcmp(cmd, "install") || !strcmp(cmd, "i"))
        argc < 3 ? printf("Usage: byteget install <lib>\n") : cmd_install(argv[2]);
    else if (!strcmp(cmd, "remove") || !strcmp(cmd, "rm"))
        argc < 3 ? printf("Usage: byteget remove <lib>\n") : cmd_remove(argv[2]);
    else if (!strcmp(cmd, "list") || !strcmp(cmd, "ls"))
        cmd_list(argc >= 3 && !strcmp(argv[2], "-v"));
    else if (!strcmp(cmd, "search") || !strcmp(cmd, "s"))
        cmd_search();
    else if (!strcmp(cmd, "update") || !strcmp(cmd, "upgrade") || !strcmp(cmd, "up"))
        cmd_update();
    else if (!strcmp(cmd, "outdated"))
        cmd_outdated();
    else if (!strcmp(cmd, "info") || !strcmp(cmd, "show"))
        argc < 3 ? printf("Usage: byteget info <lib>\n") : cmd_info(argv[2]);
    else if (!strcmp(cmd, "help") || !strcmp(cmd, "-h") || !strcmp(cmd, "--help"))
        cmd_help();
    else
        printf("Unknown: %s\nRun 'byteget help'.\n", cmd);

    return 0;
}
