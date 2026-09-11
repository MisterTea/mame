#!/bin/bash
# Cross-build SDL2, OpenSSL, and libsodium for iOS (device and/or simulator).
# Default: iphonesimulator arm64 (Apple Silicon host).
#
# Env:
#   IOS_DEPS_ROOT   install root (default /tmp/mamehub-ios-deps)
#   IOS_PLATFORM    iphonesimulator | iphoneos (default iphonesimulator)
#   IOS_MIN         deployment target (default 15.1)
set -euo pipefail

ROOT="${IOS_DEPS_ROOT:-/tmp/mamehub-ios-deps}"
PLATFORM="${IOS_PLATFORM:-iphonesimulator}"
IOS_MIN="${IOS_MIN:-15.1}"
SRC_ROOT="${MAMEHUB_DEP_SRC:-/tmp/mamehub-android-deps}"
JOBS="$(sysctl -n hw.ncpu)"

mkdir -p "$SRC_ROOT" "$ROOT"

fetch_tar() {
  local dest=$1
  shift
  local urls=("$@")
  if [[ -d "$dest" ]]; then
    return 0
  fi
  local tar="${dest}.tar.gz"
  if [[ ! -f "$tar" ]]; then
    local url ok=0
    for url in "${urls[@]}"; do
      echo "Downloading $url"
      if curl -L --fail --retry 5 --retry-delay 2 --retry-all-errors --connect-timeout 30 -o "$tar" "$url"; then
        ok=1
        break
      fi
      rm -f "$tar"
      echo "Download failed for $url; trying next mirror if any"
    done
    [[ "$ok" -eq 1 ]] || { echo "All download mirrors failed for $dest"; exit 1; }
  fi
  tar -xzf "$tar" -C "$(dirname "$dest")"
}

fetch_tar "$SRC_ROOT/openssl-3.0.17" \
  "https://www.openssl.org/source/openssl-3.0.17.tar.gz" \
  "https://github.com/openssl/openssl/releases/download/openssl-3.0.17/openssl-3.0.17.tar.gz"
# Prefer GitHub releases; download.libsodium.org has intermittent DNS failures on CI.
fetch_tar "$SRC_ROOT/libsodium-1.0.20" \
  "https://github.com/jedisct1/libsodium/releases/download/1.0.20-RELEASE/libsodium-1.0.20.tar.gz" \
  "https://download.libsodium.org/libsodium/releases/libsodium-1.0.20.tar.gz"
fetch_tar "$SRC_ROOT/SDL2-2.32.10" \
  "https://www.libsdl.org/release/SDL2-2.32.10.tar.gz" \
  "https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz"

SDK_PATH="$(xcrun --sdk "$PLATFORM" --show-sdk-path)"
if [[ "$PLATFORM" == "iphonesimulator" ]]; then
  TRIPLE="arm64-apple-ios${IOS_MIN}-simulator"
  OPENSSL_TARGET="iossimulator-xcrun"
  ARCH_SUFFIX="sim-arm64"
  CMAKE_SYSTEM_NAME="iOS"
  CMAKE_OSX_SYSROOT="iphonesimulator"
  CMAKE_OSX_ARCHITECTURES="arm64"
  VERSION_MIN_FLAG="-mios-simulator-version-min=${IOS_MIN}"
else
  TRIPLE="arm64-apple-ios${IOS_MIN}"
  OPENSSL_TARGET="ios64-xcrun"
  ARCH_SUFFIX="ios-arm64"
  CMAKE_SYSTEM_NAME="iOS"
  CMAKE_OSX_SYSROOT="iphoneos"
  CMAKE_OSX_ARCHITECTURES="arm64"
  VERSION_MIN_FLAG="-miphoneos-version-min=${IOS_MIN}"
fi

CC="$(xcrun --sdk "$PLATFORM" -f clang)"
CXX="$(xcrun --sdk "$PLATFORM" -f clang++)"
AR="$(xcrun --sdk "$PLATFORM" -f ar)"
RANLIB="$(xcrun --sdk "$PLATFORM" -f ranlib)"
IOS_CFLAGS="-arch arm64 -isysroot ${SDK_PATH} ${VERSION_MIN_FLAG}"
IOS_LDFLAGS="-arch arm64 -isysroot ${SDK_PATH} ${VERSION_MIN_FLAG}"
# Avoid leaking host env flags into cmake (breaks simulator vs device).
unset CFLAGS CXXFLAGS LDFLAGS

OPENSSL_PREFIX="$ROOT/openssl-$ARCH_SUFFIX"
SODIUM_PREFIX="$ROOT/libsodium-$ARCH_SUFFIX"
SDL_PREFIX="$ROOT/sdl-$ARCH_SUFFIX"

mkdir -p "$ROOT"
echo "== iOS deps: platform=$PLATFORM triple=$TRIPLE sdk=$SDK_PATH =="

