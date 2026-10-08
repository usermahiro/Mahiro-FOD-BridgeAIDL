// fodbridge

#include <android/log.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>

#define LOG_TAG "fodbridge"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

#define FOD_KEYCODE 195
static const char* TOUCH_NAME = "mtk-tpd";
static const char* g_devOverride = nullptr;

static const char* ACTION_DOWN = "com.rianixia.FINGER_DOWN";
static const char* ACTION_UP   = "com.rianixia.FINGER_UP";

static int openTouch() {
    if (g_devOverride) return open(g_devOverride, O_RDONLY | O_CLOEXEC);
    DIR* d = opendir("/dev/input");
    if (!d) return -1;
    int found = -1;
    while (dirent* e = readdir(d)) {
        if (strncmp(e->d_name, "event", 5) != 0) continue;
        std::string path = std::string("/dev/input/") + e->d_name;
        int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        char name[128] = {0};
        if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 0 && strcmp(name, TOUCH_NAME) == 0) {
            LOGI("touch device %s (%s)", path.c_str(), name);
            found = fd;
            break;
        }
        close(fd);
    }
    closedir(d);
    return found;
}

static std::string sh(const std::string& cmd) {
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return "";
    std::string o; char b[128];
    while (fgets(b, sizeof(b), p)) o += b;
    pclose(p);
    while (!o.empty() && (o.back() == '\n' || o.back() == '\r')) o.pop_back();
    return o;
}

// Fires the exact broadcast action FingerKeyReceiver.onReceive() switches on.
static void sendFingerBroadcast(bool down) {
    const char* action = down ? ACTION_DOWN : ACTION_UP;
    sh(std::string("cmd activity broadcast --user 0 -a ") + action);
    LOGI("broadcast %s", action);
}

static void onEdge(int st) {
    sendFingerBroadcast(st != 0);
    LOGI("finger %s", st ? "DOWN" : "UP");
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--dev") && i + 1 < argc) g_devOverride = argv[++i];
    }
    LOGI("fodbridge started (broadcast-only, matched to FingerKeyReceiver)");

    int last = -1;
    for (;;) {
        int fd;
        while ((fd = openTouch()) < 0) { sleep(2); }
        input_event ev;
        for (;;) {
            ssize_t n = read(fd, &ev, sizeof(ev));
            if (n == (ssize_t)sizeof(ev)) {
                if (ev.type == EV_KEY && ev.code == FOD_KEYCODE && ev.value != 2) {
                    int st = ev.value ? 1 : 0;
                    if (st != last) { last = st; onEdge(st); }
                }
            } else if (n < 0 && errno == EINTR) {
                continue;
            } else {
                LOGI("touch read ended (n=%zd errno=%d), reopening", n, errno);
                break;
            }
        }
        close(fd);
        if (last == 1) { last = 0; onEdge(0); }
        sleep(1);
    }
    return 0;
}
