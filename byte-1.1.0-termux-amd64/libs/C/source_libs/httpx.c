/*
 * httpx.c – Byte HTTP Library v6.1 (Complete)
 * تمام قابلیت‌ها + ریدایرکت واقعی
 *
 * کامپایل:
 *   gcc -shared -fPIC -I../../../src -o ../httpx.so httpx.c -O2 -lssl -lcrypto
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <strings.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#define MAX_BUF 2097152
#define MAX_REDIRECTS 10

/* ── Session ── */
typedef struct {
    char base_url[1024];
    char headers[4096];
    char cookies[4096];
    char proxy[256];
    char auth_type[32];
    char auth_user[256];
    char auth_pass[256];
    int  timeout;
    int  follow_redirects;
    int  max_redirects;
    int  verify_ssl;
    char cert_file[512];
    char key_file[512];
    char user_agent[256];
} Session;

/* ── URL ── */
typedef struct {
    char host[256];
    char path[1024];
    int  port;
    int  is_ssl;
} URL;

static void parse_url(const char *url_str, URL *url) {
    memset(url, 0, sizeof(URL));
    url->port = 80;
    if (strncmp(url_str, "https://", 8) == 0) { url->is_ssl = 1; url->port = 443; url_str += 8; }
    else if (strncmp(url_str, "http://", 7) == 0) { url_str += 7; }
    const char *slash = strchr(url_str, '/');
    if (slash) { strncpy(url->host, url_str, slash - url_str); strncpy(url->path, slash, 1023); }
    else { strncpy(url->host, url_str, 255); strcpy(url->path, "/"); }
    char *colon = strchr(url->host, ':');
    if (colon) { *colon = '\0'; url->port = atoi(colon + 1); }
}

/* ── SSL ── */
static SSL_CTX *ssl_ctx = NULL;
static int ssl_initialized = 0;
static void init_ssl() {
    if (ssl_initialized) return;
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    ssl_ctx = SSL_CTX_new(TLS_client_method());
    SSL_CTX_set_mode(ssl_ctx, SSL_MODE_AUTO_RETRY);
    ssl_initialized = 1;
}

/* ── Socket ── */
static int sock_connect_timeout(const char *host, int port, int timeout_sec) {
    struct hostent *he = gethostbyname(host);
    if (!he) return -1;
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    int ret = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    if (ret < 0 && errno != EINPROGRESS) { close(sock); return -1; }
    fd_set fdset; struct timeval tv;
    FD_ZERO(&fdset); FD_SET(sock, &fdset);
    tv.tv_sec = timeout_sec; tv.tv_usec = 0;
    ret = select(sock + 1, NULL, &fdset, NULL, &tv);
    if (ret <= 0) { close(sock); return -1; }
    fcntl(sock, F_SETFL, flags);
    int so_error; socklen_t len = sizeof(so_error);
    getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
    if (so_error) { close(sock); return -1; }
    return sock;
}

/* ── URL Encode ── */
static void url_encode(const char *src, char *dst, size_t max) {
    const char *hex = "0123456789ABCDEF";
    while (*src && max > 3) {
        if ((*src >= 'A' && *src <= 'Z') || (*src >= 'a' && *src <= 'z') ||
            (*src >= '0' && *src <= '9') || *src == '-' || *src == '_' ||
            *src == '.' || *src == '~') {
            *dst++ = *src; max--;
        } else {
            *dst++ = '%'; *dst++ = hex[*src >> 4]; *dst++ = hex[*src & 15];
            max -= 3;
        }
        src++;
    }
    *dst = '\0';
}

