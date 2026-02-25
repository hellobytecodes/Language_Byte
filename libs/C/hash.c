#include <string.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include "../../src/lua.h"
#include "../../src/lauxlib.h"

// تبدیل باینری به هگزادسیمال
static void to_hex(unsigned char *data, int len, char *output) {
    for(int i = 0; i < len; i++)
        sprintf(output + i*2, "%02x", data[i]);
    output[len*2] = '\0';
}

// تبدیل هگز به باینری
static int from_hex(lua_State *L) {
    const char *hex = luaL_checkstring(L, 1);
    int len = strlen(hex) / 2;
    unsigned char *data = malloc(len);
    
    for(int i = 0; i < len; i++) {
        sscanf(hex + i*2, "%2hhx", &data[i]);
    }
    
    lua_pushlstring(L, (char*)data, len);
    free(data);
    return 1;
}

// ==================== هش ====================
static void hash_with_evp(lua_State *L, const EVP_MD *(*md_func)(void)) {
    size_t len;
    const char *s = luaL_checklstring(L, 1, &len);
    
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    const EVP_MD *md = md_func();
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;
    
    EVP_DigestInit_ex(ctx, md, NULL);
    EVP_DigestUpdate(ctx, s, len);
    EVP_DigestFinal_ex(ctx, hash, &hash_len);
    
    EVP_MD_CTX_free(ctx);
    
    char hex[hash_len*2 + 1];
    to_hex(hash, hash_len, hex);
    lua_pushstring(L, hex);
}

static int l_md5(lua_State *L) { hash_with_evp(L, EVP_md5); return 1; }
static int l_sha1(lua_State *L) { hash_with_evp(L, EVP_sha1); return 1; }
static int l_sha256(lua_State *L) { hash_with_evp(L, EVP_sha256); return 1; }
static int l_sha512(lua_State *L) { hash_with_evp(L, EVP_sha512); return 1; }

// ==================== رمزنگاری با خروجی hex ====================
static int l_encrypt_hex(lua_State *L) {
    size_t text_len, key_len;
    const char *text = luaL_checklstring(L, 1, &text_len);
    const char *key = luaL_checklstring(L, 2, &key_len);

    unsigned char aes_key[32];
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, key, key_len);
    EVP_DigestFinal_ex(ctx, aes_key, NULL);
    EVP_MD_CTX_free(ctx);

    unsigned char iv[16] = {0};
    EVP_CIPHER_CTX *cipher_ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(cipher_ctx, EVP_aes_256_cbc(), NULL, aes_key, iv);

    int out_len = text_len + EVP_CIPHER_CTX_block_size(cipher_ctx);
    unsigned char *out = malloc(out_len);

    EVP_EncryptUpdate(cipher_ctx, out, &out_len, (unsigned char*)text, text_len);
    int final_len;
    EVP_EncryptFinal_ex(cipher_ctx, out + out_len, &final_len);
    out_len += final_len;

    EVP_CIPHER_CTX_free(cipher_ctx);
    
    char hex[out_len*2 + 1];
    to_hex(out, out_len, hex);
    lua_pushstring(L, hex);
    
    free(out);
    return 1;
}

static int l_decrypt_hex(lua_State *L) {
    const char *hex = luaL_checkstring(L, 1);
    size_t key_len;
    const char *key = luaL_checklstring(L, 2, &key_len);

    // تبدیل hex به باینری
    int cipher_len = strlen(hex) / 2;
    unsigned char *cipher = malloc(cipher_len);
    for(int i = 0; i < cipher_len; i++) {
        sscanf(hex + i*2, "%2hhx", &cipher[i]);
    }

    unsigned char aes_key[32];
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, key, key_len);
    EVP_DigestFinal_ex(ctx, aes_key, NULL);
    EVP_MD_CTX_free(ctx);

    unsigned char iv[16] = {0};
    EVP_CIPHER_CTX *cipher_ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(cipher_ctx, EVP_aes_256_cbc(), NULL, aes_key, iv);

    int out_len = cipher_len;
    unsigned char *out = malloc(out_len);

    EVP_DecryptUpdate(cipher_ctx, out, &out_len, cipher, cipher_len);
    int final_len;
    EVP_DecryptFinal_ex(cipher_ctx, out + out_len, &final_len);
    out_len += final_len;

    EVP_CIPHER_CTX_free(cipher_ctx);
    lua_pushlstring(L, (char*)out, out_len);
    
    free(cipher);
    free(out);
    return 1;
}

// ==================== XOR ====================
static int l_xor(lua_State *L) {
    size_t text_len, key_len;
    const char *text = luaL_checklstring(L, 1, &text_len);
    const char *key = luaL_checklstring(L, 2, &key_len);

    char *result = malloc(text_len);
    for(size_t i = 0; i < text_len; i++)
        result[i] = text[i] ^ key[i % key_len];

    lua_pushlstring(L, result, text_len);
    free(result);
    return 1;
}

// ثبت توابع
static const struct luaL_Reg hash_lib[] = {
    {"md5", l_md5},
    {"sha1", l_sha1},
    {"sha256", l_sha256},
    {"sha512", l_sha512},
    {"encrypt", l_encrypt_hex},
    {"decrypt", l_decrypt_hex},
    {"xor", l_xor},
    {"from_hex", from_hex},
    {NULL, NULL}
};

int luaopen_hash(lua_State *L) {
    luaL_newlib(L, hash_lib);
    return 1;
}
