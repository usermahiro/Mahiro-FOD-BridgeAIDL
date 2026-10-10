# FOD Native Bridge Transsion (ColorOS 17 / OPLUS ROM)

[![Build](https://github.com/usermahiro/Mahiro-FOD-BridgeAIDL/actions/workflows/build.yml/badge.svg)](https://github.com/usermahiro/Mahiro-FOD-BridgeAIDL/actions/workflows/build.yml)

Implementasi native bridge dan HAL untuk mengaktifkan Fingerprint on Display (FOD) pada perangkat Transsion (Infinix/Tecno) yang menjalankan ColorOS 17 / OxygenOS / OPLUS ROM **tanpa perlu memodifikasi atau decompile Smali SystemUI**.

---

## Arsitektur & Cara Kerja

Solusi ini menggabungkan HAL Transsion AOSP dengan native runtime injection:
1. **Transsion AOSP HAL (`vendor/`)**: Menangani hardware sensor fingerprint Goodix/Transsion.
2. **`fodbridge`**: Daemon yang mendengarkan event sentuh jari (`EV_KEY 195` / `0x00c3`) dari driver touchscreen (`/dev/input/event4`), mengirimkan trigger ke socket `@fodhook`, mengaktifkan HBM panel (`lcm_hbm_state`), dan menyiarkan broadcast async.
3. **`fodinject`**: Injector ptrace yang memuat `libfodhook.so` ke dalam proses `com.android.systemui` secara aman melalui namespace linker default/system.
4. **`libfodhook.so`**: Library runtime hook di dalam SystemUI yang:
   - Memanggil `OnScreenFingerprintUiMech.onFpTouch(boolean)` via JNI.
   - Mem-bypass blackout overlay dengan memaksa `KeyguardFeatureOption.isDisableAppDimLayer` bernilai `true`.

---

## Langkah Implementasi ke ROM

### 1. Salin Vendor HAL & Lib Transsion
Salin seluruh file dari folder `vendor/` di repo ini ke partisi `/vendor` perangkat:

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
Cari file library `hwcomposer.*.so` di `/vendor/lib64/hw/` (misal `hwcomposer.mt6893.so` atau `hwcomposer.mtkcommon.so`), lalu lakukan patch string layer HBM:

```bash
perl -pi -e 's/OnScreenFingerprintDimLayer/VRI[RianixiaHBMController]\x00/g' /vendor/lib64/hw/hwcomposer.*.so
```

---

### 3. Pasang Native Bridge Components
Download artifact `fod-native.zip` dari [GitHub Actions](https://github.com/usermahiro/Mahiro-FOD-BridgeAIDL/actions) atau compile sendiri menggunakan `./build.sh`.

Letakkan file-file berikut ke ROM:

- **System Partition:**
  - `/system/lib64/libfodhook.so` (chmod `0644`, SELinux context: `u:object_r:system_lib_file:s0`)
  - `/system/etc/init/fodnative.rc` (chmod `0644`, SELinux context: `u:object_r:system_file:s0`)
- **Vendor Partition:**
  - `/vendor/bin/hw/android.hardware.biometrics.fingerprint-servicemahiroaidl` (chmod `0755`, SELinux context: `u:object_r:system_file:s0`)
  - `/vendor/bin/hw/android.hardware.biometrics.fingerprint-injectmahiroaidl` (chmod `0755`, SELinux context: `u:object_r:system_file:s0`)

---

### 4. SELinux & Boot Awal
Untuk boot awal, pastikan menjalankan SELinux dalam mode **Permissive** (`setenforce 0` atau cmdline `androidboot.selinux=permissive`).

---

## Verifikasi & Debug

Pantau logcat saat perangkat menyala dan jari ditempel ke sensor:

```bash
adb logcat -s fodbridge fodhook fodinject
```

**Log yang diharapkan:**
1. Saat boot:
   - `fodinject`: `injecting into com.android.systemui ... OK`
   - `fodhook`: `got SystemUI classloader` -> `KeyguardFeatureOption.isDisableAppDimLayer forced to TRUE` -> `resolved OnScreenFingerprintUiMech + onFpTouch` -> `listening on @fodhook`
2. Saat jari ditempel:
   - `fodbridge`: `finger DOWN (sent to socket & broadcast)`
   - `fodhook`: `onFpTouch(1)`
3. Saat jari dilepas:
   - `fodbridge`: `finger UP (sent to socket & broadcast)`
   - `fodhook`: `onFpTouch(0)`

---

## Build Mandiri

Membutuhkan Android NDK r27c (aarch64 API 31):

```bash
export NDK=/path/to/android-ndk-r27c
./build.sh
```

---

## Credits
- [irawansalt & fajarxtr](https://github.com/irawansalt) untuk Transsion AOSP FOD HALs
- [ryanistr / rianixia](https://github.com/ryanistr) untuk riset implementasi OPLUS FOD
- [Cartethyiaaa / usermahiro](https://github.com/usermahiro) untuk implementasi Native Bridge & AIDL Hook