# ---- OpenSSL ----
if [[ ! -f "$OPENSSL_PREFIX/lib/libssl.a" ]]; then
  echo "== OpenSSL =="
  OPENSSL_SRC="$SRC_ROOT/openssl-3.0.17"
  [[ -x "$OPENSSL_SRC/Configure" ]] || { echo "missing $OPENSSL_SRC"; exit 1; }
  rm -rf "$ROOT/openssl-build-$ARCH_SUFFIX"
  mkdir -p "$ROOT/openssl-build-$ARCH_SUFFIX"
  # OpenSSL in-tree Configure mutates the source tree; copy for isolation.
  rsync -a --delete --exclude=*.o --exclude=*.a --exclude=__pycache__ \
    "$OPENSSL_SRC/" "$ROOT/openssl-build-$ARCH_SUFFIX/"
  pushd "$ROOT/openssl-build-$ARCH_SUFFIX" >/dev/null
  # Let OpenSSL's ios*-xcrun targets drive the SDK; only pass arch + min version.
  unset CC CXX CFLAGS CXXFLAGS LDFLAGS
  # no-asm: ios64-xcrun assembly uses ELF .type/.size directives that Apple
  # clang rejects; pure-C OpenSSL is fine for MAMEHub netplay crypto volume.
  ./Configure "$OPENSSL_TARGET" \
    no-shared no-tests no-ui-console no-dso no-asm \
    --prefix="$OPENSSL_PREFIX" --openssldir="$OPENSSL_PREFIX/ssl" \
    "-arch arm64" "$VERSION_MIN_FLAG"
  make -j"$JOBS"
  make install_sw
  popd >/dev/null
else
  echo "OpenSSL already at $OPENSSL_PREFIX"
fi

# ---- libsodium ----
if [[ ! -f "$SODIUM_PREFIX/lib/libsodium.a" ]]; then
  echo "== libsodium =="
  SODIUM_SRC="$SRC_ROOT/libsodium-1.0.20"
  [[ -x "$SODIUM_SRC/configure" ]] || { echo "missing $SODIUM_SRC"; exit 1; }
  rm -rf "$ROOT/libsodium-build-$ARCH_SUFFIX"
  mkdir -p "$ROOT/libsodium-build-$ARCH_SUFFIX"
  # configure mutates the source tree; copy for isolation from Android builds.
  rsync -a --delete --exclude=*.o --exclude=*.a --exclude=.libs --exclude=__pycache__ \
    "$SODIUM_SRC/" "$ROOT/libsodium-src-$ARCH_SUFFIX/"
  if [[ -f "$ROOT/libsodium-src-$ARCH_SUFFIX/Makefile" ]]; then
    make -C "$ROOT/libsodium-src-$ARCH_SUFFIX" distclean >/dev/null 2>&1 || true
  fi
  pushd "$ROOT/libsodium-build-$ARCH_SUFFIX" >/dev/null
  CC="$(xcrun --sdk "$PLATFORM" -f clang)"
  CXX="$(xcrun --sdk "$PLATFORM" -f clang++)"
  export CC CXX AR RANLIB
  export CFLAGS="$IOS_CFLAGS" CXXFLAGS="$IOS_CFLAGS" LDFLAGS="$IOS_LDFLAGS"
  "$ROOT/libsodium-src-$ARCH_SUFFIX/configure" \
    --host=aarch64-apple-darwin \
    --prefix="$SODIUM_PREFIX" \
    --disable-shared --enable-static \
    --disable-dependency-tracking \
    CC="$CC" CXX="$CXX" \
    CFLAGS="$IOS_CFLAGS" CXXFLAGS="$IOS_CFLAGS" LDFLAGS="$IOS_LDFLAGS"
  make -j"$JOBS"
  make install
  popd >/dev/null
else
  echo "libsodium already at $SODIUM_PREFIX"
fi

# ---- SDL2 ----
if [[ ! -f "$SDL_PREFIX/lib/libSDL2.a" ]]; then
  echo "== SDL2 =="
  SDL_SRC="$SRC_ROOT/SDL2-2.32.10"
  [[ -f "$SDL_SRC/CMakeLists.txt" ]] || { echo "missing $SDL_SRC"; exit 1; }
  # Clear host LDFLAGS leaked from openssl/sodium so cmake can link for simulator.
  unset CFLAGS CXXFLAGS LDFLAGS
  rm -rf "$ROOT/sdl-build-$ARCH_SUFFIX"
  cmake -S "$SDL_SRC" -B "$ROOT/sdl-build-$ARCH_SUFFIX" \
    -DCMAKE_SYSTEM_NAME="$CMAKE_SYSTEM_NAME" \
    -DCMAKE_OSX_SYSROOT="$CMAKE_OSX_SYSROOT" \
    -DCMAKE_OSX_ARCHITECTURES="$CMAKE_OSX_ARCHITECTURES" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$IOS_MIN" \
    -DCMAKE_INSTALL_PREFIX="$SDL_PREFIX" \
    -DCMAKE_BUILD_TYPE=Release \
    -DSDL_SHARED=OFF \
    -DSDL_STATIC=ON \
    -DSDL_TEST=OFF \
    -DSDL_HIDAPI=ON
  cmake --build "$ROOT/sdl-build-$ARCH_SUFFIX" -j"$JOBS"
  cmake --install "$ROOT/sdl-build-$ARCH_SUFFIX"
else
  echo "SDL2 already at $SDL_PREFIX"
fi

echo "OK: openssl=$OPENSSL_PREFIX"
echo "OK: sodium=$SODIUM_PREFIX"
echo "OK: sdl=$SDL_PREFIX"
echo "export IOS_OPENSSL_ROOT=$OPENSSL_PREFIX"
echo "export IOS_SODIUM_ROOT=$SODIUM_PREFIX"
echo "export SDL_INSTALL_ROOT=$SDL_PREFIX"
