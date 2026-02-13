#include "../src/lua.h"
#include "../src/lualib.h"
#include "../src/lauxlib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

// تابع کمکی برای حذف کدهای رنگ ANSI
void strip_ansi(char *str) {
    char *src = str, *dst = str;
    while (*src) {
        if (*src == '\033' && *(src + 1) == '[') {
            src += 2;
            while (*src && !(*src >= 'a' && *src <= 'z') && !(*src >= 'A' && *src <= 'Z')) src++;
            if (*src) src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

// تابع هوشمند برای گرفتن خروجی، چاپ همزمان و بازگرداندن به متغیر
static void push_and_print_cmd(lua_State *L, const char *cmd) {
    FILE *fp = popen(cmd, "r");
    if (!fp) { lua_pushnil(L); return; }
    
    char buf[1024];
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    
    while (fgets(buf, sizeof(buf), fp)) {
        // ۱. چاپ در کنسول (برای اجرای مستقیم)
        printf("%s", buf);
        // ۲. اضافه کردن به بافر (برای ذخیره در متغیر)
        luaL_addstring(&b, buf);
    }
    fflush(stdout); // اطمینان از خروج داده به ترمینال
    luaL_pushresult(&b);
    pclose(fp);
}

// ۱. آی‌پی عمومی
static int l_public_ip(lua_State *L) {
    push_and_print_cmd(L, "curl -s -A \"Mozilla/5.0\" https://api.ipify.org");
    return 1;
}

// ۲. آی‌پی محلی
static int l_local_ip(lua_State *L) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = inet_addr("8.8.8.8");
    serv.sin_port = htons(53);
    const char* result_ip;
    if (connect(sock, (const struct sockaddr*)&serv, sizeof(serv)) < 0) {
        result_ip = "127.0.0.1";
    } else {
        struct sockaddr_in name;
        socklen_t namelen = sizeof(name);
        getsockname(sock, (struct sockaddr*)&name, &namelen);
        result_ip = inet_ntoa(name.sin_addr);
    }
    printf("%s\n", result_ip); // چاپ مستقیم
    lua_pushstring(L, result_ip); // بازگشت به متغیر
    close(sock);
    return 1;
}

// ۳. دی‌ان‌اس
static int l_dns(lua_State *L) {
    char target[256];
    strncpy(target, luaL_checkstring(L, 1), sizeof(target));
    strip_ansi(target);
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "nslookup %s | grep 'Address' | tail -n 1 | awk '{print $2}'", target);
    push_and_print_cmd(L, cmd);
    return 1;
}

// ۴. اسکن پورت
static int l_scan(lua_State *L) {
    char target[256];
    strncpy(target, luaL_checkstring(L, 1), sizeof(target));
    strip_ansi(target);
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "nc -z -w 1 %s %lld > /dev/null 2>&1", target, luaL_checkinteger(L, 2));
    int res = (system(cmd) == 0);
    printf("%s\n", res ? "OPEN" : "CLOSED"); // چاپ وضعیت
    lua_pushboolean(L, res);
    return 1;
}

// ۵. بنر گرابینگ
static int l_banner(lua_State *L) {
    char target[256];
    strncpy(target, luaL_checkstring(L, 1), sizeof(target));
    strip_ansi(target);
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "echo \" \" | timeout 2 nc %s %lld 2>/dev/null | head -n 1", target, luaL_checkinteger(L, 2));
    push_and_print_cmd(L, cmd);
    return 1;
}

// ۶. ژئو آی‌پی
static int l_geoip(lua_State *L) {
    char ip[256];
    strncpy(ip, luaL_checkstring(L, 1), sizeof(ip));
    strip_ansi(ip);
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "curl -s -A \"Mozilla/5.0\" http://ip-api.com/json/%s", ip);
    push_and_print_cmd(L, cmd);
    return 1;
}

// ۷. هویز
static int l_whois(lua_State *L) {
    char target[256];
    strncpy(target, luaL_checkstring(L, 1), sizeof(target));
    strip_ansi(target);
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "whois %s | grep -iE 'Registrar|Organization|Admin Email' | head -n 5", target);
    push_and_print_cmd(L, cmd);
    return 1;
}

// ۸. وضعیت وب
static int l_status(lua_State *L) {
    char url[256];
    strncpy(url, luaL_checkstring(L, 1), sizeof(url));
    strip_ansi(url);
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "curl -s -o /dev/null -I -L -w '%%{http_code}' %s", url);
    push_and_print_cmd(L, cmd);
    return 1;
}

// ۹. اتصالات فعال
static int l_active(lua_State *L) {
    push_and_print_cmd(L, "netstat -ant 2>/dev/null | grep ESTABLISHED | head -n 5");
    return 1;
}

// ۱۰. دانلودر
static int l_download(lua_State *L) {
    char url[256];
    strncpy(url, luaL_checkstring(L, 1), sizeof(url));
    strip_ansi(url);
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "curl -L -s %s -o %s", url, luaL_checkstring(L, 2));
    int res = (system(cmd) == 0);
    printf("%s\n", res ? "DOWNLOAD SUCCESS" : "DOWNLOAD FAILED");
    lua_pushboolean(L, res);
    return 1;
}

static const struct luaL_Reg netlib[] = {
    {"public_ip", l_public_ip}, {"local_ip", l_local_ip}, {"dns", l_dns},
    {"scan", l_scan}, {"banner", l_banner}, {"geoip", l_geoip},
    {"whois", l_whois}, {"status", l_status}, {"active", l_active},
    {"download", l_download}, {NULL, NULL}
};

int luaopen_net(lua_State *L) { luaL_newlib(L, netlib); return 1; }