/* ── Build Request ── */
static char* build_request(const char *method, const char *url_str, Session *s,
                           int opts_idx, lua_State *L, size_t *req_len) {
    static char req[MAX_BUF];
    URL url;
    parse_url(url_str, &url);

    char full_path[2048];
    strcpy(full_path, url.path);
    if (opts_idx > 0 && lua_istable(L, opts_idx)) {
        lua_getfield(L, opts_idx, "params");
        if (lua_istable(L, -1)) {
            strcat(full_path, "?");
            int first = 1;
            lua_pushnil(L);
            while (lua_next(L, -2)) {
                if (!first) strcat(full_path, "&");
                char ek[512], ev[512];
                url_encode(lua_tostring(L, -2), ek, sizeof(ek));
                const char *v = lua_tostring(L, -1);
                if (v) url_encode(v, ev, sizeof(ev)); else ev[0] = '\0';
                strcat(full_path, ek); strcat(full_path, "="); strcat(full_path, ev);
                first = 0;
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }

    snprintf(req, MAX_BUF, "%s %s HTTP/1.1\r\nHost: %s\r\n", method, full_path, url.host);
    const char *ua = s ? s->user_agent : "Byte-HttpX/6.1";
    if (opts_idx > 0 && lua_istable(L, opts_idx)) {
        lua_getfield(L, opts_idx, "user_agent"); if (lua_isstring(L, -1)) ua = lua_tostring(L, -1); lua_pop(L, 1);
    }
    strcat(req, "User-Agent: "); strcat(req, ua); strcat(req, "\r\n");
    strcat(req, "Connection: close\r\n");
    strcat(req, "Accept: */*\r\n");

    char header_buf[8192] = "";
    if (s && s->headers[0]) { strcat(header_buf, s->headers); strcat(header_buf, "\n"); }
    if (opts_idx > 0 && lua_istable(L, opts_idx)) {
        lua_getfield(L, opts_idx, "headers");
        if (lua_istable(L, -1)) {
            lua_pushnil(L);
            while (lua_next(L, -2)) {
                strcat(header_buf, lua_tostring(L, -2));
                strcat(header_buf, ": ");
                strcat(header_buf, lua_tostring(L, -1));
                strcat(header_buf, "\n");
                lua_pop(L, 1);
            }
        } else if (lua_isstring(L, -1)) {
            strcat(header_buf, lua_tostring(L, -1));
            strcat(header_buf, "\n");
        }
        lua_pop(L, 1);
    }

    if (opts_idx > 0 && lua_istable(L, opts_idx)) {
        lua_getfield(L, opts_idx, "auth");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "type"); const char *at = lua_tostring(L, -1); lua_pop(L, 1);
            lua_getfield(L, -1, "user"); const char *au = lua_tostring(L, -1); lua_pop(L, 1);
            lua_getfield(L, -1, "pass"); const char *ap = lua_tostring(L, -1); lua_pop(L, 1);
            if (at && au) {
                if (!strcmp(at, "basic")) {
                    char cred[512];
                    snprintf(cred, sizeof(cred), "%s:%s", au, ap ? ap : "");
                    strcat(header_buf, "Authorization: Basic ");
                    strcat(header_buf, cred); strcat(header_buf, "\n");
                } else if (!strcmp(at, "bearer")) {
                    strcat(header_buf, "Authorization: Bearer ");
                    strcat(header_buf, au); strcat(header_buf, "\n");
                }
            }
        }
        lua_pop(L, 1);
    }

    if (s && s->cookies[0]) { strcat(header_buf, "Cookie: "); strcat(header_buf, s->cookies); strcat(header_buf, "\n"); }
    if (opts_idx > 0 && lua_istable(L, opts_idx)) {
        lua_getfield(L, opts_idx, "cookies");
        if (lua_istable(L, -1)) {
            char ck[4096] = "";
            lua_pushnil(L);
            while (lua_next(L, -2)) {
                if (ck[0]) strcat(ck, "; ");
                strcat(ck, lua_tostring(L, -2)); strcat(ck, "="); strcat(ck, lua_tostring(L, -1));
                lua_pop(L, 1);
            }
            if (ck[0]) { strcat(header_buf, "Cookie: "); strcat(header_buf, ck); strcat(header_buf, "\n"); }
        } else if (lua_isstring(L, -1)) {
            strcat(header_buf, "Cookie: "); strcat(header_buf, lua_tostring(L, -1)); strcat(header_buf, "\n");
        }
        lua_pop(L, 1);
    }

    char *hdr = strtok(header_buf, "\n");
    while (hdr) {
        while (*hdr == ' ' || *hdr == '\t') hdr++;
        if (*hdr && !strstr(req, hdr)) { strcat(req, hdr); strcat(req, "\r\n"); }
        hdr = strtok(NULL, "\n");
    }

    const char *body = NULL;
    char json_body[MAX_BUF], form_body[MAX_BUF];
    int is_json = 0, is_form = 0;

    if (opts_idx > 0 && lua_istable(L, opts_idx)) {
        lua_getfield(L, opts_idx, "json");
        if (lua_istable(L, -1)) {
            lua_getglobal(L, "json");
            if (lua_istable(L, -1)) {
                lua_getfield(L, -1, "encode");
                lua_pushvalue(L, -3);
                if (lua_pcall(L, 1, 1, 0) == LUA_OK && lua_isstring(L, -1)) {
                    strcpy(json_body, lua_tostring(L, -1));
                    body = json_body; is_json = 1;
                }
                lua_pop(L, 2);
            } else lua_pop(L, 2);
        }
        lua_pop(L, 1);
    }

    if (!body && opts_idx > 0 && lua_istable(L, opts_idx)) {
        lua_getfield(L, opts_idx, "data");
        if (lua_istable(L, -1)) {
            form_body[0] = '\0';
            lua_pushnil(L);
            while (lua_next(L, -2)) {
                if (form_body[0]) strcat(form_body, "&");
                char ek[512], ev[512];
                url_encode(lua_tostring(L, -2), ek, sizeof(ek));
                const char *v = lua_tostring(L, -1);
                if (v) url_encode(v, ev, sizeof(ev)); else ev[0] = '\0';
                strcat(form_body, ek); strcat(form_body, "="); strcat(form_body, ev);
                lua_pop(L, 1);
            }
            body = form_body; is_form = 1;
        } else if (lua_isstring(L, -1)) {
            body = lua_tostring(L, -1);
        }
        lua_pop(L, 1);
    }

    if (body) {
        char len[32];
        snprintf(len, sizeof(len), "Content-Length: %zu\r\n", strlen(body));
        strcat(req, len);
        if (is_json) strcat(req, "Content-Type: application/json\r\n");
        else if (is_form) strcat(req, "Content-Type: application/x-www-form-urlencoded\r\n");
    }

    strcat(req, "\r\n");
    if (body) strcat(req, body);
    *req_len = strlen(req);
    return req;
}

