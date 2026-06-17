/**
 * metaexif.c - Professional EXIF metadata library for Byte Language
 * Complete version - reads all camera settings, GPS data, and image info
 * Compilation: See instructions below
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#ifdef __cplusplus
}
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "exif.h"

// ============================================
// توابع کمکی
// ============================================

// خواندن فایل به حافظه
static unsigned char* read_file(const char* filename, size_t* size) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) return NULL;
    
    fseek(fp, 0, SEEK_END);
    *size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    unsigned char* buf = (unsigned char*)malloc(*size);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    
    size_t read = fread(buf, 1, *size, fp);
    fclose(fp);
    
    if (read != *size) {
        free(buf);
        return NULL;
    }
    
    return buf;
}

// تبدیل مختصات GPS به فرمت درجه/دقیقه/ثانیه
static void format_dms(char* buffer, size_t bufsize, double lat, double lon) {
    char lat_dir = (lat >= 0) ? 'N' : 'S';
    char lon_dir = (lon >= 0) ? 'E' : 'W';
    
    lat = fabs(lat);
    lon = fabs(lon);
    
    int lat_deg = (int)lat;
    int lat_min = (int)((lat - lat_deg) * 60);
    double lat_sec = ((lat - lat_deg) * 60 - lat_min) * 60;
    
    int lon_deg = (int)lon;
    int lon_min = (int)((lon - lon_deg) * 60);
    double lon_sec = ((lon - lon_deg) * 60 - lon_min) * 60;
    
    snprintf(buffer, bufsize, "%d°%d'%.1f\"%c %d°%d'%.1f\"%c",
             lat_deg, lat_min, lat_sec, lat_dir,
             lon_deg, lon_min, lon_sec, lon_dir);
}

// ============================================
// تابع اصلی استخراج EXIF
// ============================================
static int l_metadata_get(lua_State *L) {
    const char* filename = luaL_checkstring(L, 1);
    
    // خواندن فایل
    size_t size;
    unsigned char* buf = read_file(filename, &size);
    if (!buf) {
        lua_pushnil(L);
        lua_pushstring(L, "Cannot read file");
        return 2;
    }
    
    // پردازش EXIF
    easyexif::EXIFInfo exif;
    int code = exif.parseFrom(buf, size);
    free(buf);
    
    if (code) {
        lua_pushnil(L);
        lua_pushstring(L, "No EXIF data found");
        return 2;
    }
    
    // ایجاد جدول نتایج
    lua_newtable(L);
    
    // ========================================
    // اطلاعات دوربین
    // ========================================
    if (!exif.Make.empty()) {
        lua_pushstring(L, exif.Make.c_str());
        lua_setfield(L, -2, "make");
    }
    
    if (!exif.Model.empty()) {
        lua_pushstring(L, exif.Model.c_str());
        lua_setfield(L, -2, "model");
    }
    
    if (!exif.Software.empty()) {
        lua_pushstring(L, exif.Software.c_str());
        lua_setfield(L, -2, "software");
    }
    
    // ========================================
    // تاریخ و زمان
    // ========================================
    if (!exif.DateTime.empty()) {
        lua_pushstring(L, exif.DateTime.c_str());
        lua_setfield(L, -2, "datetime");
    }
    
    if (!exif.DateTimeOriginal.empty()) {
        lua_pushstring(L, exif.DateTimeOriginal.c_str());
        lua_setfield(L, -2, "datetime_original");
    }
    
    if (!exif.DateTimeDigitized.empty()) {
        lua_pushstring(L, exif.DateTimeDigitized.c_str());
        lua_setfield(L, -2, "datetime_digitized");
    }
    
    // ========================================
    // اطلاعات تصویر
    // ========================================
    if (!exif.ImageDescription.empty()) {
        lua_pushstring(L, exif.ImageDescription.c_str());
        lua_setfield(L, -2, "description");
    }
    
    if (!exif.Copyright.empty()) {
        lua_pushstring(L, exif.Copyright.c_str());
        lua_setfield(L, -2, "copyright");
    }
    
    if (exif.ImageWidth > 0) {
        lua_pushinteger(L, exif.ImageWidth);
        lua_setfield(L, -2, "width");
    }
    
    if (exif.ImageHeight > 0) {
        lua_pushinteger(L, exif.ImageHeight);
        lua_setfield(L, -2, "height");
    }
    
    if (exif.BitsPerSample > 0) {
        lua_pushinteger(L, exif.BitsPerSample);
        lua_setfield(L, -2, "bits");
    }
    
    if (exif.Orientation > 0) {
        lua_pushinteger(L, exif.Orientation);
        lua_setfield(L, -2, "orientation");
    }
    
    // ========================================
    // تنظیمات عکاسی
    // ========================================
    
    // سرعت شاتر
    if (exif.ExposureTime > 0) {
        lua_pushnumber(L, exif.ExposureTime);
        lua_setfield(L, -2, "exposure");
        
        char exp_buf[32];
        if (exif.ExposureTime < 1.0) {
            snprintf(exp_buf, sizeof(exp_buf), "1/%d", (int)(1.0 / exif.ExposureTime + 0.5));
        } else {
            snprintf(exp_buf, sizeof(exp_buf), "%.1f", exif.ExposureTime);
        }
        lua_pushstring(L, exp_buf);
        lua_setfield(L, -2, "exposure_str");
    }
    
    // دیافراگم
    if (exif.FNumber > 0) {
        lua_pushnumber(L, exif.FNumber);
        lua_setfield(L, -2, "aperture");
        
        char f_buf[16];
        snprintf(f_buf, sizeof(f_buf), "f/%.1f", exif.FNumber);
        lua_pushstring(L, f_buf);
        lua_setfield(L, -2, "aperture_str");
    }
    
    // ISO
    if (exif.ISOSpeedRatings > 0) {
        lua_pushinteger(L, exif.ISOSpeedRatings);
        lua_setfield(L, -2, "iso");
    }
    
    // فاصله کانونی
    if (exif.FocalLength > 0) {
        lua_pushnumber(L, exif.FocalLength);
        lua_setfield(L, -2, "focal");
        
        char fl_buf[32];
        snprintf(fl_buf, sizeof(fl_buf), "%.1f mm", exif.FocalLength);
        lua_pushstring(L, fl_buf);
        lua_setfield(L, -2, "focal_str");
    }
    
    // فاصله کانونی معادل 35mm
    if (exif.FocalLengthIn35mm > 0) {
        lua_pushinteger(L, exif.FocalLengthIn35mm);
        lua_setfield(L, -2, "focal_35mm");
    }
    
    // فلاش
    if (exif.Flash >= 0) {
        lua_pushinteger(L, exif.Flash);
        lua_setfield(L, -2, "flash");
        
        const char* flash_str = (exif.Flash & 0x1) ? "Yes" : "No";
        lua_pushstring(L, flash_str);
        lua_setfield(L, -2, "flash_str");
    }
    
    // حالت نورسنجی
    if (exif.MeteringMode >= 0) {
        lua_pushinteger(L, exif.MeteringMode);
        lua_setfield(L, -2, "metering");
    }
    
    // فاصله سوژه
    if (exif.SubjectDistance >= 0) {
        lua_pushnumber(L, exif.SubjectDistance);
        lua_setfield(L, -2, "subject_distance");
    }
    
    // جبران نوردهی
    if (exif.ExposureBiasValue != 0) {
        lua_pushnumber(L, exif.ExposureBiasValue);
        lua_setfield(L, -2, "exposure_bias");
    }
    
    // ========================================
    // اطلاعات GPS
    // ========================================
    if (exif.GeoLocation.Latitude != 0 || exif.GeoLocation.Longitude != 0) {
        lua_newtable(L);
        
        lua_pushnumber(L, exif.GeoLocation.Latitude);
        lua_setfield(L, -2, "lat");
        
        lua_pushnumber(L, exif.GeoLocation.Longitude);
        lua_setfield(L, -2, "lon");
        
        lua_pushnumber(L, exif.GeoLocation.Altitude);
        lua_setfield(L, -2, "alt");
        
        // فرمت DMS
        char dms_buf[128];
        format_dms(dms_buf, sizeof(dms_buf), 
                   exif.GeoLocation.Latitude, 
                   exif.GeoLocation.Longitude);
        lua_pushstring(L, dms_buf);
        lua_setfield(L, -2, "dms");
        
        // لینک گوگل مپ
        char map_buf[256];
        snprintf(map_buf, sizeof(map_buf), "https://maps.google.com/?q=%.6f,%.6f",
                 exif.GeoLocation.Latitude, exif.GeoLocation.Longitude);
        lua_pushstring(L, map_buf);
        lua_setfield(L, -2, "google_maps");
        
        lua_setfield(L, -2, "gps");
    }
    
    return 1;
}

// ============================================
// ثبت توابع در Lua (با extern "C" برای جلوگیری از name mangling)
// ============================================
static const luaL_Reg metadata_funcs[] = {
    {"get", l_metadata_get},
    {NULL, NULL}
};

#ifdef __cplusplus
extern "C"
#endif
int luaopen_metaexif(lua_State *L) {
    luaL_newlib(L, metadata_funcs);
    return 1;
}
