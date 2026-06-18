/*
 * netsocket.c – Complete Network Library for Byte (v5.0.1)
 * تمام قابلیت‌های TCP/UDP/SSL/HTTP/DNS + Raw Socket + ARP Spoofing
 * کامپایل: gcc -shared -fPIC -o netsocket.so source_libs/netsocket.c -I../../src -lssl -lcrypto
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/select.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <endian.h>

#include <linux/if_packet.h>
#include <netpacket/packet.h>

#ifndef ETH_P_ALL
#define ETH_P_ALL 0x0003
#endif
#ifndef ETH_P_ARP
#define ETH_P_ARP 0x0806
#endif
#ifndef ETH_ALEN
#define ETH_ALEN 6
#endif

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include <openssl/ssl.h>
#include <openssl/err.h>

#define MAX_BUF 65536
#define DEFAULT_TIMEOUT 10

/* ========== Utility ========== */
static void push_error(lua_State *L, const char *msg) { lua_pushnil(L); lua_pushstring(L, msg); }

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int set_sock_timeout(int fd, int sec) {
    struct timeval tv;
    tv.tv_sec = sec; tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    return 0;
}

static int wait_connect(int fd, int sec) {
    fd_set wfds; struct timeval tv;
    FD_ZERO(&wfds); FD_SET(fd, &wfds);
    tv.tv_sec = sec; tv.tv_usec = 0;
    if (select(fd+1, NULL, &wfds, NULL, &tv) <= 0) return -1;
    int err; socklen_t len = sizeof(err);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
    if (err) { errno = err; return -1; }
    return 0;
}

static void set_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    struct timeval tv = {0, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

/* ========== TCP ========== */
typedef struct { int fd; int timeout; int blocking; int is_server; SSL *ssl; SSL_CTX *ctx; } tcp_t;

static int tcp_connect(lua_State *L) {
    tcp_t *t = luaL_checkudata(L, 1, "tcp");
    const char *host = luaL_checkstring(L, 2);
    int port = luaL_checkinteger(L, 3);
    int to = luaL_optinteger(L, 4, t->timeout);
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    char ps[8]; snprintf(ps, sizeof(ps), "%d", port);
    if (getaddrinfo(host, ps, &hints, &res)) { push_error(L, "dns error"); return 2; }
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); push_error(L, "socket error"); return 2; }
    if (to > 0) set_sock_timeout(fd, to);
    set_nonblocking(fd);
    int ret = connect(fd, res->ai_addr, res->ai_addrlen);
    if (ret < 0 && errno != EINPROGRESS) { close(fd); freeaddrinfo(res); push_error(L, "connect error"); return 2; }
    if (errno == EINPROGRESS && wait_connect(fd, to) < 0) { close(fd); freeaddrinfo(res); push_error(L, "timeout"); return 2; }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);
    freeaddrinfo(res);
    if (t->fd >= 0) close(t->fd);
    t->fd = fd; t->is_server = 0;
    lua_pushboolean(L, 1);
    return 1;
}

