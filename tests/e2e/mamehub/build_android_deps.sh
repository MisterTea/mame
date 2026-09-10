#!/bin/bash
# Cross-build SDL2, OpenSSL, and libsodium for Android arm64-v8a.
#
# Env:
#   ANDROID_NDK_HOME     required
#   ANDROID_DEPS_ROOT    install root (default /tmp/mamehub-android-deps)
#   ANDROID_API          API level (default 24)
#   MAMEHUB_DEP_SRC      source cache dir (default $ANDROID_DEPS_ROOT)
set -euo pipefail

ROOT="${ANDROID_DEPS_ROOT:-/tmp/mamehub-android-deps}"
SRC_ROOT="${MAMEHUB_DEP_SRC:-$ROOT}"
API="${ANDROID_API:-24}"
ABI="arm64-v8a"
HOST_TAG="${ANDROID_NDK_HOST_TAG:-}"
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu)"

[[ -n "${ANDROID_NDK_HOME:-}" ]] || { echo "ANDROID_NDK_HOME is not set"; exit 1; }
[[ -d "$ANDROID_NDK_HOME" ]] || { echo "ANDROID_NDK_HOME missing: $ANDROID_NDK_HOME"; exit 1; }

if [[ -z "$HOST_TAG" ]]; then
  case "$(uname -s)-$(uname -m)" in
    Linux-x86_64) HOST_TAG=linux-x86_64 ;;
    Darwin-arm64) HOST_TAG=darwin-arm64 ;;
    Darwin-x86_64) HOST_TAG=darwin-x86_64 ;;
    *) echo "Unsupported host for Android NDK: $(uname -s)-$(uname -m)"; exit 1 ;;
  esac
fi

TOOLCHAIN="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$HOST_TAG"
[[ -d "$TOOLCHAIN" ]] || { echo "NDK toolchain missing: $TOOLCHAIN"; exit 1; }

CC="$TOOLCHAIN/bin/aarch64-linux-android${API}-clang"
CXX="$TOOLCHAIN/bin/aarch64-linux-android${API}-clang++"
AR="$TOOLCHAIN/bin/llvm-ar"
RANLIB="$TOOLCHAIN/bin/llvm-ranlib"
STRIP="$TOOLCHAIN/bin/llvm-strip"

OPENSSL_PREFIX="$ROOT/openssl-arm64"
SODIUM_PREFIX="$ROOT/libsodium-arm64"
SDL_PREFIX="$ROOT/sdl-arm64"

mkdir -p "$SRC_ROOT" "$ROOT"
echo "== Android deps: abi=$ABI api=$API ndk=$ANDROID_NDK_HOME host=$HOST_TAG =="

fetch_tar() {
  local url=$1 dest=$2
  if [[ -d "$dest" ]]; then
    return 0
  fi
  local tar="${dest}.tar.gz"
  if [[ ! -f "$tar" ]]; then
    echo "Downloading $url"
    curl -L --fail -o "$tar" "$url"
  fi
  mkdir -p "$(dirname "$dest")"
  tar -xzf "$tar" -C "$(dirname "$dest")"
}

# ---- sources ----
fetch_tar "https://www.openssl.org/source/openssl-3.0.17.tar.gz" "$SRC_ROOT/openssl-3.0.17"
fetch_tar "https://download.libsodium.org/libsodium/releases/libsodium-1.0.20.tar.gz" "$SRC_ROOT/libsodium-1.0.20"
fetch_tar "https://www.libsdl.org/release/SDL2-2.32.10.tar.gz" "$SRC_ROOT/SDL2-2.32.10"

