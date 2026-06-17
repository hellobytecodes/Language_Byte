#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include "../../src/lua.h"
#include "../../src/lauxlib.h"

// --- بخش مدیریت سیستم ---
static int l_run(lua_State *L) { lua_pushinteger(L, system(luaL_checkstring(L, 1))); return 1; }

static int l_name(lua_State *L) { 
    struct utsname buf; uname(&buf);
    lua_pushstring(L, buf.sysname); return 1; 
}

static int l_version(lua_State *L) {
    struct utsname buf; uname(&buf);
    lua_pushstring(L, buf.release); return 1;
}

static int l_arch(lua_State *L) {
    struct utsname buf; uname(&buf);
    lua_pushstring(L, buf.machine); return 1;
}

static int l_uptime(lua_State *L) {
    FILE *f = fopen("/proc/uptime", "r");
    if (!f) { lua_pushnumber(L, 0); return 1; }
    double up; fscanf(f, "%lf", &up); fclose(f);
    lua_pushnumber(L, up); return 1;
}

// --- بخش مدیریت فایل و پوشه ---
static int l_mkdir(lua_State *L) { lua_pushboolean(L, mkdir(luaL_checkstring(L, 1), 0777) == 0); return 1; }
static int l_rmdir(lua_State *L) { lua_pushboolean(L, rmdir(luaL_checkstring(L, 1)) == 0); return 1; }
static int l_delete(lua_State *L) { lua_pushboolean(L, remove(luaL_checkstring(L, 1)) == 0); return 1; }
static int l_exists(lua_State *L) { lua_pushboolean(L, access(luaL_checkstring(L, 1), F_OK) == 0); return 1; }
static int l_rename(lua_State *L) { 
    lua_pushboolean(L, rename(luaL_checkstring(L, 1), luaL_checkstring(L, 2)) == 0); return 1; 
}

// --- بخش اطلاعات کاربر و پروسس ---
static int l_getuser(lua_State *L) { 
    char *u = getenv("USER");
    lua_pushstring(L, u ? u : "unknown"); return 1; 
}
static int l_getpid(lua_State *L) { lua_pushinteger(L, getpid()); return 1; }
static int l_getenv(lua_State *L) { 
    char *v = getenv(luaL_checkstring(L, 1));
    lua_pushstring(L, v ? v : ""); return 1; 
}
static int l_exit(lua_State *L) { exit(luaL_optinteger(L, 1, 0)); return 0; }

// --- بخش ابزارهای کاربردی ---
static int l_sleep(lua_State *L) { sleep(luaL_checkinteger(L, 1)); return 0; }
static int l_time(lua_State *L) { lua_pushinteger(L, (int)time(NULL)); return 1; }

static int l_list(lua_State *L) {
    DIR *d = opendir(luaL_optstring(L, 1, "."));
    if (!d) return 0;
    struct dirent *dir;
    lua_newtable(L);
    int i = 1;
    while ((dir = readdir(d)) != NULL) {
        lua_pushstring(L, dir->d_name);
        lua_rawseti(L, -2, i++);
    }
    closedir(d);
    return 1;
}

static int l_pwd(lua_State *L) {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd))) lua_pushstring(L, cwd);
    else lua_pushstring(L, ".");
    return 1;
}

static int l_chmod(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    const char *mode_str = luaL_checkstring(L, 2);
    int mode = (int)strtol(mode_str, NULL, 8);
    lua_pushboolean(L, chmod(path, mode) == 0);
    return 1;
}

static int l_cpu_info(lua_State *L) {
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f) { lua_pushstring(L, "Generic CPU"); return 1; }
    char line[256];
    int found = 0;
    while(fgets(line, sizeof(line), f)) {
        if(strstr(line, "model name") || strstr(line, "Processor") || strstr(line, "Hardware")) {
            char *colon = strchr(line, ':');
            if (colon) {
                char *res = colon + 2;
                res[strcspn(res, "\n")] = 0; // حذف اینتر آخر خط
                lua_pushstring(L, res);
                found = 1; break;
            }
        }
    }
    fclose(f);
    if (!found) lua_pushstring(L, "ARM Processer / Unknown");
    return 1;
}

static int l_get_temp(lua_State *L) {
    FILE *f = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (!f) { lua_pushinteger(L, 0); return 1; }
    int t; fscanf(f, "%d", &t); fclose(f);
    lua_pushnumber(L, t / 1000.0); return 1;
}

// ثبت ۲۱ متد
static const struct luaL_Reg osblib[] = {
    {"run", l_run}, {"name", l_name}, {"version", l_version}, {"arch", l_arch},
    {"uptime", l_uptime}, {"mkdir", l_mkdir}, {"rmdir", l_rmdir}, {"delete", l_delete},
    {"exists", l_exists}, {"rename", l_rename}, {"user", l_getuser}, {"pid", l_getpid},
    {"env", l_getenv}, {"exit", l_exit}, {"sleep", l_sleep}, {"time", l_time},
    {"list", l_list}, {"pwd", l_pwd}, {"chmod", l_chmod}, {"cpu", l_cpu_info},
    {"temp", l_get_temp},
    {NULL, NULL}
};

int luaopen_osb(lua_State *L) {
    luaL_newlib(L, osblib);
    return 1;
}
