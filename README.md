# FOD native bridge Transsion 

![Build](https://github.com/USER/REPO/actions/workflows/build.yml/badge.svg)

## Cek
adb logcat -s fodbridge fodhook
 - "got SystemUI classloader" lalu "resolved OnScreenFingerprintUiMech + onFpTouch" lalu "listening on @fodhook"
 - tempel jari: "finger DOWN" (fodbridge) dan "onFpTouch(1)" (fodhook); lepas: "finger UP" dan "onFpTouch(0)"
Kalau "could not resolve OnScreenFingerprintUiMech": kirim OplusKeyguardDependencyEx.smali dan OplusBiometricAuthController.smali.
