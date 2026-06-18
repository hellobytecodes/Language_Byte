#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#define WORK_DIR ".byte_exe_build"

static int fex(const char *p) { return access(p, F_OK) == 0; }

static int mkdir_p(const char *p) {
    char t[2048]; snprintf(t, sizeof(t), "%s", p);
    for (char *s = t + 1; *s; s++) if (*s == '/') { *s = 0; mkdir(t, 0755); *s = '/'; }
    return mkdir(t, 0755);
}

static int wf(const char *p, const char *c) {
    FILE *f = fopen(p, "w"); if (!f) return -1; fputs(c, f); fclose(f); return 0;
}

static int l_build(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    lua_getfield(L, 1, "code");      const char *code    = lua_tostring(L, -1);
    lua_getfield(L, 1, "name");      const char *name    = lua_tostring(L, -1);
    lua_getfield(L, 1, "console");   int console         = lua_toboolean(L, -1);

    if (!code || !strlen(code)) return luaL_error(L, "code is required");
    if (!name) name = "output";

    system("rm -rf " WORK_DIR);
    mkdir_p(WORK_DIR);

    wf(WORK_DIR "/main.c", code);

    char cmd[4096];
    printf("[*] Compiling with MinGW...\n");

    snprintf(cmd, sizeof(cmd),
        "x86_64-w64-mingw32-gcc "
        "-o " WORK_DIR "/%s.exe "
        "" WORK_DIR "/main.c "
        "-static -lwininet -lws2_32 -luser32 -lshell32 -ladvapi32",
        name);

    if (!console) strcat(cmd, " -mwindows");

    if (system(cmd) != 0) {
        system("rm -rf " WORK_DIR);
        return luaL_error(L, "Compilation failed");
    }

    snprintf(cmd, sizeof(cmd), "cp " WORK_DIR "/%s.exe ./", name);
    system(cmd);
    system("rm -rf " WORK_DIR);

    char out[512];
    snprintf(out, sizeof(out), "%s.exe", name);
    lua_pushstring(L, out);
    printf("[OK] %s\n", out);
    return 1;
}

int luaopen_winbuild(lua_State *L) {
    lua_newtable(L);
    lua_pushcfunction(L, l_build);
    lua_setfield(L, -2, "build");
    return 1;
}
