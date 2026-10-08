#!/bin/sh
set -e
: "${NDK:?set NDK=/path/to/android-ndk}"

CXX="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android31-clang++"
cd "$(dirname "$0")"

mkdir -p rom/vendor/bin/hw rom/system/lib64

echo "Compiling daemon bridge..."
$CXX -O2 -fPIE -pie -static-libstdc++ src/fodbridge.cpp \
    -o rom/vendor/bin/hw/android.hardware.biometrics.fingerprint-servicemahiroaidl \
    -llog

echo "Compiling injector..."
$CXX -O2 -fPIE -pie -static-libstdc++ src/fodinject.cpp \
    -o rom/vendor/bin/hw/android.hardware.biometrics.fingerprint-injectmahiroaidl \
    -llog -ldl

echo "Compiling hook library..."
$CXX -O2 -shared -fPIC -static-libstdc++ src/libfodhook.cpp \
    -o rom/system/lib64/libfodhook.so \
    -llog -ldl

echo "Done"
