#!/bin/bash

echo "--- [1/3] Compiling Lua Core with fPIC ---"
cd src
make clean
# اضافه کردن -lm برای توابع ریاضی
make MYCFLAGS="-fPIC -DLUA_USE_LINUX" MYLIBS="-ldl -lm"
cd ..

echo "--- [2/3] Compiling Byte Interpreter ---"
# اضافه کردن -rdynamic و -lm برای حل مشکل سمبل‌ها
gcc -o byte byte.c -Isrc -Lsrc -llua -lm -ldl -rdynamic

echo "--- [3/3] Compiling Security Library ---"
# اینجا نکته طلاییه: حتما باید -lm رو به .so هم بدی
gcc -shared -fPIC -o libs/C/security.so libs/C/security.c -Isrc -Lsrc -llua -lm

echo "--- DONE! Testing now... ---"
./byte test.by
