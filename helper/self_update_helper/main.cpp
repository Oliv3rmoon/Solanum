// ps5-native-app-boilerplate - The self-update helper, a payload for the console's loader.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Solanum: Nightshade's copy (Oliv3rmoon/Nightshade, headless/self_update_helper), with Solanum's
// data folder for its log and for the version an update replaces, and without Nightshade's
// launcher check (launcher_data stays empty, so launcher_update_compatible always agrees).
//
// The app sends this program to the payload loader (loopback port 9021). The
// loader starts it and hands it the same connection as its standard input and
// output, where it speaks self_update_protocol.h. It stays alive after the
// app has closed, which is when it puts the new files in place.
#include "updater.hpp"
#include <csignal>
#include <cstdint>
#include <string>
#include <cstdio>
#include <cstring>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C"
{
    int sceKernelSendNotificationRequest(int device, void *request, std::size_t size, int blocking);
    int klog_puts(const char *text);
    // ProsperoEden: libkernel's module loader, to reach sceLncUtil when an install asks which app
    // runs (running_app below). Weak, so that no loader can refuse to start the helper over them:
    // the app's own update depends on it starting and never asks.
    __attribute__((weak)) int sceKernelLoadStartModule(const char *path, std::size_t argc, const void *argv,
                                                       std::uint32_t flags, void *option, int *result);
    __attribute__((weak)) int sceKernelDlsym(int module, const char *name, void **address);
    // Solanum: the payload SDK's lookup of a loaded module's function (ps5/kernel.h), for the
    // app-install service when sceKernelDlsym can't find it. Weak, as above.
    __attribute__((weak)) std::intptr_t kernel_dynlib_dlsym(int pid, unsigned int handle, const char *name);
    __attribute__((weak)) int sceUserServiceInitialize(void *parameters);
}

namespace
{
struct NotificationRequest
{
    std::uint8_t reserved[45];
    char message[3075];
};

void notify(const std::string &message)
{
    static NotificationRequest request;
    std::memset(&request, 0, sizeof(request));
    std::snprintf(request.message, sizeof(request.message), "%s", message.c_str());
    (void)sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
}

bool free_space(const std::string &path, std::uint64_t &bytes)
{
    struct statfs info
    {
    };
    if (statfs(path.c_str(), &info) != 0 || info.f_bavail < 0)
        return false;
    bytes = static_cast<std::uint64_t>(info.f_bavail) * static_cast<std::uint64_t>(info.f_bsize);
    return true;
}
} // namespace

