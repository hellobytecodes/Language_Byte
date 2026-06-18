// gcc -shared -fPIC -I../../../src -o ../portscanner.so portscanner.c \ L../../../src -llua -lm
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "../../../src/lua.h"
#include "../../../src/lauxlib.h"
#include "../../../src/lualib.h"

#define DEFAULT_TIMEOUT 1

/*
=====================================================
TCP CONNECT SCAN (اصلاح‌شده)
=====================================================
*/

static int connect_scan(const char *host, int port, int timeout) {
    int sock;
    struct sockaddr_in addr;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return 0;

    // Set non-blocking
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1) {
        close(sock);
        return 0;
    }
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    // Resolve host
    struct hostent *he = gethostbyname(host);
    if (!he) {
        close(sock);
        return 0;
    }

    struct in_addr **addr_list = (struct in_addr **)he->h_addr_list;
    if (!addr_list[0]) {
        close(sock);
        return 0;
    }

    addr.sin_addr = *addr_list[0];

    // Connect (non-blocking)
    int result = connect(sock, (struct sockaddr*)&addr, sizeof(addr));

    if (result < 0) {
        if (errno != EINPROGRESS) {
            close(sock);
            return 0;  // Connection failed immediately
        }
    }

    // Wait for connection with select()
    fd_set fdset;
    struct timeval tv;
    FD_ZERO(&fdset);
    FD_SET(sock, &fdset);
    tv.tv_sec = timeout;
    tv.tv_usec = 0;

    result = select(sock + 1, NULL, &fdset, NULL, &tv);

    if (result == 1) {
        int so_error;
        socklen_t len = sizeof(so_error);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
        close(sock);
        return (so_error == 0);  // true if no error
    }

    // Timeout (result == 0) or error (result == -1)
    close(sock);
    return 0;
}

/*
=====================================================
SERVICE DETECTION (لیست کامل و استاندارد)
=====================================================
*/

static const char* detect_service(int port) {
    switch (port) {
        case 7:    return "echo";
        case 9:    return "discard";
        case 11:   return "systat";
        case 13:   return "daytime";
        case 15:   return "netstat";
        case 17:   return "qotd";
        case 18:   return "msp";
        case 19:   return "chargen";
        case 20:   return "ftp-data";
        case 21:   return "ftp";
        case 22:   return "ssh";
        case 23:   return "telnet";
        case 25:   return "smtp";
        case 37:   return "time";
        case 42:   return "nameserver";
        case 43:   return "whois";
        case 49:   return "tacacs";
        case 50:   return "re-mail-ck";
        case 53:   return "dns";
        case 67:
        case 68:   return "dhcp";
        case 69:   return "tftp";
        case 80:   return "http";
        case 88:   return "kerberos";
        case 110:  return "pop3";
        case 111:  return "rpcbind";
        case 113:  return "ident";
        case 119:  return "nntp";
        case 123:  return "ntp";
        case 135:  return "msrpc";
        case 137:
        case 138:
        case 139:  return "netbios";
        case 143:  return "imap";
        case 161:
        case 162:  return "snmp";
        case 179:  return "bgp";
        case 194:  return "irc";
        case 389:  return "ldap";
        case 443:  return "https";
        case 445:  return "smb";
        case 465:  return "smtps";
        case 514:  return "syslog";
        case 515:  return "printer";
        case 554:  return "rtsp";
        case 587:  return "submission";
        case 631:  return "ipp";
        case 636:  return "ldaps";
        case 873:  return "rsync";
        case 993:  return "imaps";
        case 995:  return "pop3s";
        case 1080: return "socks5";
        case 1194: return "openvpn";
        case 1433: return "mssql";
        case 1521: return "oracle";
        case 1723: return "pptp";
        case 1883: return "mqtt";
        case 2049: return "nfs";
        case 2082:
        case 2083: return "cpanel";
        case 2181: return "zookeeper";
        case 2375:
        case 2376: return "docker";
        case 3128: return "squid";
        case 3306: return "mysql";
        case 3389: return "rdp";
        case 3690: return "svn";
        case 4444: return "metasploit";
        case 4505:
        case 4506: return "saltstack";
        case 4567: return "sinatra";
        case 5000: return "flask";
        case 5060: return "sip";
        case 5222: return "xmpp";
        case 5269: return "xmpp-server";
        case 5353: return "mdns";
        case 5432: return "postgresql";
        case 5672: return "rabbitmq";
        case 5900: return "vnc";
        case 5984: return "couchdb";
        case 6379: return "redis";
        case 6443: return "k8s-api";
        case 6667: return "ircd";
        case 7474: return "neo4j";
        case 7547: return "cwmp";
        case 8000: return "http-alt";
        case 8080: return "http-proxy";
        case 8443: return "https-alt";
        case 8888: return "http-alt2";
        case 9000: return "php-fpm";
        case 9090: return "prometheus";
        case 9200: return "elasticsearch";
        case 9300: return "elasticsearch-node";
        case 11211: return "memcached";
        case 27017: return "mongodb";
        case 27018: return "mongodb-shard";
        case 28017: return "mongodb-web";
        case 50070: return "hadoop-nn";
        default:    return "unknown";
    }
}