static int tcp_send(lua_State *L) { tcp_t *t=luaL_checkudata(L,1,"tcp"); size_t len; const char *data=luaL_checklstring(L,2,&len); int n=t->ssl?SSL_write(t->ssl,data,len):send(t->fd,data,len,0); if(n<0){push_error(L,"send error");return 2;} lua_pushinteger(L,n); return 1; }
static int tcp_sendall(lua_State *L) { tcp_t *t=luaL_checkudata(L,1,"tcp"); size_t len; const char *data=luaL_checklstring(L,2,&len); size_t sent=0; while(sent<len){int n=t->ssl?SSL_write(t->ssl,data+sent,len-sent):send(t->fd,data+sent,len-sent,0); if(n<=0){push_error(L,"sendall error");return 2;} sent+=n;} lua_pushboolean(L,1); return 1; }
static int tcp_recv(lua_State *L) { tcp_t *t=luaL_checkudata(L,1,"tcp"); const char *pat=luaL_optstring(L,2,"*l"); char buf[MAX_BUF]; int n; if(!strcmp(pat,"*a")){luaL_Buffer B; luaL_buffinit(L,&B); while(1){n=t->ssl?SSL_read(t->ssl,buf,sizeof(buf)):recv(t->fd,buf,sizeof(buf),0); if(n<=0)break; luaL_addlstring(&B,buf,n);} luaL_pushresult(&B); return 1;} else if(!strcmp(pat,"*l")){luaL_Buffer B; luaL_buffinit(L,&B); char c; while(1){n=t->ssl?SSL_read(t->ssl,&c,1):recv(t->fd,&c,1,0); if(n<=0)break; if(c=='\n')break; if(c!='\r')luaL_addchar(&B,c);} luaL_pushresult(&B); return 1;} else {int size=atoi(pat); if(size<=0)size=1; n=t->ssl?SSL_read(t->ssl,buf,size):recv(t->fd,buf,size,0); if(n<=0){push_error(L,"recv error");return 2;} lua_pushlstring(L,buf,n); return 1;} }
static int tcp_close(lua_State *L) { tcp_t *t=luaL_checkudata(L,1,"tcp"); if(t->ssl){SSL_shutdown(t->ssl);SSL_free(t->ssl);t->ssl=NULL;} if(t->ctx){SSL_CTX_free(t->ctx);t->ctx=NULL;} if(t->fd>=0){close(t->fd);t->fd=-1;} return 0; }
static int tcp_bind(lua_State *L) { tcp_t *t=luaL_checkudata(L,1,"tcp"); const char *host=luaL_optstring(L,2,"0.0.0.0"); int port=luaL_checkinteger(L,3); struct addrinfo hints,*res; memset(&hints,0,sizeof(hints)); hints.ai_family=AF_INET; hints.ai_socktype=SOCK_STREAM; hints.ai_flags=AI_PASSIVE; char ps[8]; snprintf(ps,sizeof(ps),"%d",port); if(getaddrinfo(host,ps,&hints,&res)){push_error(L,"bind error");return 2;} int fd=socket(res->ai_family,res->ai_socktype,res->ai_protocol); if(fd<0){freeaddrinfo(res);push_error(L,"socket error");return 2;} int opt=1; setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt)); if(bind(fd,res->ai_addr,res->ai_addrlen)<0){close(fd);freeaddrinfo(res);push_error(L,"bind failed");return 2;} freeaddrinfo(res); if(t->fd>=0)close(t->fd); t->fd=fd; t->is_server=1; lua_pushboolean(L,1); return 1; }
static int tcp_listen(lua_State *L) { tcp_t *t=luaL_checkudata(L,1,"tcp"); int backlog=luaL_optinteger(L,2,5); if(listen(t->fd,backlog)<0){push_error(L,"listen error");return 2;} set_blocking(t->fd); t->blocking=1; t->timeout=0; lua_pushboolean(L,1); return 1; }
static int tcp_accept(lua_State *L) { tcp_t *t=luaL_checkudata(L,1,"tcp"); struct sockaddr_in addr; socklen_t len=sizeof(addr); int old_flags=fcntl(t->fd,F_GETFL,0); fcntl(t->fd,F_SETFL,old_flags&~O_NONBLOCK); int fd=accept(t->fd,(struct sockaddr*)&addr,&len); if(fd<0){fcntl(t->fd,F_SETFL,old_flags); push_error(L,"accept error");return 2;} fcntl(t->fd,F_SETFL,old_flags); tcp_t *nt=lua_newuserdata(L,sizeof(tcp_t)); memset(nt,0,sizeof(tcp_t)); nt->fd=fd; nt->timeout=t->timeout; nt->blocking=t->blocking; nt->is_server=0; luaL_getmetatable(L,"tcp"); lua_setmetatable(L,-2); lua_pushstring(L,inet_ntoa(addr.sin_addr)); return 2; }
static int tcp_settimeout(lua_State *L) { tcp_t *t=luaL_checkudata(L,1,"tcp"); t->timeout=luaL_checkinteger(L,2); set_sock_timeout(t->fd,t->timeout); return 0; }
static int tcp_starttls(lua_State *L) { tcp_t *t=luaL_checkudata(L,1,"tcp"); SSL_library_init(); SSL_load_error_strings(); SSL_CTX *ctx=SSL_CTX_new(t->is_server?TLS_server_method():TLS_client_method()); SSL *ssl=SSL_new(ctx); SSL_set_fd(ssl,t->fd); int ret=t->is_server?SSL_accept(ssl):SSL_connect(ssl); if(ret!=1){SSL_free(ssl);SSL_CTX_free(ctx); push_error(L,"tls error");return 2;} t->ssl=ssl; t->ctx=ctx; lua_pushboolean(L,1); return 1; }
static int tcp_getpeername(lua_State *L) { tcp_t *t=luaL_checkudata(L,1,"tcp"); struct sockaddr_in addr; socklen_t len=sizeof(addr); if(getpeername(t->fd,(struct sockaddr*)&addr,&len)<0){push_error(L,"error");return 2;} lua_pushstring(L,inet_ntoa(addr.sin_addr)); lua_pushinteger(L,ntohs(addr.sin_port)); return 2; }
static int tcp_getsockname(lua_State *L) { tcp_t *t=luaL_checkudata(L,1,"tcp"); struct sockaddr_in addr; socklen_t len=sizeof(addr); if(getsockname(t->fd,(struct sockaddr*)&addr,&len)<0){push_error(L,"error");return 2;} lua_pushstring(L,inet_ntoa(addr.sin_addr)); lua_pushinteger(L,ntohs(addr.sin_port)); return 2; }
static int tcp_setoption(lua_State *L) { tcp_t *t=luaL_checkudata(L,1,"tcp"); const char *opt=luaL_checkstring(L,2); int val=luaL_checkinteger(L,3); int level=SOL_SOCKET,optname=0; if(!strcmp(opt,"reuseaddr"))optname=SO_REUSEADDR; else if(!strcmp(opt,"keepalive"))optname=SO_KEEPALIVE; else if(!strcmp(opt,"nodelay")){level=IPPROTO_TCP;optname=TCP_NODELAY;} else {push_error(L,"unknown");return 2;} if(setsockopt(t->fd,level,optname,&val,sizeof(val))<0){push_error(L,"error");return 2;} lua_pushboolean(L,1); return 1; }
static int tcp_getoption(lua_State *L) { tcp_t *t=luaL_checkudata(L,1,"tcp"); const char *opt=luaL_checkstring(L,2); int level=SOL_SOCKET,optname=0; if(!strcmp(opt,"reuseaddr"))optname=SO_REUSEADDR; else if(!strcmp(opt,"keepalive"))optname=SO_KEEPALIVE; else if(!strcmp(opt,"nodelay")){level=IPPROTO_TCP;optname=TCP_NODELAY;} else {push_error(L,"unknown");return 2;} int val; socklen_t len=sizeof(val); if(getsockopt(t->fd,level,optname,&val,&len)<0){push_error(L,"error");return 2;} lua_pushinteger(L,val); return 1; }
static int tcp_shutdown(lua_State *L) { tcp_t *t=luaL_checkudata(L,1,"tcp"); shutdown(t->fd,luaL_optinteger(L,2,2)); return 0; }
static int tcp_gettimeout(lua_State *L) { tcp_t *t=luaL_checkudata(L,1,"tcp"); lua_pushinteger(L,t->timeout); return 1; }
static int tcp_setblocking(lua_State *L) { tcp_t *t=luaL_checkudata(L,1,"tcp"); t->blocking=lua_toboolean(L,2); if(t->blocking)set_sock_timeout(t->fd,0); else if(t->timeout>0)set_sock_timeout(t->fd,t->timeout); return 0; }
static int tcp_sendfile(lua_State *L) { tcp_t *t=luaL_checkudata(L,1,"tcp"); const char *fp=luaL_checkstring(L,2); FILE *f=fopen(fp,"rb"); if(!f){push_error(L,"file error");return 2;} char buf[8192]; size_t n; while((n=fread(buf,1,sizeof(buf),f))>0){int s=t->ssl?SSL_write(t->ssl,buf,n):send(t->fd,buf,n,0); if(s<=0){fclose(f);push_error(L,"sendfile error");return 2;} if((size_t)s<n)fseek(f,(long)(s-n),SEEK_CUR);} fclose(f); lua_pushboolean(L,1); return 1; }
static int tcp_gc(lua_State *L) { tcp_close(L); return 0; }

