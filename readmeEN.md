# Transsion FOD Native Bridge (ColorOS 17 / OPLUS ROM)

[![Build](https://github.com/usermahiro/Mahiro-FOD-BridgeAIDL/actions/workflows/build.yml/badge.svg)](https://github.com/usermahiro/Mahiro-FOD-BridgeAIDL/actions/workflows/build.yml)

Native bridge and HAL integration to enable Fingerprint on Display (FOD) on Transsion devices (Infinix/Tecno) running ColorOS 17 / OxygenOS / OPLUS ROMs **without decompiling or patching SystemUI Smali**.

---

## Architecture & How It Works

This solution combines Transsion AOSP HAL with native runtime injection:
1. **Transsion AOSP HAL (`vendor/`)**: Handles Goodix/Transsion fingerprint sensor hardware communication.
2. **`fodbridge`**: Daemon that monitors touch events (`EV_KEY 195` / `0x00c3`) from the touchscreen input device (`/dev/input/event4`), dispatches triggers via unix socket `@fodhook`, activates the display HBM panel node (`lcm_hbm_state`), and fires async broadcasts.
3. **`fodinject`**: Ptrace injector that safely loads `libfodhook.so` into the `com.android.systemui` process using the default/system linker namespace.
4. **`libfodhook.so`**: Injected runtime hook inside SystemUI that:
   - Invokes `OnScreenFingerprintUiMech.onFpTouch(boolean)` via JNI.
   - Bypasses screen blackout overlay by forcing `KeyguardFeatureOption.isDisableAppDimLayer` to `true`.

---

## Implementation Steps for Custom ROMs

### 1. Copy Vendor HAL & Transsion Libraries
Copy all files from the `vendor/` directory in this repository into your target device `/vendor` partition:

- **Binaries:**
  - `/vendor/bin/hw/android.hardware.biometrics.fingerprint@2.3-service.transsion` (chmod `0755`)
  - `/vendor/bin/hw/fp_hal` (chmod `0755`)
- **Libraries:**
  - `/vendor/lib64/android.hardware.biometrics.fingerprint@2.1.so` (chmod `0644`)
  - `/vendor/lib64/android.hardware.biometrics.fingerprint@2.2.so` (chmod `0644`)
  - `/vendor/lib64/android.hardware.biometrics.fingerprint@2.3.so` (chmod `0644`)
  - `/vendor/lib64/libc++.so` (chmod `0644`)
- **VINTF Manifest & Init RC:**
  - `/vendor/etc/vintf/manifest/android.hardware.biometrics.fingerprint@2.3-service.transsion.xml` (chmod `0644`)
  - `/vendor/etc/init/fp_hal.rc` (chmod `0644`)

---

### 2. Patch HWComposer
Locate the `hwcomposer.*.so` library in `/vendor/lib64/hw/` (e.g., `hwcomposer.mt6893.so` or `hwcomposer.mtkcommon.so`), then patch the HBM layer title string:

```bash
perl -pi -e 's/OnScreenFingerprintDimLayer/VRI[RianixiaHBMController]\x00/g' /vendor/lib64/hw/hwcomposer.*.so
```

---

### 3. Install Native Bridge Components
Download the `fod-native.zip` artifact from [GitHub Actions](https://github.com/usermahiro/Mahiro-FOD-BridgeAIDL/actions) or compile manually using `./build.sh`.

Deploy the following files into your ROM partitions:

- **System Partition:**
  - `/system/lib64/libfodhook.so` (chmod `0644`, SELinux context: `u:object_r:system_lib_file:s0`)
  - `/system/etc/init/fodnative.rc` (chmod `0644`, SELinux context: `u:object_r:system_file:s0`)
- **Vendor Partition:**
  - `/vendor/bin/hw/android.hardware.biometrics.fingerprint-servicemahiroaidl` (chmod `0755`, SELinux context: `u:object_r:system_file:s0`)
  - `/vendor/bin/hw/android.hardware.biometrics.fingerprint-injectmahiroaidl` (chmod `0755`, SELinux context: `u:object_r:system_file:s0`)

---

### 4. SELinux & Initial Boot
For initial bring-up, ensure SELinux is set to **Permissive** (`setenforce 0` or kernel cmdline `androidboot.selinux=permissive`).

---

## Verification & Debugging

Monitor logcat while booting and placing your finger on the FOD area:

```bash
adb logcat -s fodbridge fodhook fodinject
```

**Expected Log Output:**
1. During boot:
   - `fodinject`: `injecting into com.android.systemui ... OK`
   - `fodhook`: `got SystemUI classloader` -> `KeyguardFeatureOption.isDisableAppDimLayer forced to TRUE` -> `resolved OnScreenFingerprintUiMech + onFpTouch` -> `listening on @fodhook`
2. When finger touches sensor:
   - `fodbridge`: `finger DOWN (sent to socket & broadcast)`
   - `fodhook`: `onFpTouch(1)`
3. When finger is lifted:
   - `fodbridge`: `finger UP (sent to socket & broadcast)`
   - `fodhook`: `onFpTouch(0)`

---

## Building from Source

Requires Android NDK r27c (aarch64 API 31):

```bash
export NDK=/path/to/android-ndk-r27c
./build.sh
```

---

## Credits
- [irawansalt & fajarxtr](https://github.com/irawansalt) for Transsion AOSP FOD HALs
- [ryanistr / rianixia](https://github.com/ryanistr) for OPLUS FOD implementation research
- [Cartethyiaaa / usermahiro](https://github.com/usermahiro) for Native Bridge & AIDL Hook implementation
