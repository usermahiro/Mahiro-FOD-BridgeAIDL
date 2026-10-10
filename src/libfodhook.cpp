// libfodhook.so
#include <jni.h>
#include <dlfcn.h>
#include <unistd.h>
#include <string.h>
#include <stddef.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <functional>
#include <android/log.h>

#define LOG_TAG "fodhook"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static const char* MECH_TYPE = "com.oplus.systemui.biometrics.finger.udfps.OnScreenFingerprintUiMech";

typedef jint (*GetVMs_t)(JavaVM**, jsize, jsize*);
static JavaVM* getVm() {
    const char* libs[] = {"libnativehelper.so", "libart.so", "libandroid_runtime.so"};
    for (const char* n : libs) {
        void* h = dlopen(n, RTLD_NOW | RTLD_GLOBAL);
        if (!h) continue;
        GetVMs_t f = (GetVMs_t)dlsym(h, "JNI_GetCreatedJavaVMs");
        if (!f) continue;
        JavaVM* vm = nullptr; jsize cnt = 0;
        if (f(&vm, 1, &cnt) == JNI_OK && cnt > 0 && vm) { LOGI("JavaVM via %s", n); return vm; }
    }
    return nullptr;
}

static bool clr(JNIEnv* env) {
    if (!env->ExceptionCheck()) return false;
    env->ExceptionClear();
    return true;
}

static jobject callNoArg(JNIEnv* env, jobject obj, const char* name) {
    jclass cClass = env->FindClass("java/lang/Class");
    jclass cMethod = env->FindClass("java/lang/reflect/Method");
    jmethodID mGetMethod = env->GetMethodID(cClass, "getMethod",
        "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;");
    jmethodID mInvoke = env->GetMethodID(cMethod, "invoke",
        "(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;");
    jclass cObj = env->GetObjectClass(obj);
    jstring s = env->NewStringUTF(name);
    jobject m = env->CallObjectMethod(cObj, mGetMethod, s, (jobjectArray)nullptr);
    if (clr(env) || !m) return nullptr;
    jobject r = env->CallObjectMethod(m, mInvoke, obj, (jobjectArray)nullptr);
    if (clr(env)) return nullptr;
    return r;
}

static jobject fieldOfType(JNIEnv* env, jobject obj, const char* typeName) {
    jclass cClass = env->FindClass("java/lang/Class");
    jclass cField = env->FindClass("java/lang/reflect/Field");
    jmethodID mGetDF = env->GetMethodID(cClass, "getDeclaredFields", "()[Ljava/lang/reflect/Field;");
    jmethodID mSuper = env->GetMethodID(cClass, "getSuperclass", "()Ljava/lang/Class;");
    jmethodID mCName = env->GetMethodID(cClass, "getName", "()Ljava/lang/String;");
    jmethodID mType  = env->GetMethodID(cField, "getType", "()Ljava/lang/Class;");
    jmethodID mAcc   = env->GetMethodID(cField, "setAccessible", "(Z)V");
    jmethodID mGet   = env->GetMethodID(cField, "get", "(Ljava/lang/Object;)Ljava/lang/Object;");
    jclass c = env->GetObjectClass(obj);
    while (c) {
        jobjectArray fs = (jobjectArray)env->CallObjectMethod(c, mGetDF);
        if (clr(env)) return nullptr;
        jsize n = fs ? env->GetArrayLength(fs) : 0;
        for (jsize i = 0; i < n; ++i) {
            jobject f = env->GetObjectArrayElement(fs, i);
            jobject t = env->CallObjectMethod(f, mType);
            jstring nm = t ? (jstring)env->CallObjectMethod(t, mCName) : nullptr;
            bool match = false;
            if (nm) {
                const char* u = env->GetStringUTFChars(nm, nullptr);
                match = u && strcmp(u, typeName) == 0;
                env->ReleaseStringUTFChars(nm, u);
            }
            if (match) {
                env->CallVoidMethod(f, mAcc, JNI_TRUE);
                jobject v = env->CallObjectMethod(f, mGet, obj);
                if (!clr(env) && v) return v;
            }
            clr(env);
        }
        c = (jclass)env->CallObjectMethod(c, mSuper);
        if (clr(env)) break;
    }
    return nullptr;
}