/* ── Send & Receive (با ریدایرکت واقعی) ── */
static int send_recv(const char *host, int port, int is_ssl, const char *req, size_t req_len,
                     char *response, size_t max_resp, int timeout, int follow_redirects) {
    char current_host[256];
    strncpy(current_host, host, 255);
    int current_port = port;
    int current_ssl = is_ssl;
    char current_req[MAX_BUF];
    strncpy(current_req, req, MAX_BUF - 1);
    size_t current_len = req_len;

    for (int redirect = 0; redirect < MAX_REDIRECTS; redirect++) {
        int sock = sock_connect_timeout(current_host, current_port, timeout);
        if (sock < 0) return -1;

        if (current_ssl) {
            init_ssl();
            SSL *ssl = SSL_new(ssl_ctx);
            SSL_set_fd(ssl, sock);
            if (SSL_connect(ssl) <= 0) { SSL_free(ssl); close(sock); return -1; }
            SSL_write(ssl, current_req, current_len);
            int total = 0, n;
            while (total < (int)max_resp - 1 && (n = SSL_read(ssl, response + total, max_resp - total - 1)) > 0) total += n;
            response[total] = '\0';
            SSL_shutdown(ssl); SSL_free(ssl);
        } else {
            send(sock, current_req, current_len, 0);
            int total = 0, n;
            while (total < (int)max_resp - 1 && (n = recv(sock, response + total, max_resp - total - 1, 0)) > 0) total += n;
            response[total] = '\0';
        }
        close(sock);

        if (!follow_redirects) return 0;

        int status = 0;
        sscanf(response, "HTTP/%*s %d", &status);
        if (status == 301 || status == 302 || status == 307 || status == 308) {
            char *loc = strcasestr(response, "\nLocation: ");
            if (!loc) loc = strcasestr(response, "\r\nLocation: ");
            if (loc) {
                loc += (loc[0] == '\r' ? 2 : 1) + 10;
                char *end = strstr(loc, "\r\n");
                if (!end) end = strstr(loc, "\n");
                if (end) *end = '\0';

                URL url;
                parse_url(loc, &url);
                strncpy(current_host, url.host, 255);
                current_port = url.port;
                current_ssl = url.is_ssl;
                snprintf(current_req, MAX_BUF, "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", url.path, url.host);
                current_len = strlen(current_req);
                continue;
            }
        }
        break;
    }
    return 0;
}