# ---- OpenSSL ----
if [[ ! -f "$OPENSSL_PREFIX/lib/libssl.a" ]]; then
  echo "== OpenSSL =="
  rm -rf "$ROOT/openssl-build-arm64"
  rsync -a --delete --exclude=*.o --exclude=*.a --exclude=__pycache__ \
    "$SRC_ROOT/openssl-3.0.17/" "$ROOT/openssl-build-arm64/"
  pushd "$ROOT/openssl-build-arm64" >/dev/null
  export ANDROID_NDK_ROOT="$ANDROID_NDK_HOME"
  export PATH="$TOOLCHAIN/bin:$PATH"
  unset CC CXX CFLAGS CXXFLAGS LDFLAGS
  ./Configure android-arm64 \
    no-shared no-tests no-ui-console no-dso \
    -D__ANDROID_API__="$API" \
    --prefix="$OPENSSL_PREFIX" --openssldir="$OPENSSL_PREFIX/ssl"
  make -j"$JOBS"
  make install_sw
  popd >/dev/null
else
  echo "OpenSSL already at $OPENSSL_PREFIX"
fi

# OpenSSL Configure unsets CC/CXX; restore NDK compilers for later deps.
CC="$TOOLCHAIN/bin/aarch64-linux-android${API}-clang"
CXX="$TOOLCHAIN/bin/aarch64-linux-android${API}-clang++"
AR="$TOOLCHAIN/bin/llvm-ar"
RANLIB="$TOOLCHAIN/bin/llvm-ranlib"
STRIP="$TOOLCHAIN/bin/llvm-strip"

# ---- libsodium ----
if [[ ! -f "$SODIUM_PREFIX/lib/libsodium.a" ]]; then
  echo "== libsodium =="
  rm -rf "$ROOT/libsodium-src-arm64" "$ROOT/libsodium-build-arm64"
  rsync -a --delete --exclude=*.o --exclude=*.a --exclude=.libs --exclude=__pycache__ \
    "$SRC_ROOT/libsodium-1.0.20/" "$ROOT/libsodium-src-arm64/"
  if [[ -f "$ROOT/libsodium-src-arm64/Makefile" ]]; then
    make -C "$ROOT/libsodium-src-arm64" distclean >/dev/null 2>&1 || true
  fi
  mkdir -p "$ROOT/libsodium-build-arm64"
  pushd "$ROOT/libsodium-build-arm64" >/dev/null
  export CC CXX AR RANLIB
  export CFLAGS="-fPIC -O2" CXXFLAGS="-fPIC -O2" LDFLAGS=""
  "$ROOT/libsodium-src-arm64/configure" \
    --host=aarch64-linux-android \
    --prefix="$SODIUM_PREFIX" \
    --disable-shared --enable-static \
    --disable-dependency-tracking \
    CC="$CC" CXX="$CXX" AR="$AR" RANLIB="$RANLIB"
  make -j"$JOBS"
  make install
  popd >/dev/null
else
  echo "libsodium already at $SODIUM_PREFIX"
fi

# ---- SDL2 ----
if [[ ! -f "$SDL_PREFIX/lib/libSDL2.so" ]]; then
  echo "== SDL2 =="
  unset CFLAGS CXXFLAGS LDFLAGS
  rm -rf "$ROOT/sdl-build-arm64"
  cmake -S "$SRC_ROOT/SDL2-2.32.10" -B "$ROOT/sdl-build-arm64" \
    -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="$ABI" \
    -DANDROID_PLATFORM="android-$API" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$SDL_PREFIX" \
    -DSDL_SHARED=ON \
    -DSDL_STATIC=OFF \
    -DSDL_TEST=OFF \
    -DSDL_HIDAPI=ON
  cmake --build "$ROOT/sdl-build-arm64" -j"$JOBS"
  cmake --install "$ROOT/sdl-build-arm64"
else
  echo "SDL2 already at $SDL_PREFIX"
fi

echo "OK: openssl=$OPENSSL_PREFIX"
echo "OK: sodium=$SODIUM_PREFIX"
echo "OK: sdl=$SDL_PREFIX"
echo "export ANDROID_OPENSSL_ROOT=$OPENSSL_PREFIX"
echo "export ANDROID_SODIUM_ROOT=$SODIUM_PREFIX"
echo "export SDL_INSTALL_ROOT=$SDL_PREFIX"