/* ========== UDP ========== */
typedef struct { int fd; int timeout; int blocking; } udp_t;
static int udp_bind(lua_State *L) { udp_t *u=luaL_checkudata(L,1,"udp"); const char *host=luaL_optstring(L,2,"0.0.0.0"); int port=luaL_checkinteger(L,3); struct addrinfo hints,*res; memset(&hints,0,sizeof(hints)); hints.ai_family=AF_INET; hints.ai_socktype=SOCK_DGRAM; hints.ai_flags=AI_PASSIVE; char ps[8]; snprintf(ps,sizeof(ps),"%d",port); if(getaddrinfo(host,ps,&hints,&res)){push_error(L,"bind error");return 2;} int fd=socket(res->ai_family,res->ai_socktype,res->ai_protocol); if(fd<0){freeaddrinfo(res);push_error(L,"socket error");return 2;} if(bind(fd,res->ai_addr,res->ai_addrlen)<0){close(fd);freeaddrinfo(res);push_error(L,"bind failed");return 2;} freeaddrinfo(res); u->fd=fd; lua_pushboolean(L,1); return 1; }
static int udp_sendto(lua_State *L) { udp_t *u=luaL_checkudata(L,1,"udp"); size_t len; const char *data=luaL_checklstring(L,2,&len); const char *host=luaL_checkstring(L,3); int port=luaL_checkinteger(L,4); struct addrinfo hints,*res; memset(&hints,0,sizeof(hints)); hints.ai_family=AF_INET; hints.ai_socktype=SOCK_DGRAM; char ps[8]; snprintf(ps,sizeof(ps),"%d",port); if(getaddrinfo(host,ps,&hints,&res)){push_error(L,"dns error");return 2;} int n=sendto(u->fd,data,len,0,res->ai_addr,res->ai_addrlen); freeaddrinfo(res); if(n<0){push_error(L,"sendto error");return 2;} lua_pushinteger(L,n); return 1; }
static int udp_recvfrom(lua_State *L) { udp_t *u=luaL_checkudata(L,1,"udp"); int size=luaL_optinteger(L,2,1024); char *buf=malloc(size); struct sockaddr_in addr; socklen_t len=sizeof(addr); int n=recvfrom(u->fd,buf,size,0,(struct sockaddr*)&addr,&len); if(n<0){free(buf);push_error(L,"recvfrom error");return 2;} lua_pushlstring(L,buf,n); lua_pushstring(L,inet_ntoa(addr.sin_addr)); lua_pushinteger(L,ntohs(addr.sin_port)); free(buf); return 3; }
static int udp_close(lua_State *L) { udp_t *u=luaL_checkudata(L,1,"udp"); if(u->fd>=0){close(u->fd);u->fd=-1;} return 0; }
static int udp_settimeout(lua_State *L) { udp_t *u=luaL_checkudata(L,1,"udp"); u->timeout=luaL_checkinteger(L,2); set_sock_timeout(u->fd,u->timeout); return 0; }
static int udp_getsockname(lua_State *L) { udp_t *u=luaL_checkudata(L,1,"udp"); struct sockaddr_in addr; socklen_t len=sizeof(addr); if(getsockname(u->fd,(struct sockaddr*)&addr,&len)<0){push_error(L,"error");return 2;} lua_pushstring(L,inet_ntoa(addr.sin_addr)); lua_pushinteger(L,ntohs(addr.sin_port)); return 2; }
static int udp_setoption(lua_State *L) { udp_t *u=luaL_checkudata(L,1,"udp"); const char *opt=luaL_checkstring(L,2); int val=luaL_checkinteger(L,3); int optname=0; if(!strcmp(opt,"reuseaddr"))optname=SO_REUSEADDR; else if(!strcmp(opt,"broadcast"))optname=SO_BROADCAST; else {push_error(L,"unknown");return 2;} if(setsockopt(u->fd,SOL_SOCKET,optname,&val,sizeof(val))<0){push_error(L,"error");return 2;} lua_pushboolean(L,1); return 1; }
static int udp_getoption(lua_State *L) { udp_t *u=luaL_checkudata(L,1,"udp"); const char *opt=luaL_checkstring(L,2); int optname=0; if(!strcmp(opt,"reuseaddr"))optname=SO_REUSEADDR; else if(!strcmp(opt,"broadcast"))optname=SO_BROADCAST; else {push_error(L,"unknown");return 2;} int val; socklen_t len=sizeof(val); if(getsockopt(u->fd,SOL_SOCKET,optname,&val,&len)<0){push_error(L,"error");return 2;} lua_pushinteger(L,val); return 1; }
static int udp_gettimeout(lua_State *L) { udp_t *u=luaL_checkudata(L,1,"udp"); lua_pushinteger(L,u->timeout); return 1; }
static int udp_setblocking(lua_State *L) { udp_t *u=luaL_checkudata(L,1,"udp"); u->blocking=lua_toboolean(L,2); if(u->blocking)set_sock_timeout(u->fd,0); else if(u->timeout>0)set_sock_timeout(u->fd,u->timeout); return 0; }
static int udp_gc(lua_State *L) { return udp_close(L); }

