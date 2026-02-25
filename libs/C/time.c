#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include "../../src/lua.h"
#include "../../src/lauxlib.h"

// ==================== توابع زمان ====================

// زمان فعلی (timestamp)
static int l_now(lua_State *L) {
    time_t t = time(NULL);
    lua_pushinteger(L, (lua_Integer)t);
    return 1;
}

// زمان با میلی‌ثانیه
static int l_now_ms(lua_State *L) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    lua_pushinteger(L, (lua_Integer)(tv.tv_sec * 1000 + tv.tv_usec / 1000));
    return 1;
}

// تاریخ و زمان فرمت شده
static int l_format(lua_State *L) {
    time_t t = time(NULL);
    const char *format = luaL_optstring(L, 1, "%Y-%m-%d %H:%M:%S");
    
    char buffer[256];
    struct tm *tm_info = localtime(&t);
    strftime(buffer, sizeof(buffer), format, tm_info);
    
    lua_pushstring(L, buffer);
    return 1;
}

// تاخیر (ثانیه)
static int l_sleep(lua_State *L) {
    int seconds = luaL_checkinteger(L, 1);
    sleep(seconds);
    return 0;
}

// تاخیر (میلی‌ثانیه)
static int l_msleep(lua_State *L) {
    int ms = luaL_checkinteger(L, 1);
    usleep(ms * 1000);
    return 0;
}

// زمان اجرا (تايمر)
static int l_timer(lua_State *L) {
    static clock_t start = 0;
    const char *cmd = luaL_optstring(L, 1, "start");
    
    if(strcmp(cmd, "start") == 0) {
        start = clock();
        lua_pushboolean(L, 1);
    }
    else if(strcmp(cmd, "stop") == 0) {
        clock_t end = clock();
        double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
        lua_pushnumber(L, elapsed);
    }
    else {
        lua_pushnil(L);
    }
    return 1;
}

// جزءهای تاریخ
static int l_date(lua_State *L) {
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    
    lua_newtable(L);
    lua_pushinteger(L, tm_info->tm_year + 1900); lua_setfield(L, -2, "year");
    lua_pushinteger(L, tm_info->tm_mon + 1); lua_setfield(L, -2, "month");
    lua_pushinteger(L, tm_info->tm_mday); lua_setfield(L, -2, "day");
    lua_pushinteger(L, tm_info->tm_hour); lua_setfield(L, -2, "hour");
    lua_pushinteger(L, tm_info->tm_min); lua_setfield(L, -2, "min");
    lua_pushinteger(L, tm_info->tm_sec); lua_setfield(L, -2, "sec");
    lua_pushinteger(L, tm_info->tm_wday); lua_setfield(L, -2, "wday");  // 0=یکشنبه
    
    return 1;
}

// تبدیل timestamp به تاریخ
static int l_from_timestamp(lua_State *L) {
    time_t t = luaL_checkinteger(L, 1);
    const char *format = luaL_optstring(L, 2, "%Y-%m-%d %H:%M:%S");
    
    char buffer[256];
    struct tm *tm_info = localtime(&t);
    strftime(buffer, sizeof(buffer), format, tm_info);
    
    lua_pushstring(L, buffer);
    return 1;
}

// اختلاف زمان
static int l_diff(lua_State *L) {
    time_t t1 = luaL_checkinteger(L, 1);
    time_t t2 = luaL_optinteger(L, 2, time(NULL));
    
    double diff = difftime(t1, t2);
    lua_pushnumber(L, diff);
    return 1;
}

// ثبت توابع
static const struct luaL_Reg time_lib[] = {
    {"now", l_now},
    {"now_ms", l_now_ms},
    {"format", l_format},
    {"sleep", l_sleep},
    {"msleep", l_msleep},
    {"timer", l_timer},
    {"date", l_date},
    {"from_timestamp", l_from_timestamp},
    {"diff", l_diff},
    {NULL, NULL}
};

int luaopen_time(lua_State *L) {
    luaL_newlib(L, time_lib);
    return 1;
}
