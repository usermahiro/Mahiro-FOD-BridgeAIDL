# Tanam ke ROM

- Tidak perlu VINTF
- Library WAJIB di /system/lib64: fodinject memanggil dlopen dari namespace system, vendor/lib64 tidak bisa dibaca SystemUI.
- Label SELinux: bin = system_file, lib = system_lib_file
- Pakai SystemUI A17 stock

## Kalau service tidak jalan
adb logcat -b all -d | grep -iE "init.*(fodbridge|fodinject)|fodbridge|fodhook|fodinject"

## Nanti kalau enforcing
Perlu domain fodbridge dan fodinject: ptrace ke systemui_app, connectto ke socket SystemUI, baca /proc/fingerprint_status,
eksekusi `cmd`, dan binder ke system_server. Itu dikerjakan setelah semua jalan di permissive (audit dari dmesg).