/* ========== Raw Socket ========== */
static int l_raw_socket(lua_State *L) {
    int family = luaL_optinteger(L, 1, AF_PACKET);
    int type = luaL_optinteger(L, 2, SOCK_RAW);
    int proto = luaL_optinteger(L, 3, htons(ETH_P_ALL));
    int fd = socket(family, type, proto);
    if (fd < 0) { push_error(L, "raw socket failed (need root?)"); return 2; }
    lua_pushinteger(L, fd);
    return 1;
}
static int l_raw_recv(lua_State *L) { int fd=luaL_checkinteger(L,1); int size=luaL_optinteger(L,2,65536); char *buf=malloc(size); int n=recv(fd,buf,size,0); if(n<0){free(buf);push_error(L,"recv error");return 2;} lua_pushlstring(L,buf,n); free(buf); return 1; }
static int l_raw_send(lua_State *L) { int fd=luaL_checkinteger(L,1); size_t len; const char *data=luaL_checklstring(L,2,&len); int n=send(fd,data,len,0); if(n<0){push_error(L,"send error");return 2;} lua_pushinteger(L,n); return 1; }
static int l_raw_close(lua_State *L) { int fd=luaL_checkinteger(L,1); close(fd); return 0; }
static int l_raw_bind(lua_State *L) { int fd=luaL_checkinteger(L,1); const char *iface=luaL_checkstring(L,2); struct ifreq ifr; memset(&ifr,0,sizeof(ifr)); strncpy(ifr.ifr_name,iface,IFNAMSIZ-1); if(setsockopt(fd,SOL_SOCKET,SO_BINDTODEVICE,&ifr,sizeof(ifr))<0){push_error(L,"bind error");return 2;} lua_pushboolean(L,1); return 1; }

/* ========== ARP Spoofing ========== */
static int l_arp_spoof(lua_State *L) {
    const char *iface = luaL_checkstring(L, 1);
    const char *target_ip = luaL_checkstring(L, 2);
    const char *spoof_ip = luaL_checkstring(L, 3);
    int fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ARP));
    if (fd < 0) { push_error(L, "raw socket failed (need root)"); return 2; }
    struct ifreq ifr; memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, iface, IFNAMSIZ-1);
    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) { close(fd); push_error(L, "ioctl error"); return 2; }
    if (ioctl(fd, SIOCGIFHWADDR, &ifr) < 0) { close(fd); push_error(L, "ioctl mac error"); return 2; }
    struct sockaddr_ll sa; memset(&sa, 0, sizeof(sa));
    sa.sll_family = AF_PACKET; sa.sll_ifindex = ifr.ifr_ifindex;
    sa.sll_halen = ETH_ALEN; memcpy(sa.sll_addr, ifr.ifr_hwaddr.sa_data, ETH_ALEN);
    unsigned char packet[42]; memset(packet, 0, sizeof(packet));
    memset(packet, 0xff, 6); memcpy(packet+6, ifr.ifr_hwaddr.sa_data, 6);
    packet[12] = 0x08; packet[13] = 0x06;
    packet[14] = 0x00; packet[15] = 0x01; packet[16] = 0x08; packet[17] = 0x00;
    packet[18] = 6; packet[19] = 4; packet[20] = 0x00; packet[21] = 0x02;
    memcpy(packet+22, ifr.ifr_hwaddr.sa_data, 6);
    inet_pton(AF_INET, spoof_ip, packet+28);
    memset(packet+32, 0xff, 6);
    inet_pton(AF_INET, target_ip, packet+38);
    int n = sendto(fd, packet, sizeof(packet), 0, (struct sockaddr*)&sa, sizeof(sa));
    close(fd);
    if (n < 0) { push_error(L, "send error"); return 2; }
    lua_pushboolean(L, 1);
    return 1;
}

