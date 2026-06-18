#include "../src/lua.h"
#include "../src/lualib.h"
#include "../src/lauxlib.h"

static const struct luaL_Reg colorlib[] = {
    {NULL, NULL}
};

int luaopen_color(lua_State *L) {
    luaL_newlib(L, colorlib);

    // تعریف تمام کدهای ANSI برای رنگ‌ها و استایل‌ها
    struct { char *name; char *code; } colors[] = {
        // استایل‌ها
        {"reset",     "\033[0m"},
        {"bold",      "\033[1m"},
        {"dim",       "\033[2m"},
        {"italic",    "\033[3m"},
        {"underline", "\033[4m"},
        {"blink",     "\033[5m"},
        {"reverse",   "\033[7m"},

        // رنگ‌های متن (Foreground)
        {"red",       "\033[1;31m"},
        {"green",     "\033[1;32m"},
        {"yellow",    "\033[1;33m"},
        {"blue",      "\033[1;34m"},
        {"magenta",   "\033[1;35m"},
        {"cyan",      "\033[1;36m"},
        {"white",     "\033[1;37m"},
        {"gray",      "\033[90m"},

        // رنگ‌های پس‌زمینه (Background)
        {"bg_red",     "\033[41m"},
        {"bg_green",   "\033[42m"},
        {"bg_yellow",  "\033[43m"},
        {"bg_blue",    "\033[44m"},
        {"bg_magenta", "\033[45m"},
        {"bg_cyan",    "\033[46m"},
        {"bg_white",   "\033[47m"}
    };

    // اضافه کردن تمام رنگ‌ها به جدول خروجی کتابخانه
    for (int i = 0; i < sizeof(colors)/sizeof(colors[0]); i++) {
        lua_pushstring(L, colors[i].code);
        lua_setfield(L, -2, colors[i].name);
    }

    return 1;
}
