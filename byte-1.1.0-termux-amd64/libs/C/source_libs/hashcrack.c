/*
 * hashcrack.c - Ultimate Hash Library for Byte (v2.3)
 * ۱۷ الگوریتم بدون bcrypt
 * کامپایل: gcc -shared -fPIC -o hashcrack.so source_libs/hashcrack.c -I../../src -lssl -lcrypto
 */

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#include <openssl/evp.h>
#include <openssl/md4.h>
#include <openssl/md5.h>
#include <openssl/sha.h>
#include <openssl/ripemd.h>
#include <openssl/whrlpool.h>

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#pragma GCC diagnostic pop

#define MAX_OUT 4096

static const char* supported_hashes[] = {
    "md4", "md5",
    "sha1", "sha224", "sha256", "sha384", "sha512",
    "sha3-224", "sha3-256", "sha3-384", "sha3-512",
    "ripemd160",
    "blake2b", "blake2s",
    "whirlpool",
    "ntlm",
    "lm",
    NULL
};

static void to_hex(const unsigned char *data, int len, char *out) {
    for (int i = 0; i < len; i++) {
        sprintf(out + i*2, "%02x", data[i]);
    }
    out[len*2] = '\0';
}

static int evp_hash(const char *algo_name, const unsigned char *data, size_t len,
                    unsigned char *digest, unsigned int *dlen) {
    const EVP_MD *md = EVP_get_digestbyname(algo_name);
    if (!md) return 0;
    
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) return 0;
    
    EVP_DigestInit_ex(ctx, md, NULL);
    EVP_DigestUpdate(ctx, data, len);
    EVP_DigestFinal_ex(ctx, digest, dlen);
    EVP_MD_CTX_free(ctx);
    return 1;
}

static char* ntlm_hash(const char *input) {
    size_t len = strlen(input);
    unsigned char utf16[1024];
    int ulen = 0;
    for (size_t i = 0; i < len && ulen < 1022; i++) {
        utf16[ulen++] = input[i];
        utf16[ulen++] = 0;
    }
    
    unsigned char digest[MD4_DIGEST_LENGTH];
    MD4(utf16, ulen, digest);
    
    char *out = malloc(MD4_DIGEST_LENGTH * 2 + 1);
    to_hex(digest, MD4_DIGEST_LENGTH, out);
    return out;
}

static char* lm_hash(const char *input) {
    char upper[15] = {0};
    strncpy(upper, input, 14);
    for (int i = 0; upper[i]; i++) upper[i] = toupper(upper[i]);
    
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5((unsigned char*)upper, strlen(upper), digest);
    
    char *out = malloc(MD5_DIGEST_LENGTH * 2 + 1);
    to_hex(digest, MD5_DIGEST_LENGTH, out);
    return out;
}

static char* compute_hash_full(const char *input, const char *algorithm) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int dlen = 0;
    
    if (strcmp(algorithm, "ntlm") == 0) return ntlm_hash(input);
    if (strcmp(algorithm, "lm") == 0) return lm_hash(input);
    
    if (strcmp(algorithm, "md4") == 0) {
        MD4((unsigned char*)input, strlen(input), digest);
        dlen = MD4_DIGEST_LENGTH;
        char *out = malloc(dlen * 2 + 1);
        to_hex(digest, dlen, out);
        return out;
    }
    
    if (strcmp(algorithm, "whirlpool") == 0) {
        WHIRLPOOL((unsigned char*)input, strlen(input), digest);
        dlen = WHIRLPOOL_DIGEST_LENGTH;
        char *out = malloc(dlen * 2 + 1);
        to_hex(digest, dlen, out);
        return out;
    }
    
    const char *evp_name = NULL;
    if (strcmp(algorithm, "md5") == 0) evp_name = "md5";
    else if (strcmp(algorithm, "sha1") == 0) evp_name = "sha1";
    else if (strcmp(algorithm, "sha224") == 0) evp_name = "sha224";
    else if (strcmp(algorithm, "sha256") == 0) evp_name = "sha256";
    else if (strcmp(algorithm, "sha384") == 0) evp_name = "sha384";
    else if (strcmp(algorithm, "sha512") == 0) evp_name = "sha512";
    else if (strcmp(algorithm, "ripemd160") == 0) evp_name = "ripemd160";
    else if (strcmp(algorithm, "blake2b") == 0) evp_name = "blake2b512";
    else if (strcmp(algorithm, "blake2s") == 0) evp_name = "blake2s256";
    else if (strcmp(algorithm, "sha3-224") == 0) evp_name = "sha3-224";
    else if (strcmp(algorithm, "sha3-256") == 0) evp_name = "sha3-256";
    else if (strcmp(algorithm, "sha3-384") == 0) evp_name = "sha3-384";
    else if (strcmp(algorithm, "sha3-512") == 0) evp_name = "sha3-512";
    else return NULL;
    
    if (!evp_hash(evp_name, (unsigned char*)input, strlen(input), digest, &dlen)) return NULL;
    
    char *out = malloc(dlen * 2 + 1);
    to_hex(digest, dlen, out);
    return out;
}