/* ── Parse Response ── */
static void parse_response(lua_State *L, const char *raw) {
    lua_newtable(L);
    const char *body = strstr(raw, "\r\n\r\n");
    if (!body) body = strstr(raw, "\n\n");

    if (body) {
        if (body[0] == '\r') body += 4; else body += 2;
        int status = 0;
        char reason[128] = "";
        sscanf(raw, "HTTP/%*s %d %127[^\r\n]", &status, reason);
        lua_pushinteger(L, status); lua_setfield(L, -2, "status_code");
        lua_pushstring(L, reason); lua_setfield(L, -2, "reason");
        lua_pushboolean(L, status >= 200 && status < 300); lua_setfield(L, -2, "ok");
        lua_pushstring(L, body); lua_setfield(L, -2, "text");

        lua_newtable(L);
        const char *hs = strstr(raw, "\r\n");
        if (hs) {
            hs += 2;
            const char *he = strstr(raw, "\r\n\r\n");
            while (hs && he && hs < he) {
                const char *colon = strchr(hs, ':');
                if (colon && colon < he) {
                    char key[128] = "";
                    strncpy(key, hs, colon - hs);
                    const char *val = colon + 1;
                    while (*val == ' ') val++;
                    const char *crlf = strstr(val, "\r\n");
                    if (!crlf) crlf = he;
                    char value[4096] = "";
                    strncpy(value, val, crlf - val);
                    lua_pushstring(L, value); lua_setfield(L, -2, key);
                    hs = crlf + 2;
                } else break;
            }
        }
        lua_setfield(L, -2, "headers");

        lua_getfield(L, -1, "headers");
        lua_getfield(L, -1, "Content-Type");
        const char *ct = lua_tostring(L, -1);
        const char *enc = "utf-8";
        if (ct) { char *cs = strstr(ct, "charset="); if (cs) enc = cs + 8; }
        lua_pushstring(L, enc); lua_setfield(L, -3, "encoding");
        lua_pop(L, 2);

        if (ct && strstr(ct, "application/json")) {
            lua_getglobal(L, "json");
            if (lua_istable(L, -1)) {
                lua_getfield(L, -1, "decode");
                lua_pushstring(L, body);
                if (lua_pcall(L, 1, 1, 0) == LUA_OK) lua_setfield(L, -3, "json");
                else lua_pop(L, 1);
            } else lua_pop(L, 1);
        }
    } else {
        lua_pushinteger(L, 0); lua_setfield(L, -2, "status_code");
        lua_pushstring(L, raw); lua_setfield(L, -2, "text");
        lua_pushboolean(L, 0); lua_setfield(L, -2, "ok");
    }
}

/* ── Execute ── */
static int execute(lua_State *L, const char *method, const char *url_str, Session *s, int opts_idx) {
    URL url;
    parse_url(url_str, &url);
    size_t req_len;
    char *req = build_request(method, url_str, s, opts_idx, L, &req_len);
    int timeout = s ? s->timeout : 30;
    int follow = 1;
    if (opts_idx > 0 && lua_istable(L, opts_idx)) {
        lua_getfield(L, opts_idx, "timeout"); if (lua_isnumber(L, -1)) timeout = lua_tointeger(L, -1); lua_pop(L, 1);
        lua_getfield(L, opts_idx, "allow_redirects"); if (lua_isboolean(L, -1)) follow = lua_toboolean(L, -1); lua_pop(L, 1);
    }
    char *response = malloc(MAX_BUF);
    if (!response) return luaL_error(L, "memory");
    int ret = send_recv(url.host, url.port, url.is_ssl, req, req_len, response, MAX_BUF, timeout, follow);
    if (ret < 0) { free(response); lua_pushnil(L); lua_pushstring(L, "Connection failed"); return 2; }
    parse_response(L, response);
    free(response);
    return 1;
}

/* ── Lua API ── */
static int l_request(lua_State *L) {
    return execute(L, luaL_checkstring(L,1), luaL_checkstring(L,2), NULL, lua_istable(L,3)?3:0);
}
#define SH(n,m) static int l_##n(lua_State *L) { lua_pushstring(L,m); lua_insert(L,1); return l_request(L); }
SH(get,"GET") SH(post,"POST") SH(put,"PUT") SH(delete,"DELETE") SH(patch,"PATCH") SH(head,"HEAD") SH(options,"OPTIONS")