/* ========== توابع سراسری ========== */
static int l_dns(lua_State *L) { const char *h=luaL_checkstring(L,1); struct addrinfo hints,*res; memset(&hints,0,sizeof(hints)); hints.ai_family=AF_INET; if(getaddrinfo(h,NULL,&hints,&res)){push_error(L,"dns error");return 2;} char ip[INET_ADDRSTRLEN]; inet_ntop(AF_INET,&((struct sockaddr_in*)res->ai_addr)->sin_addr,ip,sizeof(ip)); freeaddrinfo(res); lua_pushstring(L,ip); return 1; }
static int l_dns_all(lua_State *L) { const char *h=luaL_checkstring(L,1); struct addrinfo hints,*res; memset(&hints,0,sizeof(hints)); hints.ai_family=AF_INET; if(getaddrinfo(h,NULL,&hints,&res)){push_error(L,"dns error");return 2;} lua_newtable(L); int i=1; for(struct addrinfo *r=res;r;r=r->ai_next){char ip[INET_ADDRSTRLEN]; inet_ntop(AF_INET,&((struct sockaddr_in*)r->ai_addr)->sin_addr,ip,sizeof(ip)); lua_pushstring(L,ip); lua_rawseti(L,-2,i++);} freeaddrinfo(res); return 1; }
static int l_reverse_dns(lua_State *L) { const char *ip=luaL_checkstring(L,1); struct sockaddr_in sa; sa.sin_family=AF_INET; inet_pton(AF_INET,ip,&sa.sin_addr); char host[NI_MAXHOST]; if(getnameinfo((struct sockaddr*)&sa,sizeof(sa),host,sizeof(host),NULL,0,0)!=0){push_error(L,"reverse dns error");return 2;} lua_pushstring(L,host); return 1; }
static int l_scan(lua_State *L) { const char *h=luaL_checkstring(L,1); luaL_checktype(L,2,LUA_TTABLE); lua_newtable(L); int idx=1,port; lua_pushnil(L); while(lua_next(L,2)){port=lua_tointeger(L,-1); int fd=socket(AF_INET,SOCK_STREAM,0); if(fd<0){lua_pop(L,1);continue;} struct sockaddr_in addr; memset(&addr,0,sizeof(addr)); addr.sin_family=AF_INET; addr.sin_port=htons(port); inet_pton(AF_INET,h,&addr.sin_addr); set_nonblocking(fd); int ret=connect(fd,(struct sockaddr*)&addr,sizeof(addr)); if(ret<0&&errno!=EINPROGRESS){close(fd);lua_pop(L,1);continue;} if(errno==EINPROGRESS&&wait_connect(fd,1)<0){close(fd);lua_pop(L,1);continue;} lua_pushinteger(L,port); lua_rawseti(L,-3,idx++); close(fd); lua_pop(L,1);} return 1; }
static int l_scan_range(lua_State *L) { const char *h=luaL_checkstring(L,1); int s=luaL_checkinteger(L,2),e=luaL_checkinteger(L,3); lua_newtable(L); int idx=1; for(int p=s;p<=e;p++){int fd=socket(AF_INET,SOCK_STREAM,0); if(fd<0)continue; struct sockaddr_in addr; memset(&addr,0,sizeof(addr)); addr.sin_family=AF_INET; addr.sin_port=htons(p); inet_pton(AF_INET,h,&addr.sin_addr); set_nonblocking(fd); int ret=connect(fd,(struct sockaddr*)&addr,sizeof(addr)); if(ret<0&&errno!=EINPROGRESS){close(fd);continue;} if(errno==EINPROGRESS&&wait_connect(fd,1)<0){close(fd);continue;} lua_pushinteger(L,p); lua_rawseti(L,-2,idx++); close(fd);} return 1; }
static int l_http(lua_State *L) { const char *url=luaL_checkstring(L,1); char host[256]={0},path[1024]="/"; int port=80; sscanf(url,"http://%255[^:/]:%d/%1023[^\n]",host,&port,path); if(!host[0]) sscanf(url,"http://%255[^/]/%1023[^\n]",host,path); int fd=socket(AF_INET,SOCK_STREAM,0); if(fd<0){push_error(L,"socket error");return 2;} struct sockaddr_in addr; memset(&addr,0,sizeof(addr)); addr.sin_family=AF_INET; addr.sin_port=htons(port); struct hostent *he=gethostbyname(host); if(!he){close(fd);push_error(L,"dns error");return 2;} memcpy(&addr.sin_addr,he->h_addr_list[0],he->h_length); set_sock_timeout(fd,5); if(connect(fd,(struct sockaddr*)&addr,sizeof(addr))<0){close(fd);push_error(L,"connect error");return 2;} char req[1024]; snprintf(req,sizeof(req),"GET /%s HTTP/1.0\r\nHost: %s\r\n\r\n",path,host); send(fd,req,strlen(req),0); char buf[MAX_BUF]; int n=recv(fd,buf,sizeof(buf)-1,0); close(fd); if(n<=0){push_error(L,"no response");return 2;} buf[n]='\0'; char *body=strstr(buf,"\r\n\r\n"); body=body?body+4:""; int code=0; sscanf(buf,"HTTP/1.%*d %d",&code); lua_pushstring(L,body); lua_pushinteger(L,code); return 2; }
static int l_headers(lua_State *L) { const char *url=luaL_checkstring(L,1); char host[256]={0},path[1024]="/"; int port=80; sscanf(url,"http://%255[^:/]:%d/%1023[^\n]",host,&port,path); if(!host[0]) sscanf(url,"http://%255[^/]/%1023[^\n]",host,path); int fd=socket(AF_INET,SOCK_STREAM,0); if(fd<0){push_error(L,"socket error");return 2;} struct sockaddr_in addr; memset(&addr,0,sizeof(addr)); addr.sin_family=AF_INET; addr.sin_port=htons(port); struct hostent *he=gethostbyname(host); if(!he){close(fd);push_error(L,"dns error");return 2;} memcpy(&addr.sin_addr,he->h_addr_list[0],he->h_length); set_sock_timeout(fd,5); if(connect(fd,(struct sockaddr*)&addr,sizeof(addr))<0){close(fd);push_error(L,"connect error");return 2;} char req[1024]; snprintf(req,sizeof(req),"HEAD /%s HTTP/1.0\r\nHost: %s\r\n\r\n",path,host); send(fd,req,strlen(req),0); char buf[8192]; int n=recv(fd,buf,sizeof(buf)-1,0); close(fd); if(n<=0){push_error(L,"no response");return 2;} buf[n]='\0'; lua_newtable(L); char *line=strtok(buf,"\r\n"); while(line){char *colon=strchr(line,':'); if(colon){*colon='\0'; lua_pushstring(L,line); while(*(++colon)==' '); lua_pushstring(L,colon); lua_settable(L,-3);} line=strtok(NULL,"\r\n");} return 1; }
static int l_download(lua_State *L) { const char *url=luaL_checkstring(L,1),*sp=luaL_checkstring(L,2); char host[256]={0},path[1024]="/"; int port=80; sscanf(url,"http://%255[^:/]:%d/%1023[^\n]",host,&port,path); if(!host[0]) sscanf(url,"http://%255[^/]/%1023[^\n]",host,path); int fd=socket(AF_INET,SOCK_STREAM,0); if(fd<0){push_error(L,"socket error");return 2;} struct sockaddr_in addr; memset(&addr,0,sizeof(addr)); addr.sin_family=AF_INET; addr.sin_port=htons(port); struct hostent *he=gethostbyname(host); if(!he){close(fd);push_error(L,"dns error");return 2;} memcpy(&addr.sin_addr,he->h_addr_list[0],he->h_length); set_sock_timeout(fd,10); if(connect(fd,(struct sockaddr*)&addr,sizeof(addr))<0){close(fd);push_error(L,"connect error");return 2;} char req[1024]; snprintf(req,sizeof(req),"GET /%s HTTP/1.0\r\nHost: %s\r\n\r\n",path,host); send(fd,req,strlen(req),0); FILE *fp=fopen(sp,"wb"); if(!fp){close(fd);push_error(L,"file error");return 2;} char buf[8192]; int n,hd=0; while((n=recv(fd,buf,sizeof(buf),0))>0){if(!hd){char *end=strstr(buf,"\r\n\r\n"); if(end){hd=1; int hl=end-buf+4; fwrite(end+4,1,n-hl,fp);}} else fwrite(buf,1,n,fp);} fclose(fp); close(fd); lua_pushboolean(L,1); return 1; }
static int l_ping(lua_State *L) { const char *h=luaL_checkstring(L,1); int c=luaL_optinteger(L,2,1); char cmd[256]; snprintf(cmd,sizeof(cmd),"ping -c %d -W 2 %s 2>&1",c,h); FILE *fp=popen(cmd,"r"); if(!fp){push_error(L,"ping error");return 2;} char buf[MAX_BUF]; int n=fread(buf,1,sizeof(buf)-1,fp); pclose(fp); buf[n]='\0'; lua_pushstring(L,buf); return 1; }
static int l_ifconfig(lua_State *L) { struct ifaddrs *ifa,*ifp; if(getifaddrs(&ifp)==-1){push_error(L,"error");return 2;} lua_newtable(L); int i=1; for(ifa=ifp;ifa;ifa=ifa->ifa_next){if(!ifa->ifa_addr||ifa->ifa_addr->sa_family!=AF_INET)continue; lua_newtable(L); lua_pushstring(L,ifa->ifa_name); lua_setfield(L,-2,"name"); char ip[INET_ADDRSTRLEN]; inet_ntop(AF_INET,&((struct sockaddr_in*)ifa->ifa_addr)->sin_addr,ip,sizeof(ip)); lua_pushstring(L,ip); lua_setfield(L,-2,"ip"); if(ifa->ifa_netmask){inet_ntop(AF_INET,&((struct sockaddr_in*)ifa->ifa_netmask)->sin_addr,ip,sizeof(ip)); lua_pushstring(L,ip); lua_setfield(L,-2,"netmask");} lua_rawseti(L,-2,i++);} freeifaddrs(ifp); return 1; }
static int l_local_ip(lua_State *L) { struct ifaddrs *ifa,*ifp; if(getifaddrs(&ifp)==-1){push_error(L,"error");return 2;} for(ifa=ifp;ifa;ifa=ifa->ifa_next){if(!ifa->ifa_addr||ifa->ifa_addr->sa_family!=AF_INET)continue; if(!strcmp(ifa->ifa_name,"lo"))continue; char ip[INET_ADDRSTRLEN]; inet_ntop(AF_INET,&((struct sockaddr_in*)ifa->ifa_addr)->sin_addr,ip,sizeof(ip)); freeifaddrs(ifp); lua_pushstring(L,ip); return 1;} freeifaddrs(ifp); push_error(L,"no IP"); return 2; }
static int l_is_ip(lua_State *L) { const char *s=luaL_checkstring(L,1); struct sockaddr_in sa; lua_pushboolean(L,inet_pton(AF_INET,s,&sa.sin_addr)==1); return 1; }
static int l_is_port_open(lua_State *L) { const char *h=luaL_checkstring(L,1); int p=luaL_checkinteger(L,2); int fd=socket(AF_INET,SOCK_STREAM,0); if(fd<0){lua_pushboolean(L,0);return 1;} struct sockaddr_in addr; memset(&addr,0,sizeof(addr)); addr.sin_family=AF_INET; addr.sin_port=htons(p); inet_pton(AF_INET,h,&addr.sin_addr); set_nonblocking(fd); int ret=connect(fd,(struct sockaddr*)&addr,sizeof(addr)); if(ret<0&&errno!=EINPROGRESS){close(fd);lua_pushboolean(L,0);return 1;} if(errno==EINPROGRESS&&wait_connect(fd,2)<0){close(fd);lua_pushboolean(L,0);return 1;} close(fd); lua_pushboolean(L,1); return 1; }
static int l_gethostname(lua_State *L) { char h[256]; if(gethostname(h,sizeof(h))!=0){push_error(L,"error");return 2;} lua_pushstring(L,h); return 1; }
static int l_getfqdn(lua_State *L) { const char *n=luaL_optstring(L,1,NULL); if(!n){char h[256]; if(gethostname(h,sizeof(h))!=0){push_error(L,"error");return 2;} n=h;} struct addrinfo hints,*res; memset(&hints,0,sizeof(hints)); hints.ai_family=AF_INET; hints.ai_flags=AI_CANONNAME; if(getaddrinfo(n,NULL,&hints,&res)!=0){push_error(L,"error");return 2;} lua_pushstring(L,res->ai_canonname?res->ai_canonname:n); freeaddrinfo(res); return 1; }
static int l_getprotobyname(lua_State *L) { const char *n=luaL_checkstring(L,1); struct protoent *pe=getprotobyname(n); if(!pe){push_error(L,"not found");return 2;} lua_pushinteger(L,pe->p_proto); return 1; }
static int l_getservbyname(lua_State *L) { const char *s=luaL_checkstring(L,1),*p=luaL_optstring(L,2,"tcp"); struct servent *se=getservbyname(s,p); if(!se){push_error(L,"not found");return 2;} lua_pushinteger(L,ntohs(se->s_port)); return 1; }
static int l_getservbyport(lua_State *L) { int p=luaL_checkinteger(L,1); const char *pr=luaL_optstring(L,2,"tcp"); struct servent *se=getservbyport(htons(p),pr); if(!se){push_error(L,"not found");return 2;} lua_pushstring(L,se->s_name); return 1; }
static int l_inet_aton(lua_State *L) { const char *ip=luaL_checkstring(L,1); struct in_addr a; if(inet_aton(ip,&a)==0){push_error(L,"invalid IP");return 2;} lua_pushlstring(L,(const char*)&a,sizeof(a)); return 1; }
static int l_inet_ntoa(lua_State *L) { size_t len; const char *p=luaL_checklstring(L,1,&len); if(len!=sizeof(struct in_addr)){push_error(L,"invalid");return 2;} struct in_addr a; memcpy(&a,p,sizeof(a)); lua_pushstring(L,inet_ntoa(a)); return 1; }
static int l_inet_pton(lua_State *L) { int f=luaL_checkinteger(L,1); const char *ip=luaL_checkstring(L,2); char buf[sizeof(struct in6_addr)]; if(inet_pton(f,ip,buf)!=1){push_error(L,"invalid");return 2;} int sz=(f==AF_INET)?sizeof(struct in_addr):sizeof(struct in6_addr); lua_pushlstring(L,buf,sz); return 1; }
static int l_inet_ntop(lua_State *L) { int f=luaL_checkinteger(L,1); size_t len; const char *p=luaL_checklstring(L,2,&len); char buf[INET6_ADDRSTRLEN]; if(inet_ntop(f,p,buf,sizeof(buf))==NULL){push_error(L,"invalid");return 2;} lua_pushstring(L,buf); return 1; }
static int l_htonl(lua_State *L) { lua_pushinteger(L,htonl(luaL_checkinteger(L,1))); return 1; }
static int l_htons(lua_State *L) { lua_pushinteger(L,htons(luaL_checkinteger(L,1))); return 1; }
static int l_ntohl(lua_State *L) { lua_pushinteger(L,ntohl(luaL_checkinteger(L,1))); return 1; }
static int l_ntohs(lua_State *L) { lua_pushinteger(L,ntohs(luaL_checkinteger(L,1))); return 1; }
static int l_if_nameindex(lua_State *L) { struct if_nameindex *if_ni=if_nameindex(); if(!if_ni){push_error(L,"error");return 2;} lua_newtable(L); int i=1; for(struct if_nameindex *p=if_ni;p->if_index||p->if_name;p++){lua_newtable(L); lua_pushinteger(L,p->if_index); lua_setfield(L,-2,"index"); lua_pushstring(L,p->if_name); lua_setfield(L,-2,"name"); lua_rawseti(L,-2,i++);} if_freenameindex(if_ni); return 1; }
static int l_if_nametoindex(lua_State *L) { const char *n=luaL_checkstring(L,1); unsigned idx=if_nametoindex(n); if(idx==0){push_error(L,"not found");return 2;} lua_pushinteger(L,idx); return 1; }
static int l_if_indextoname(lua_State *L) { unsigned idx=luaL_checkinteger(L,1); char buf[IF_NAMESIZE]; if(!if_indextoname(idx,buf)){push_error(L,"invalid");return 2;} lua_pushstring(L,buf); return 1; }
static int l_version(lua_State *L) { lua_pushstring(L,"5.0.1"); return 1; }