/* ========== API های Byte ========== */

static int l_hash(lua_State *L) {
    const char *text = luaL_checkstring(L, 1);
    const char *algo = luaL_optstring(L, 2, "md5");
    
    char *result = compute_hash_full(text, algo);
    if (result) {
        lua_pushstring(L, result);
        free(result);
        return 1;
    }
    lua_pushnil(L);
    lua_pushstring(L, "unsupported algorithm");
    return 2;
}

static int l_crack(lua_State *L) {
    const char *target = luaL_checkstring(L, 1);
    const char *wordlist = luaL_checkstring(L, 2);
    const char *algo = luaL_optstring(L, 3, "md5");
    int max_attempts = luaL_optinteger(L, 4, 0);
    
    FILE *f = fopen(wordlist, "r");
    if (!f) {
        lua_pushnil(L);
        lua_pushstring(L, "cannot open wordlist");
        return 2;
    }
    
    char line[512];
    long attempts = 0;
    clock_t start = clock();
    
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n\r")] = 0;
        if (strlen(line) == 0) continue;
        
        char *computed = compute_hash_full(line, algo);
        attempts++;
        
        if (computed) {
            int match = (strcmp(computed, target) == 0);
            if (match) {
                fclose(f);
                lua_newtable(L);
                lua_pushstring(L, line); lua_setfield(L, -2, "password");
                lua_pushinteger(L, attempts); lua_setfield(L, -2, "attempts");
                lua_pushnumber(L, (double)(clock() - start) / CLOCKS_PER_SEC); lua_setfield(L, -2, "time");
                lua_pushstring(L, algo); lua_setfield(L, -2, "algorithm");
                free(computed);
                return 1;
            }
            free(computed);
        }
        
        if (max_attempts > 0 && attempts >= max_attempts) break;
    }
    
    fclose(f);
    lua_pushnil(L);
    lua_pushstring(L, "password not found");
    return 2;
}

static int l_identify(lua_State *L) {
    const char *hash = luaL_checkstring(L, 1);
    size_t len = strlen(hash);
    
    lua_newtable(L);
    int idx = 1;
    
    if (hash[0] == '$') {
        if (strncmp(hash, "$1$", 3) == 0) {
            lua_pushstring(L, "md5 (Unix crypt)"); lua_rawseti(L, -2, idx++);
        } else if (strncmp(hash, "$2a$", 4) == 0 || strncmp(hash, "$2b$", 4) == 0 || strncmp(hash, "$2y$", 4) == 0) {
            lua_pushstring(L, "bcrypt (not supported)"); lua_rawseti(L, -2, idx++);
        } else if (strncmp(hash, "$5$", 3) == 0) {
            lua_pushstring(L, "sha256 (Unix crypt)"); lua_rawseti(L, -2, idx++);
        } else if (strncmp(hash, "$6$", 3) == 0) {
            lua_pushstring(L, "sha512 (Unix crypt)"); lua_rawseti(L, -2, idx++);
        }
        return 1;
    }
    
    if (len == 32) {
        lua_pushstring(L, "md4 / md5 / NTLM"); lua_rawseti(L, -2, idx++);
    } else if (len == 40) {
        lua_pushstring(L, "sha1 / ripemd160"); lua_rawseti(L, -2, idx++);
    } else if (len == 56) {
        lua_pushstring(L, "sha224"); lua_rawseti(L, -2, idx++);
    } else if (len == 64) {
        lua_pushstring(L, "sha256 / blake2s"); lua_rawseti(L, -2, idx++);
    } else if (len == 96) {
        lua_pushstring(L, "sha384"); lua_rawseti(L, -2, idx++);
    } else if (len == 128) {
        lua_pushstring(L, "sha512 / blake2b / whirlpool / sha3-512"); lua_rawseti(L, -2, idx++);
    }
    
    return 1;
}

static int l_supported(lua_State *L) {
    lua_newtable(L);
    for (int i = 0; supported_hashes[i]; i++) {
        lua_pushstring(L, supported_hashes[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static int l_benchmark(lua_State *L) {
    const char *algo = luaL_optstring(L, 1, "md5");
    int rounds = luaL_optinteger(L, 2, 10000);
    
    clock_t start = clock();
    for (int i = 0; i < rounds; i++) {
        char *h = compute_hash_full("benchmark test string 12345", algo);
        if (h) free(h);
    }
    double elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;
    
    lua_newtable(L);
    lua_pushstring(L, algo); lua_setfield(L, -2, "algorithm");
    lua_pushinteger(L, rounds); lua_setfield(L, -2, "rounds");
    lua_pushnumber(L, elapsed); lua_setfield(L, -2, "time");
    lua_pushnumber(L, rounds / elapsed); lua_setfield(L, -2, "hashes_per_second");
    return 1;
}

static const luaL_Reg lib[] = {
    {"hash",      l_hash},
    {"crack",     l_crack},
    {"identify",  l_identify},
    {"supported", l_supported},
    {"benchmark", l_benchmark},
    {NULL, NULL}
};

int luaopen_hashcrack(lua_State *L) {
    luaL_newlib(L, lib);
    return 1;
}
