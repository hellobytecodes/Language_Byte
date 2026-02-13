#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/lua.h"
#include "src/lualib.h"
#include "src/lauxlib.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("==============================\n");
        printf("   Byte Language v1.1\n");
        printf("   Usage: ./byte <file.by>\n");
        printf("==============================\n");
        return 1;
    }

    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    // ۱. تنظیم دستورات پایه (بدون اینتر خودکار در echo)
    // این کد باعث می‌شود echo مثل یک ابزار حرفه‌ای رفتار کند
    const char *base_funcs = 
        "echo = function(...) "
        "  local args = {...} "
        "  for i, v in ipairs(args) do "
        "    io.write(tostring(v)) "
        "  end "
        "end "
        "function input(m) "
        "  io.write(m or '') "
        "  io.flush() "
        "  return io.read() "
        "end";
    
    luaL_dostring(L, base_funcs);

    // ۲. تنظیم مسیرهای کتابخانه (Lua و C)
    luaL_dostring(L, "package.path = './libs/Lua/?.by;./libs/Lua/?.lua;' .. package.path");
    luaL_dostring(L, "package.cpath = './libs/C/?.so;' .. package.cpath");

    // ۳. تابع هندل‌کننده include هوشمند
    luaL_dostring(L, 
        "function _byte_include(name) "
        "  local status, lib = pcall(require, name) "
        "  if status then _G[name] = lib "
        "  else io.write('\\n[!] Error loading: ' .. name .. '\\n' .. lib .. '\\n') end "
        "end"
    );

    // ۴. خواندن فایل کد کاربر
    FILE *f = fopen(argv[1], "rb");
    if (!f) { 
        printf("Error: Could not open file %s\n", argv[1]); 
        lua_close(L);
        return 1; 
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *raw_code = malloc(size + 1);
    if (!raw_code) {
        fclose(f);
        lua_close(L);
        return 1;
    }
    fread(raw_code, 1, size, f);
    fclose(f);
    raw_code[size] = '\0';

    // ۵. جایگزینی ایمن #include با _byte_include با استفاده از موتور Lua
    lua_getglobal(L, "string");
    lua_getfield(L, -1, "gsub");
    lua_pushstring(L, raw_code);
    lua_pushstring(L, "#include");
    lua_pushstring(L, "_byte_include");
    
    if (lua_pcall(L, 3, 1, 0) != LUA_OK) {
        printf("Pre-processor Error: %s\n", lua_tostring(L, -1));
        free(raw_code);
        lua_close(L);
        return 1;
    }
    
    const char *processed_code = lua_tostring(L, -1);

    // ۶. اجرای کد نهایی کاربر
    if (luaL_dostring(L, processed_code) != LUA_OK) {
        fprintf(stderr, "\033[1;31mByte Runtime Error:\033[0m\n%s\n", lua_tostring(L, -1));
    }

    free(raw_code);
    lua_close(L);
    return 0;
}
