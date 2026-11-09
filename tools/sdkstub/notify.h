// ATTRAPPE - siehe tools/sdkstub/README.md
#ifndef EG3D_STUB_NOTIFY_H
#define EG3D_STUB_NOTIFY_H

#include "max.h"

struct NotifyInfo {
    int intcode = 0;
    void* callParam = nullptr;
};

using NOTIFYPROC = void (*)(void* param, NotifyInfo* info);

#define NOTIFY_SYSTEM_STARTUP  1
#define NOTIFY_SYSTEM_SHUTDOWN 2
#define NOTIFY_FILE_POST_OPEN  3

inline int RegisterNotification(NOTIFYPROC, void*, int) { return 1; }
inline int UnRegisterNotification(NOTIFYPROC, void*, int) { return 1; }

#endif  // EG3D_STUB_NOTIFY_H