/*
=====================================================
portscanner.scan(host, start_port, end_port, timeout?)
=====================================================
*/

static int l_scan(lua_State *L) {
    const char *host = luaL_checkstring(L, 1);
    int start = luaL_checkinteger(L, 2);
    int end = luaL_checkinteger(L, 3);
    int timeout = luaL_optinteger(L, 4, DEFAULT_TIMEOUT);

    lua_newtable(L);
    int idx = 1;

    printf("\n[*] Scanning %s (ports %d-%d)...\n\n", host, start, end);

    for (int port = start; port <= end; port++) {
        if (connect_scan(host, port, timeout)) {
            // Push index
            lua_pushinteger(L, idx++);

            // Create result table
            lua_newtable(L);

            // port
            lua_pushstring(L, "port");
            lua_pushinteger(L, port);
            lua_settable(L, -3);

            // service
            lua_pushstring(L, "service");
            lua_pushstring(L, detect_service(port));
            lua_settable(L, -3);

            // state
            lua_pushstring(L, "state");
            lua_pushstring(L, "open");
            lua_settable(L, -3);

            // Insert into results table
            lua_settable(L, -3);

            printf("  [OPEN] %d - %s\n", port, detect_service(port));
        }
    }

    printf("\n[*] Scan complete. %d ports open.\n", idx - 1);
    return 1;
}

/*
=====================================================
portscanner.isopen(host, port)
=====================================================
*/

static int l_isopen(lua_State *L) {
    const char *host = luaL_checkstring(L, 1);
    int port = luaL_checkinteger(L, 2);

    int result = connect_scan(host, port, DEFAULT_TIMEOUT);
    lua_pushboolean(L, result);

    return 1;
}

/*
=====================================================
portscanner.service(port)
=====================================================
*/

static int l_service(lua_State *L) {
    int port = luaL_checkinteger(L, 1);
    lua_pushstring(L, detect_service(port));
    return 1;
}

/*
=====================================================
portscanner.resolve(host)
=====================================================
*/

static int l_resolve(lua_State *L) {
    const char *host = luaL_checkstring(L, 1);

    struct hostent *he = gethostbyname(host);
    if (!he) {
        lua_pushnil(L);
        return 1;
    }

    struct in_addr **addr_list = (struct in_addr **)he->h_addr_list;
    if (addr_list[0]) {
        lua_pushstring(L, inet_ntoa(*addr_list[0]));
        return 1;
    }

    lua_pushnil(L);
    return 1;
}

/*
=====================================================
portscanner.banner()
=====================================================
*/

static int l_banner(lua_State *L) {
    lua_pushstring(L,
        "Byte Advanced PortScanner v2.1\n"
        "Features: TCP Connect Scan, Service Detection, DNS Resolve"
    );
    return 1;
}

/*
=====================================================
FUNCTION TABLE
=====================================================
*/

static const luaL_Reg portscannerlib[] = {
    {"scan",    l_scan},
    {"isopen",  l_isopen},
    {"service", l_service},
    {"resolve", l_resolve},
    {"banner",  l_banner},
    {NULL, NULL}
};

/*
=====================================================
EXPORT
=====================================================
*/

int luaopen_portscanner(lua_State *L) {
    luaL_newlib(L, portscannerlib);
    return 1;
}
