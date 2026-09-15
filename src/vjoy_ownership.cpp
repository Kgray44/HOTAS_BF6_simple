#include "vjoy_ownership.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <mutex>

namespace hotas {
namespace {

using GetVJDStatusFn = int(__cdecl *)(UINT);
using GetOwnerPidFn = DWORD(__cdecl *)(UINT);

struct VJoyOwnershipApi {
    HMODULE library = nullptr;
    GetVJDStatusFn getStatus = nullptr;
    GetOwnerPidFn getOwnerPid = nullptr;
    QString error;
};

VJoyOwnershipApi &ownershipApi()
{
    static VJoyOwnershipApi api;
    static std::once_flag initialized;
    std::call_once(initialized, [] {
        QStringList candidates{QStringLiteral("vJoyInterface.dll")};
        for (const QString &root : {qEnvironmentVariable("ProgramW6432"),
                                    qEnvironmentVariable("ProgramFiles"),
                                    QStringLiteral("C:/Program Files")}) {
            if (!root.isEmpty()) candidates.append(root + QStringLiteral("/vJoy/x64/vJoyInterface.dll"));
        }
        candidates.removeDuplicates();
        for (const QString &candidate : candidates) {
            const std::wstring path = QDir::toNativeSeparators(candidate).toStdWString();
            api.library = LoadLibraryW(path.c_str());
            if (api.library) break;
        }
        if (!api.library) {
            api.error = QStringLiteral("vJoyInterface.dll could not be loaded for ownership inspection");
            return;
        }
        api.getStatus = reinterpret_cast<GetVJDStatusFn>(GetProcAddress(api.library, "GetVJDStatus"));
        api.getOwnerPid = reinterpret_cast<GetOwnerPidFn>(GetProcAddress(api.library, "GetOwnerPid"));
        if (!api.getStatus) api.error = QStringLiteral("vJoyInterface.dll does not expose GetVJDStatus");
    });
    return api;
}

} // namespace

QString vjoyRawStatusName(int rawStatus)
{
    switch (rawStatus) {
    case kVJoyStatusOwn: return QStringLiteral("OWN");
    case kVJoyStatusFree: return QStringLiteral("FREE");
    case kVJoyStatusBusy: return QStringLiteral("BUSY");
    case kVJoyStatusMissing: return QStringLiteral("MISS");
    default: return QStringLiteral("UNKNOWN");
    }
}

QString vjoyOwnershipStateName(VJoyOwnershipState state)
{
    switch (state) {
    case VJoyOwnershipState::OwnedByCurrentProcess: return QStringLiteral("OWNED BY HOTAS BF6");
    case VJoyOwnershipState::Free: return QStringLiteral("FREE");
    case VJoyOwnershipState::BusyOtherProcess: return QStringLiteral("BUSY BY OTHER PROCESS");
    case VJoyOwnershipState::StaleOwnership: return QStringLiteral("STALE OWNERSHIP / DRIVER STATE");
    case VJoyOwnershipState::Missing: return QStringLiteral("MISS");
    case VJoyOwnershipState::Unknown: return QStringLiteral("UNKNOWN");
    }
    return QStringLiteral("UNKNOWN");
}

QString vjoyOwnershipOwnerLabel(const VJoyOwnershipEvidence &evidence)
{
    if (evidence.ownerPid == 0) return QStringLiteral("no owner PID reported");
    QString label = evidence.ownerProcess.name;
    if (label.isEmpty()) label = QStringLiteral("process identity unavailable");
    return QStringLiteral("%1 (PID %2)").arg(label).arg(evidence.ownerPid);
}

VJoyOwnerProcessEvidence inspectVJoyOwnerProcess(quint64 pid)
{
    VJoyOwnerProcessEvidence result;
    if (pid == 0) {
        result.queryAttempted = true;
        result.livenessKnown = true;
        result.diagnostic = QStringLiteral("GetOwnerPid returned 0");
        return result;
    }
    result.queryAttempted = true;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE,
                                 static_cast<DWORD>(pid));
    if (!process) {
        const DWORD error = GetLastError();
        if (error == ERROR_INVALID_PARAMETER) {
            result.livenessKnown = true;
            result.diagnostic = QStringLiteral("PID %1 no longer exists").arg(pid);
        } else {
            result.diagnostic = QStringLiteral("Could not inspect PID %1 (Windows error %2)").arg(pid).arg(error);
        }
        return result;
    }
    const DWORD wait = WaitForSingleObject(process, 0);
    result.livenessKnown = wait == WAIT_TIMEOUT || wait == WAIT_OBJECT_0;
    result.live = wait == WAIT_TIMEOUT;
    if (result.live) {
        DWORD pathLength = 32768;
        std::wstring path(static_cast<size_t>(pathLength), L'\0');
        if (QueryFullProcessImageNameW(process, 0, path.data(), &pathLength)) {
            path.resize(pathLength);
            result.path = QDir::toNativeSeparators(QString::fromWCharArray(path.c_str()));
            result.name = QFileInfo(result.path).fileName();
        } else {
            result.diagnostic = QStringLiteral("PID %1 is live, but its executable path could not be read (Windows error %2)")
                                    .arg(pid).arg(GetLastError());
        }
    } else if (wait == WAIT_OBJECT_0) {
        result.diagnostic = QStringLiteral("PID %1 has exited").arg(pid);
    } else {
        result.livenessKnown = false;
        result.diagnostic = QStringLiteral("Could not determine whether PID %1 is live (Windows error %2)")
                                .arg(pid).arg(GetLastError());
    }
    CloseHandle(process);
    return result;
}