/* ========== سازنده‌ها ========== */
static int tcp_constructor(lua_State *L) { tcp_t *t=lua_newuserdata(L,sizeof(tcp_t)); memset(t,0,sizeof(tcp_t)); t->fd=-1; t->timeout=DEFAULT_TIMEOUT; t->blocking=1; luaL_getmetatable(L,"tcp"); lua_setmetatable(L,-2); return 1; }
static int udp_constructor(lua_State *L) { udp_t *u=lua_newuserdata(L,sizeof(udp_t)); memset(u,0,sizeof(udp_t)); u->fd=-1; u->timeout=DEFAULT_TIMEOUT; u->blocking=1; luaL_getmetatable(L,"udp"); lua_setmetatable(L,-2); return 1; }

/* ========== رجیستر ========== */
static const luaL_Reg lib[] = {
    {"dns",l_dns},{"dns_all",l_dns_all},{"reverse_dns",l_reverse_dns},
    {"scan",l_scan},{"scan_range",l_scan_range},
    {"http",l_http},{"headers",l_headers},{"download",l_download},
    {"ping",l_ping},{"ifconfig",l_ifconfig},{"local_ip",l_local_ip},
    {"is_ip",l_is_ip},{"is_port_open",l_is_port_open},
    {"gethostname",l_gethostname},{"getfqdn",l_getfqdn},
    {"getprotobyname",l_getprotobyname},{"getservbyname",l_getservbyname},
    {"getservbyport",l_getservbyport},
    {"inet_aton",l_inet_aton},{"inet_ntoa",l_inet_ntoa},
    {"inet_pton",l_inet_pton},{"inet_ntop",l_inet_ntop},
    {"htonl",l_htonl},{"htons",l_htons},{"ntohl",l_ntohl},{"ntohs",l_ntohs},
    {"if_nameindex",l_if_nameindex},{"if_nametoindex",l_if_nametoindex},
    {"if_indextoname",l_if_indextoname},
    {"raw_socket",l_raw_socket},{"raw_recv",l_raw_recv},{"raw_send",l_raw_send},
    {"raw_close",l_raw_close},{"raw_bind",l_raw_bind},
    {"arp_spoof",l_arp_spoof},
    {"version",l_version},
    {NULL,NULL}
};

