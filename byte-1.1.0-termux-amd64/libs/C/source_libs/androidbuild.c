#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#define WORK_DIR  ".byte_apk_build"
#define JAR_PATH  "libs/C/AndroidJar/android.jar"

static int fex(const char *p) { return access(p, F_OK) == 0; }

static int mkdir_p(const char *p) {
    char t[2048];
    snprintf(t, sizeof(t), "%s", p);
    for (char *s = t + 1; *s; s++) if (*s == '/') { *s = 0; mkdir(t, 0755); *s = '/'; }
    return mkdir(t, 0755);
}

static int wf(const char *p, const char *c) {
    FILE *f = fopen(p, "w"); if (!f) return -1; fputs(c, f); fclose(f); return 0;
}

static const char* get_jar() {
    if (fex(JAR_PATH)) return JAR_PATH;
    static char d[1024];
    snprintf(d, sizeof(d), "%s/.byte-android-sdk/platforms/android-34/android.jar",
             getenv("HOME") ? getenv("HOME") : "/root");
    if (fex(d)) return d;
    return NULL;
}

static int l_build(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    lua_getfield(L, 1, "java");     const char *java     = lua_tostring(L, -1);
    lua_getfield(L, 1, "layout");   const char *layout   = lua_tostring(L, -1);
    lua_getfield(L, 1, "manifest"); const char *manifest = lua_tostring(L, -1);
    lua_getfield(L, 1, "name");     const char *name     = lua_tostring(L, -1);
    lua_getfield(L, 1, "package");  const char *pkg      = lua_tostring(L, -1);
    lua_getfield(L, 1, "icon");     const char *icon     = lua_tostring(L, -1);

    if (!java || !strlen(java)) return luaL_error(L, "java code required");
    if (!name) name = "ByteApp";
    if (!pkg || !strlen(pkg)) pkg = "com.example.app";

    const char *jar = get_jar();
    if (!jar) return luaL_error(L, "android.jar not found in libs/C/AndroidJar/");

    system("rm -rf " WORK_DIR);
    mkdir_p(WORK_DIR "/src");
    mkdir_p(WORK_DIR "/res/layout");
    mkdir_p(WORK_DIR "/res/values");
    mkdir_p(WORK_DIR "/res/drawable");
    mkdir_p(WORK_DIR "/obj");
    mkdir_p(WORK_DIR "/compiled");
    mkdir_p(WORK_DIR "/gen");

    wf(WORK_DIR "/src/MainActivity.java", java);

    if (layout && strlen(layout) > 0) wf(WORK_DIR "/res/layout/activity_main.xml", layout);
    else wf(WORK_DIR "/res/layout/activity_main.xml",
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<LinearLayout xmlns:android=\"http://schemas.android.com/apk/res/android\"\n"
        "  android:layout_width=\"match_parent\" android:layout_height=\"match_parent\"\n"
        "  android:gravity=\"center\">\n"
        "  <TextView android:id=\"@+id/hello\" android:text=\"Hello\" android:textSize=\"28sp\"/>\n"
        "</LinearLayout>\n");

    char s[512];
    snprintf(s, sizeof(s), "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<resources>\n  <string name=\"app_name\">%s</string>\n</resources>\n", name);
    wf(WORK_DIR "/res/values/strings.xml", s);

    if (icon && strlen(icon) > 0 && fex(icon)) {
        char ic[1024];
        snprintf(ic, sizeof(ic), "cp %s " WORK_DIR "/res/drawable/icon.png 2>/dev/null", icon);
        system(ic);
    }

    if (manifest && strlen(manifest) > 0) wf(WORK_DIR "/AndroidManifest.xml", manifest);
    else {
        char m[4096];
        snprintf(m, sizeof(m),
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
            "<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\"\n"
            "    package=\"%s\" android:versionCode=\"1\" android:versionName=\"1.0\">\n"
            "  <uses-sdk android:minSdkVersion=\"21\" android:targetSdkVersion=\"34\"/>\n"
            "  <application android:label=\"%s\" android:theme=\"@android:style/Theme.Material.Light.NoActionBar\">\n"
            "    <activity android:name=\".MainActivity\" android:exported=\"true\">\n"
            "      <intent-filter>\n"
            "        <action android:name=\"android.intent.action.MAIN\"/>\n"
            "        <category android:name=\"android.intent.category.LAUNCHER\"/>\n"
            "      </intent-filter>\n"
            "    </activity>\n"
            "  </application>\n</manifest>\n", pkg, name);
        wf(WORK_DIR "/AndroidManifest.xml", m);
    }

    char c[8192];

    snprintf(c, sizeof(c), "aapt2 compile -o %s/compiled %s/res/values/strings.xml 2>/dev/null", WORK_DIR, WORK_DIR); system(c);
    if (layout && strlen(layout) > 0) {
        snprintf(c, sizeof(c), "aapt2 compile -o %s/compiled %s/res/layout/activity_main.xml 2>/dev/null", WORK_DIR, WORK_DIR); system(c);
    }

    snprintf(c, sizeof(c),
        "aapt2 link -o %s/base.apk -I %s "
        "--manifest %s/AndroidManifest.xml "
        "%s/compiled/*.flat --auto-add-overlay "
        "--java %s/gen 2>&1",
        WORK_DIR, jar, WORK_DIR, WORK_DIR, WORK_DIR);
    if (system(c)) { system("rm -rf " WORK_DIR); return luaL_error(L, "aapt2 link failed"); }

    snprintf(c, sizeof(c), "find %s/gen -name 'R.java' -exec cp {} %s/src/ \\; 2>/dev/null", WORK_DIR, WORK_DIR); system(c);

    snprintf(c, sizeof(c), "ecj -d %s/obj -source 1.7 -target 1.7 -nowarn %s/src/*.java 2>&1", WORK_DIR, WORK_DIR);
    if (system(c)) { system("rm -rf " WORK_DIR); return luaL_error(L, "Java compile error"); }

    snprintf(c, sizeof(c), "dx --dex --output=%s/classes.dex %s/obj 2>&1", WORK_DIR, WORK_DIR);
    if (system(c)) { system("rm -rf " WORK_DIR); return luaL_error(L, "DEX error"); }

    snprintf(c, sizeof(c), "cd %s && zip -u base.apk classes.dex 2>/dev/null", WORK_DIR); system(c);

    snprintf(c, sizeof(c),
        "apksigner sign --ks $HOME/.byte-android-sdk/debug.keystore "
        "--ks-pass pass:android --min-sdk-version 21 "
        "--out %s.apk %s/base.apk 2>&1", name, WORK_DIR);
    if (system(c)) { system("rm -rf " WORK_DIR); return luaL_error(L, "Signing failed – run build.sh"); }

    system("rm -rf " WORK_DIR);

    char o[512]; snprintf(o, sizeof(o), "%s.apk", name);
    lua_pushstring(L, o); printf("[OK] %s\n", o); return 1;
}

int luaopen_androidbuild(lua_State *L) {
    lua_newtable(L);
    lua_pushcfunction(L, l_build); lua_setfield(L, -2, "build");
    return 1;
}