VJoyOwnershipEvidence classifyVJoyOwnership(int deviceId, int rawStatus, bool ownerPidAvailable,
                                             quint64 ownerPid, quint64 hotasProcessId,
                                             const VJoyOwnerProcessEvidence &ownerProcess)
{
    VJoyOwnershipEvidence result;
    result.deviceId = deviceId;
    result.rawStatus = rawStatus;
    result.ownerPidAvailable = ownerPidAvailable;
    result.ownerPid = ownerPid;
    result.hotasProcessId = hotasProcessId;
    result.ownerProcess = ownerProcess;

    if (rawStatus == kVJoyStatusOwn
        || (ownerPidAvailable && ownerPid != 0 && ownerPid == hotasProcessId)) {
        result.state = VJoyOwnershipState::OwnedByCurrentProcess;
        result.diagnostic = rawStatus == kVJoyStatusBusy
            ? QStringLiteral("GetVJDStatus reported BUSY, but GetOwnerPid identifies this HOTAS BF6 process.")
            : QStringLiteral("vJoy is owned by this HOTAS BF6 process.");
    } else if (rawStatus == kVJoyStatusFree) {
        result.state = VJoyOwnershipState::Free;
        result.diagnostic = QStringLiteral("vJoy is free for acquisition.");
    } else if (rawStatus == kVJoyStatusBusy) {
        if (!ownerPidAvailable) {
            result.state = VJoyOwnershipState::Unknown;
            result.diagnostic = QStringLiteral("GetVJDStatus reported BUSY, but vJoy did not expose GetOwnerPid.");
        } else if (ownerPid == 0 || (ownerProcess.livenessKnown && !ownerProcess.live)) {
            result.state = VJoyOwnershipState::StaleOwnership;
            result.diagnostic = ownerPid == 0
                ? QStringLiteral("GetVJDStatus reported BUSY, but GetOwnerPid returned no live owner.")
                : QStringLiteral("GetVJDStatus reported BUSY, but owner PID %1 is not live.").arg(ownerPid);
        } else {
            result.state = VJoyOwnershipState::BusyOtherProcess;
            result.diagnostic = QStringLiteral("vJoy is owned by %1.").arg(vjoyOwnershipOwnerLabel(result));
        }
    } else if (rawStatus == kVJoyStatusMissing) {
        result.state = VJoyOwnershipState::Missing;
        result.diagnostic = QStringLiteral("vJoy reports that this device is missing or unavailable.");
    } else {
        result.state = VJoyOwnershipState::Unknown;
        result.diagnostic = QStringLiteral("GetVJDStatus returned an unknown value (%1).").arg(rawStatus);
    }
    return result;
}

VJoyOwnershipEvidence queryVJoyOwnership(int deviceId, quint64 hotasProcessId)
{
#ifdef HOTAS_CONTROLLER_READINESS_TESTING
    // ControllerReadinessTests owns a fake vJoyConfig boundary. Allow its
    // narrow Device-2 fixture to declare matching ownership evidence instead
    // of accidentally reading the developer machine's actual vJoy driver.
    // This code is not compiled into the mapper or any production test.
    if (qEnvironmentVariableIntValue("HOTAS_TEST_FREE_VJOY_DEVICE") == deviceId) {
        return classifyVJoyOwnership(deviceId, kVJoyStatusFree, false, 0,
                                     hotasProcessId == 0 ? GetCurrentProcessId() : hotasProcessId);
    }
#endif
    VJoyOwnershipApi &api = ownershipApi();
    if (hotasProcessId == 0) hotasProcessId = GetCurrentProcessId();
    if (!api.getStatus) {
        VJoyOwnershipEvidence result;
        result.deviceId = deviceId;
        result.hotasProcessId = hotasProcessId;
        result.diagnostic = api.error;
        return result;
    }
    const int rawStatus = api.getStatus(static_cast<UINT>(deviceId));
    const bool ownerPidAvailable = api.getOwnerPid != nullptr;
    const quint64 ownerPid = ownerPidAvailable ? api.getOwnerPid(static_cast<UINT>(deviceId)) : 0;
    const VJoyOwnerProcessEvidence owner = ownerPidAvailable
        ? inspectVJoyOwnerProcess(ownerPid) : VJoyOwnerProcessEvidence{};
    return classifyVJoyOwnership(deviceId, rawStatus, ownerPidAvailable, ownerPid, hotasProcessId, owner);
}

VJoyAcquireAction vjoyAcquireActionFor(const VJoyOwnershipEvidence &evidence)
{
    switch (evidence.state) {
    case VJoyOwnershipState::OwnedByCurrentProcess: return VJoyAcquireAction::AlreadyOwned;
    case VJoyOwnershipState::Free: return VJoyAcquireAction::Acquire;
    case VJoyOwnershipState::BusyOtherProcess: return VJoyAcquireAction::ExternalBusy;
    case VJoyOwnershipState::StaleOwnership: return VJoyAcquireAction::StaleOwnership;
    case VJoyOwnershipState::Missing:
    case VJoyOwnershipState::Unknown: return VJoyAcquireAction::Unavailable;
    }
    return VJoyAcquireAction::Unavailable;
}

} // namespace hotas