int luaopen_netsocket(lua_State *L) {
    luaL_newmetatable(L,"tcp"); lua_pushvalue(L,-1); lua_setfield(L,-2,"__index");
    lua_pushcfunction(L,tcp_connect); lua_setfield(L,-2,"connect");
    lua_pushcfunction(L,tcp_send); lua_setfield(L,-2,"send");
    lua_pushcfunction(L,tcp_sendall); lua_setfield(L,-2,"sendall");
    lua_pushcfunction(L,tcp_recv); lua_setfield(L,-2,"receive");
    lua_pushcfunction(L,tcp_close); lua_setfield(L,-2,"close");
    lua_pushcfunction(L,tcp_bind); lua_setfield(L,-2,"bind");
    lua_pushcfunction(L,tcp_listen); lua_setfield(L,-2,"listen");
    lua_pushcfunction(L,tcp_accept); lua_setfield(L,-2,"accept");
    lua_pushcfunction(L,tcp_settimeout); lua_setfield(L,-2,"settimeout");
    lua_pushcfunction(L,tcp_starttls); lua_setfield(L,-2,"starttls");
    lua_pushcfunction(L,tcp_getpeername); lua_setfield(L,-2,"getpeername");
    lua_pushcfunction(L,tcp_getsockname); lua_setfield(L,-2,"getsockname");
    lua_pushcfunction(L,tcp_setoption); lua_setfield(L,-2,"setoption");
    lua_pushcfunction(L,tcp_getoption); lua_setfield(L,-2,"getoption");
    lua_pushcfunction(L,tcp_shutdown); lua_setfield(L,-2,"shutdown");
    lua_pushcfunction(L,tcp_gettimeout); lua_setfield(L,-2,"gettimeout");
    lua_pushcfunction(L,tcp_setblocking); lua_setfield(L,-2,"setblocking");
    lua_pushcfunction(L,tcp_sendfile); lua_setfield(L,-2,"sendfile");
    lua_pushcfunction(L,tcp_gc); lua_setfield(L,-2,"__gc");
    lua_pop(L,1);

    luaL_newmetatable(L,"udp"); lua_pushvalue(L,-1); lua_setfield(L,-2,"__index");
    lua_pushcfunction(L,udp_bind); lua_setfield(L,-2,"bind");
    lua_pushcfunction(L,udp_sendto); lua_setfield(L,-2,"sendto");
    lua_pushcfunction(L,udp_recvfrom); lua_setfield(L,-2,"receivefrom");
    lua_pushcfunction(L,udp_close); lua_setfield(L,-2,"close");
    lua_pushcfunction(L,udp_settimeout); lua_setfield(L,-2,"settimeout");
    lua_pushcfunction(L,udp_getsockname); lua_setfield(L,-2,"getsockname");
    lua_pushcfunction(L,udp_setoption); lua_setfield(L,-2,"setoption");
    lua_pushcfunction(L,udp_getoption); lua_setfield(L,-2,"getoption");
    lua_pushcfunction(L,udp_gettimeout); lua_setfield(L,-2,"gettimeout");
    lua_pushcfunction(L,udp_setblocking); lua_setfield(L,-2,"setblocking");
    lua_pushcfunction(L,udp_gc); lua_setfield(L,-2,"__gc");
    lua_pop(L,1);

    luaL_newlib(L, lib);
    lua_pushcfunction(L, tcp_constructor); lua_setfield(L, -2, "tcp");
    lua_pushcfunction(L, udp_constructor); lua_setfield(L, -2, "udp");

    lua_pushinteger(L, AF_INET);   lua_setfield(L, -2, "AF_INET");
    lua_pushinteger(L, AF_INET6);  lua_setfield(L, -2, "AF_INET6");
    lua_pushinteger(L, SOCK_STREAM); lua_setfield(L, -2, "SOCK_STREAM");
    lua_pushinteger(L, SOCK_DGRAM);  lua_setfield(L, -2, "SOCK_DGRAM");
    lua_pushinteger(L, AF_PACKET);   lua_setfield(L, -2, "AF_PACKET");
    lua_pushinteger(L, SOCK_RAW);    lua_setfield(L, -2, "SOCK_RAW");
    lua_pushinteger(L, ETH_P_ALL);   lua_setfield(L, -2, "ETH_P_ALL");

    return 1;
}