static void* worker(void*) {
    usleep(500 * 1000);
    JavaVM* vm = getVm();
    if (!vm) { LOGE("no JavaVM"); return nullptr; }
    JNIEnv* env = nullptr;
    jint att = vm->AttachCurrentThread(&env, nullptr);
    if (att != JNI_OK || !env) { LOGE("attach failed"); return nullptr; }

    jobject gCL = nullptr; jmethodID mLoad = nullptr;
    for (int i = 0; i < 600 && !gCL; ++i) {
        clr(env);
        jclass cAT = env->FindClass("android/app/ActivityThread");
        if (!cAT) { clr(env); usleep(500000); continue; }
        jmethodID mCur = env->GetStaticMethodID(cAT, "currentApplication", "()Landroid/app/Application;");
        if (!mCur) { clr(env); usleep(500000); continue; }
        jobject app = env->CallStaticObjectMethod(cAT, mCur);
        if (clr(env) || !app) { usleep(500000); continue; }
        jmethodID mGetCL = env->GetMethodID(env->GetObjectClass(app), "getClassLoader", "()Ljava/lang/ClassLoader;");
        if (!mGetCL) { clr(env); usleep(500000); continue; }
        jobject cl = env->CallObjectMethod(app, mGetCL);
        if (!cl) { usleep(500000); continue; }
        mLoad = env->GetMethodID(env->GetObjectClass(cl), "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
        if (!mLoad) { clr(env); usleep(500000); continue; }
        gCL = env->NewGlobalRef(cl);
        LOGI("got SystemUI classloader");
    }
    if (!gCL) { LOGE("no app classloader"); return nullptr; }

    auto loadClass = [&](const char* name) -> jclass {
        jstring s = env->NewStringUTF(name);
        jobject c = env->CallObjectMethod(gCL, mLoad, s);
        env->DeleteLocalRef(s);
        if (clr(env)) return nullptr;
        return (jclass)c;
    };

    // Align FOD icon with the physical sensor: read /sys/kernel/tran_fp/fod_location_xy
// (centers, "x,y" from top) and set KeyguardFingerprintUtils.iconMarginBottom to
// displayHeight - sensorY so the icon center sits exactly on the sensor.
auto applyMargin = [&]() {
    jclass cKFU = loadClass("com.oplus.systemui.biometrics.finger.KeyguardFingerprintUtils");
    if (!cKFU) return;
    int sensor = -1;
    int height = -1;
    FILE* f = fopen("/sys/kernel/tran_fp/fod_location_xy", "r");
    if (f) {
        int x = -1;
        if (fscanf(f, "%d,%d", &x, &sensor) < 2) sensor = -1;
        fclose(f);
    }
    f = fopen("/sys/class/graphics/fb0/virtual_size", "r");
    if (f) {
        int w = -1;
        if (fscanf(f, "%d,%d", &w, &height) < 2) height = -1;
        if (height < 0) { rewind(f); char c; if (fscanf(f, "%d %c %d", &w, &c, &height) < 3) height = -1; }
        fclose(f);
    }
    jobject app2 = nullptr;
    if (height <= 0) {
        // fallback: real display height from the SystemUI context
        jclass cAT2 = loadClass("android.app.ActivityThread");
        jmethodID mCur2 = cAT2 ? env->GetStaticMethodID(cAT2, "currentApplication",
            "()Landroid/app/Application;") : nullptr;
        if (mCur2) app2 = env->CallStaticObjectMethod(cAT2, mCur2);
        clr(env);
    }
    if (height <= 0 && app2) {
        jmethodID mGetRes = env->GetMethodID(env->GetObjectClass(app2),
            "getResources", "()Landroid/content/res/Resources;");
        jobject res = mGetRes ? env->CallObjectMethod(app2, mGetRes) : nullptr;
        jfieldID fH = 0;
        if (res && !clr(env)) {
            jmethodID mGetDM = env->GetMethodID(env->GetObjectClass(res),
                "getDisplayMetrics", "()Landroid/util/DisplayMetrics;");
            jobject dm = mGetDM ? env->CallObjectMethod(res, mGetDM) : nullptr;
            if (dm && !clr(env)) {
                fH = env->GetFieldID(env->GetObjectClass(dm), "heightPixels", "I");
                if (fH) height = env->GetIntField(dm, fH);
            }
        }
        clr(env);
    }
    if (sensor <= 0 || height <= 0) {
        LOGE("could not read fod_location_xy (%d) / height (%d)", sensor, height);
        return;
    }
    int margin = height - sensor;
    if (margin < 0) margin = 0;
    jfieldID fA = env->GetStaticFieldID(cKFU, "iconMarginBottom", "I");
    jfieldID fB = env->GetStaticFieldID(cKFU, "iconMarginBottomProp", "I");
    if (fA) env->SetStaticIntField(cKFU, fA, margin);
    if (fB) env->SetStaticIntField(cKFU, fB, margin);
    clr(env);
    LOGI("iconMarginBottom overridden -> %d px (h=%d y=%d)", margin, height, sensor);
};
applyMargin();

// Fix screen blackout on Lockscreen: force isDisableAppDimLayer -> true
    {
        jclass cOpt = loadClass("com.oplusos.systemui.common.feature.KeyguardFeatureOption");
        if (cOpt) {
            jfieldID fDel = env->GetStaticFieldID(cOpt, "isDisableAppDimLayer$delegate", "Lkotlin/Lazy;");
            if (fDel) {
                jobject del = env->GetStaticObjectField(cOpt, fDel);
                if (del) {
                    jclass cLazy = env->GetObjectClass(del);
                    jfieldID fVal = env->GetFieldID(cLazy, "_value", "Ljava/lang/Object;");
                    jfieldID fInit = env->GetFieldID(cLazy, "initializer", "Lkotlin/jvm/functions/Function0;");
                    jclass cBool = env->FindClass("java/lang/Boolean");
                    if (cBool) {
                        jfieldID fTrue = env->GetStaticFieldID(cBool, "TRUE", "Ljava/lang/Boolean;");
                        if (fTrue) {
                            jobject bTrue = env->GetStaticObjectField(cBool, fTrue);
                            if (fVal && bTrue) env->SetObjectField(del, fVal, bTrue);
                            if (fInit) env->SetObjectField(del, fInit, nullptr);
                            LOGI("KeyguardFeatureOption.isDisableAppDimLayer forced to TRUE (Dim Layer DISABLED)");
                        }
                    }
                }
            }
            clr(env);
        }
    }

    jobject gMech = nullptr; jmethodID mTouch = nullptr;
    for (int tries = 0; tries < 600 && !gMech; ++tries) {
        env->PushLocalFrame(256);
        do {
            jclass cDep = loadClass("com.android.systemui.DependencyEx");
            if (!cDep) break;
            jfieldID fS = env->GetStaticFieldID(cDep, "sDependency", "Lcom/android/systemui/DependencyEx;");
            if (!fS) { clr(env); break; }
            jobject dep = env->GetStaticObjectField(cDep, fS);
            if (!dep) break;
            jmethodID mGetDep = env->GetMethodID(cDep, "getDependency", "(Ljava/lang/Class;)Ljava/lang/Object;");
            jclass cOk = loadClass("com.android.keyguard.OplusKeyguardDependencyEx");
            if (!mGetDep || !cOk) { clr(env); break; }
            jobject ok = env->CallObjectMethod(dep, mGetDep, cOk);
            if (clr(env) || !ok) break;
            jobject ctrl = callNoArg(env, ok, "getOplusBiometricAuthController");
            if (!ctrl) break;
            jobject mech = fieldOfType(env, ctrl, MECH_TYPE);
            if (!mech) break;
            jmethodID m = env->GetMethodID(env->GetObjectClass(mech), "onFpTouch", "(Z)V");
            if (!m) { clr(env); break; }
            gMech = env->NewGlobalRef(mech);
            mTouch = m;
        } while (false);
        env->PopLocalFrame(nullptr);
        if (!gMech) usleep(500000);
    }
    if (!gMech) { LOGE("could not resolve OnScreenFingerprintUiMech"); return nullptr; }
    LOGI("resolved OnScreenFingerprintUiMech + onFpTouch");
    applyMargin();

    int s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    sockaddr_un a; 
    memset(&a, 0, sizeof(a));
    a.sun_family = AF_UNIX;
    const char* nm = "fodhook";
    a.sun_path[0] = '\0';
    memcpy(a.sun_path + 1, nm, strlen(nm));
    socklen_t len = offsetof(sockaddr_un, sun_path) + 1 + strlen(nm);

    if (s < 0 || bind(s, (sockaddr*)&a, len) != 0 || listen(s, 4) != 0) { 
        LOGE("socket bind/listen failed (%s)", strerror(errno)); 
        if (s >= 0) close(s);
        return nullptr; 
    }
    LOGI("listening on @fodhook");

    for (;;) {
        int c = accept(s, nullptr, nullptr);
        if (c < 0) { usleep(100000); continue; }
        char b;
        while (read(c, &b, 1) == 1) {
            if (b != '1' && b != '0') continue;
            env->CallVoidMethod(gMech, mTouch, (jboolean)(b == '1'));
            if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
            LOGI("onFpTouch(%c)", b);
        }
        close(c);
        env->CallVoidMethod(gMech, mTouch, (jboolean)JNI_FALSE);
        clr(env);
    }
    return nullptr;
}

__attribute__((constructor))
static void on_load() {
    LOGI("MahiroHook");
    pthread_t t;
    pthread_create(&t, nullptr, worker, nullptr);
    pthread_detach(t);
}