static int l_download(lua_State *L) {
    const char *url_str = luaL_checkstring(L, 1);
    const char *path = luaL_checkstring(L, 2);
    int timeout = luaL_optinteger(L, 3, 60);
    URL url; parse_url(url_str, &url);
    char req[2048];
    snprintf(req, sizeof(req), "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", url.path, url.host);
    char *resp = malloc(MAX_BUF);
    if (!resp) return luaL_error(L, "memory");
    if (send_recv(url.host, url.port, url.is_ssl, req, strlen(req), resp, MAX_BUF, timeout, 1) < 0) {
        free(resp); lua_pushboolean(L, 0); return 1;
    }
    const char *body = strstr(resp, "\r\n\r\n");
    if (!body) body = resp;
    else { if (body[0]=='\r') body+=4; else body+=2; }
    FILE *f = fopen(path, "wb");
    if (!f) { free(resp); lua_pushboolean(L, 0); return 1; }
    fwrite(body, 1, strlen(body), f);
    fclose(f);
    free(resp);
    lua_pushboolean(L, 1); return 1;
}

/* ── Session ── */
static int l_session_new(lua_State *L) {
    Session *s = lua_newuserdata(L, sizeof(Session));
    memset(s, 0, sizeof(Session));
    s->timeout = 30; s->follow_redirects = 1; s->max_redirects = 10; s->verify_ssl = 1;
    strcpy(s->user_agent, "Byte-HttpX/6.1");
    luaL_getmetatable(L, "httpx_sess"); lua_setmetatable(L, -2); return 1;
}
static int l_session_set(lua_State *L) {
    Session *s = luaL_checkudata(L,1,"httpx_sess");
    const char *k = luaL_checkstring(L,2);
    if (!strcmp(k,"base_url")) strncpy(s->base_url,luaL_checkstring(L,3),1023);
    else if (!strcmp(k,"headers")) strncpy(s->headers,luaL_checkstring(L,3),4095);
    else if (!strcmp(k,"cookies")) strncpy(s->cookies,luaL_checkstring(L,3),4095);
    else if (!strcmp(k,"timeout")) s->timeout=luaL_checkinteger(L,3);
    else if (!strcmp(k,"follow_redirects")) s->follow_redirects=lua_toboolean(L,3);
    else if (!strcmp(k,"verify_ssl")) s->verify_ssl=lua_toboolean(L,3);
    else if (!strcmp(k,"user_agent")) strncpy(s->user_agent,luaL_checkstring(L,3),255);
    else if (!strcmp(k,"auth")) {
        if (lua_istable(L,3)) {
            lua_getfield(L,3,"type"); strncpy(s->auth_type,lua_tostring(L,-1)?:"",31); lua_pop(L,1);
            lua_getfield(L,3,"user"); strncpy(s->auth_user,lua_tostring(L,-1)?:"",255); lua_pop(L,1);
            lua_getfield(L,3,"pass"); strncpy(s->auth_pass,lua_tostring(L,-1)?:"",255); lua_pop(L,1);
        }
    }
    return 0;
}
static int l_session_req(lua_State *L) {
    return execute(L, luaL_checkstring(L,2), luaL_checkstring(L,3),
                   luaL_checkudata(L,1,"httpx_sess"), lua_istable(L,4)?4:0);
}

static const luaL_Reg sm[] = {
    {"set",l_session_set},{"request",l_session_req},{"get",l_session_req},{"post",l_session_req},
    {"put",l_session_req},{"delete",l_session_req},{"patch",l_session_req},{"head",l_session_req},
    {"options",l_session_req},{NULL,NULL}
};

int luaopen_httpx(lua_State *L) {
    luaL_newmetatable(L, "httpx_sess");
    lua_pushvalue(L,-1); lua_setfield(L,-2,"__index");
    luaL_setfuncs(L,sm,0); lua_pop(L,1);
    lua_newtable(L);
    lua_pushcfunction(L,l_request); lua_setfield(L,-2,"request");
    lua_pushcfunction(L,l_get); lua_setfield(L,-2,"get");
    lua_pushcfunction(L,l_post); lua_setfield(L,-2,"post");
    lua_pushcfunction(L,l_put); lua_setfield(L,-2,"put");
    lua_pushcfunction(L,l_delete); lua_setfield(L,-2,"delete");
    lua_pushcfunction(L,l_patch); lua_setfield(L,-2,"patch");
    lua_pushcfunction(L,l_head); lua_setfield(L,-2,"head");
    lua_pushcfunction(L,l_options); lua_setfield(L,-2,"options");
    lua_pushcfunction(L,l_download); lua_setfield(L,-2,"download");
    lua_pushcfunction(L,l_session_new); lua_setfield(L,-2,"session");
    return 1;
}