int main()
{
    std::signal(SIGPIPE, SIG_IGN);
    self_update::Environment environment;
    environment.roots = self_update::default_roots();
    environment.drives = self_update::default_drives();
    environment.space = free_space;
    environment.notify = notify;
    // The version an update replaces stays here until the next update.
    environment.previous = "/data/solanum/previous-version";
    // ProsperoEden: for an install of another app (Nightshade's Apps): which big app runs now, read
    // as Mihawk's PS5 RetroArch forwarder reads it. libSceSystemService is loaded and its two
    // functions looked up here, the first time an install asks, not linked: the helper imports
    // nothing it did not before. Anything missing is "can't be told", and the install refuses.
    environment.running_app = [&environment](std::string &title)
    {
        using RunningBigApp = std::uint32_t (*)();
        using AppTitleId = int (*)(std::uint32_t app_id, char *title_id);
        static RunningBigApp running = nullptr;
        static AppTitleId title_of = nullptr;
        static bool looked = false;
        if (!looked)
        {
            looked = true;
            int module = -1;
            if (sceKernelLoadStartModule != nullptr && sceKernelDlsym != nullptr)
                for (const char *path : {"/system/common/lib/libSceSystemService.sprx", "libSceSystemService.sprx"})
                    if ((module = sceKernelLoadStartModule(path, 0, nullptr, 0, nullptr, nullptr)) >= 0)
                        break;
            if (module >= 0 &&
                (sceKernelDlsym(module, "sceLncUtilGetAppIdOfRunningBigApp", reinterpret_cast<void **>(&running)) != 0 ||
                 sceKernelDlsym(module, "sceLncUtilGetAppTitleId", reinterpret_cast<void **>(&title_of)) != 0))
                running = nullptr;
            environment.log(running != nullptr && title_of != nullptr
                                ? "sceLncUtil found in libSceSystemService"
                                : "sceLncUtil could not be reached (module " + std::to_string(module) + ")");
        }
        if (running == nullptr || title_of == nullptr)
            return false;
        const std::uint32_t app = running();
        char id[16] = {};
        if (app == 0 || app == 0xffffffffu || title_of(app, id) != 0)
            return false;
        id[15] = '\0';
        title = id;
        return true;
    };
    // Solanum: an uninstall takes the app off the home screen as the console's own uninstall does,
    // through its app-install service (libSceAppInstUtil). As ProsperoStore found, a payload that
    // imports that library doesn't start on some firmware, so it is loaded by path when an uninstall
    // asks, and its two functions looked up then. Anything missing is a code, never a crash.
    environment.unregister = [&environment](const std::string &title) -> int
    {
        using Initialize = int (*)();
        using Uninstall = int (*)(const char *title_id);
        static Initialize initialize = nullptr;
        static Uninstall uninstall = nullptr;
        static int module = -1;
        if (module < 0)
        {
            if (sceKernelLoadStartModule == nullptr)
                return -2;
            if (sceUserServiceInitialize != nullptr)
                (void)sceUserServiceInitialize(nullptr);
            for (const char *path : {"/system/common/lib/libSceAppInstUtil.sprx", "libSceAppInstUtil.sprx"})
                if ((module = sceKernelLoadStartModule(path, 0, nullptr, 0, nullptr, nullptr)) >= 0)
                    break;
            if (module < 0)
            {
                environment.log("libSceAppInstUtil could not be loaded (" + std::to_string(module) + ")");
                return module;
            }
            const auto find = [](const char *name) -> void *
            {
                void *address = nullptr;
                if (sceKernelDlsym != nullptr && sceKernelDlsym(module, name, &address) == 0 && address != nullptr)
                    return address;
                if (kernel_dynlib_dlsym != nullptr)
                    return reinterpret_cast<void *>(
                        kernel_dynlib_dlsym(getpid(), static_cast<unsigned int>(module), name));
                return nullptr;
            };
            initialize = reinterpret_cast<Initialize>(find("sceAppInstUtilInitialize"));
            uninstall = reinterpret_cast<Uninstall>(find("sceAppInstUtilAppUnInstall"));
            environment.log(initialize != nullptr && uninstall != nullptr
                                ? "sceAppInstUtil found in libSceAppInstUtil"
                                : "sceAppInstUtil's functions could not be found");
            if (initialize != nullptr)
            {
                const int ready = initialize();
                if (ready < 0)
                    environment.log("sceAppInstUtilInitialize answered " + std::to_string(ready));
            }
        }
        if (uninstall == nullptr)
            return -3;
        return uninstall(title.c_str());
    };
    // ProsperoEden: the kernel log, and (Solanum) /data/solanum/logs/updater.log (the last install's
    // lines, for a player to send: the kernel log is out of reach for most).
    // Solanum: several helpers can work at once (the download queue), so each adds to the log, its
    // lines marked with its process; a log grown past a megabyte starts again.
    struct stat log_info
    {
    };
    const bool too_long = stat("/data/solanum/logs/updater.log", &log_info) == 0 && log_info.st_size > (1 << 20);
    static std::FILE *file = std::fopen("/data/solanum/logs/updater.log", too_long ? "w" : "a");
    environment.log = [](const std::string &line)
    {
        (void)klog_puts(("[self-update] " + line).c_str());
        if (file)
        {
            std::fprintf(file, "[%d] %s\n", static_cast<int>(getpid()), line.c_str());
            std::fflush(file);
        }
    };
    const self_update::Io io{[](void *data, std::size_t size)
                             { return static_cast<long>(read(STDIN_FILENO, data, size)); },
                             [](const void *data, std::size_t size)
                             { return static_cast<long>(write(STDOUT_FILENO, data, size)); }};
    return self_update::run(environment, io);
}
