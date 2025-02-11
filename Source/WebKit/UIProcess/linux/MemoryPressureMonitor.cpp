/*
 * Copyright (C) 2016 Igalia S.L.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "MemoryPressureMonitor.h"

#if OS(LINUX)

#include "Attachment.h"
#include <errno.h>
#include <fcntl.h>
#include <mutex>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <wtf/CurrentTime.h>
#include <wtf/Threading.h>
#include <wtf/UniStdExtras.h>

namespace WebKit {

static const size_t notSet = static_cast<size_t>(-1);

static const double s_minPollingIntervalInSeconds = 1;
static const double s_maxPollingIntervalInSeconds = 5;
static const double s_minUsedMemoryPercentageForPolling = 50;
static const double s_maxUsedMemoryPercentageForPolling = 90;
static const int s_memoryPresurePercentageThreshold = 95;

static size_t lowWatermarkPages()
{
    FILE* file = fopen("/proc/zoneinfo", "r");
    if (!file)
        return notSet;

    size_t low = 0;
    bool inZone = false;
    bool foundLow = false;
    char buffer[128];
    while (char* line = fgets(buffer, 128, file)) {
        if (!strncmp(line, "Node", 4)) {
            inZone = true;
            foundLow = false;
            continue;
        }

        char* token = strtok(line, " ");
        if (!token)
            continue;

        if (!strcmp(token, "low")) {
            if (!inZone || foundLow) {
                low = notSet;
                break;
            }
            token = strtok(nullptr, " ");
            if (!token) {
                low = notSet;
                break;
            }
            low += atoll(token);
            foundLow = true;
        }
    }
    fclose(file);

    return low;
}

static inline size_t systemPageSize()
{
    static size_t pageSize = 0;
    if (!pageSize)
        pageSize = sysconf(_SC_PAGE_SIZE);
    return pageSize;
}

// If MemAvailable was not present in /proc/meminfo, because it's an old kernel version,
// we can do the same calculation with the information we have from meminfo and the low watermaks.
// See https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=34e431b0ae398fc54ea69ff85ec700722c9da773
static size_t calculateMemoryAvailable(size_t memoryFree, size_t activeFile, size_t inactiveFile, size_t slabReclaimable)
{
    if (memoryFree == notSet || activeFile == notSet || inactiveFile == notSet || slabReclaimable == notSet)
        return notSet;

    size_t lowWatermark = lowWatermarkPages();
    if (lowWatermark == notSet)
        return notSet;

    lowWatermark *= systemPageSize() / KB;

    // Estimate the amount of memory available for userspace allocations, without causing swapping.
    // Free memory cannot be taken below the low watermark, before the system starts swapping.
    lowWatermark *= systemPageSize() / KB;
    size_t memoryAvailable = memoryFree - lowWatermark;

    // Not all the page cache can be freed, otherwise the system will start swapping. Assume at least
    // half of the page cache, or the low watermark worth of cache, needs to stay.
    size_t pageCache = activeFile + inactiveFile;
    pageCache -= std::min(pageCache / 2, lowWatermark);
    memoryAvailable += pageCache;

    // Part of the reclaimable slab consists of items that are in use, and cannot be freed.
    // Cap this estimate at the low watermark.
    memoryAvailable += slabReclaimable - std::min(slabReclaimable / 2, lowWatermark);
    return memoryAvailable;
}

static int systemMemoryUsedAsPercentage()
{
    FILE* file = fopen("/proc/meminfo", "r");
    if (!file)
        return -1;

    size_t memoryAvailable, memoryTotal, memoryFree, activeFile, inactiveFile, slabReclaimable;
    memoryAvailable = memoryTotal = memoryFree = activeFile = inactiveFile = slabReclaimable = notSet;
    char buffer[128];
    while (char* line = fgets(buffer, 128, file)) {
        char* token = strtok(line, " ");
        if (!token)
            break;

        if (!strcmp(token, "MemAvailable:")) {
            if ((token = strtok(nullptr, " "))) {
                memoryAvailable = atoll(token);
                if (memoryTotal != notSet)
                    break;
            }
        } else if (!strcmp(token, "MemTotal:")) {
            if ((token = strtok(nullptr, " ")))
                memoryTotal = atoll(token);
            else
                break;
        } else if (!strcmp(token, "MemFree:")) {
            if ((token = strtok(nullptr, " ")))
                memoryFree = atoll(token);
            else
                break;
        } else if (!strcmp(token, "Active(file):")) {
            if ((token = strtok(nullptr, " ")))
                activeFile = atoll(token);
            else
                break;
        } else if (!strcmp(token, "Inactive(file):")) {
            if ((token = strtok(nullptr, " ")))
                inactiveFile = atoll(token);
            else
                break;
        } else if (!strcmp(token, "SReclaimable:")) {
            if ((token = strtok(nullptr, " ")))
                slabReclaimable = atoll(token);
            else
                break;
        }

        if (memoryTotal != notSet && memoryFree != notSet && activeFile != notSet && inactiveFile != notSet && slabReclaimable != notSet)
            break;
    }
    fclose(file);

    if (!memoryTotal || memoryTotal == notSet)
        return -1;

    if (memoryAvailable == notSet) {
        memoryAvailable = calculateMemoryAvailable(memoryFree, activeFile, inactiveFile, slabReclaimable);
        if (memoryAvailable == notSet)
            return -1;
    }

    if (memoryAvailable > memoryTotal)
        return -1;

    return ((memoryTotal - memoryAvailable) * 100) / memoryTotal;
}

static inline double pollIntervalForUsedMemoryPercentage(int usedPercentage)
{
    // Use a different poll interval depending on the currently memory used,
    // to avoid polling too often when the system is under low memory usage.
    if (usedPercentage < s_minUsedMemoryPercentageForPolling)
        return s_maxPollingIntervalInSeconds;

    if (usedPercentage >= s_maxUsedMemoryPercentageForPolling)
        return s_minPollingIntervalInSeconds;

    return s_minPollingIntervalInSeconds + (s_maxPollingIntervalInSeconds - s_minPollingIntervalInSeconds) *
        ((usedPercentage - s_minUsedMemoryPercentageForPolling) / (s_maxUsedMemoryPercentageForPolling - s_minUsedMemoryPercentageForPolling));
}

static bool isSystemdMemoryPressureMonitorAvailable()
{
    int fd = open("/sys/fs/cgroup/memory/memory.pressure_level", O_CLOEXEC | O_RDONLY);
    if (fd == -1)
        return false;
    close(fd);

    fd = open("/sys/fs/cgroup/memory/cgroup.event_control", O_CLOEXEC | O_WRONLY);
    if (fd == -1)
        return false;
    close(fd);

    return true;
}

bool MemoryPressureMonitor::isEnabled()
{
    static std::once_flag onceFlag;
    static bool enabled;
    std::call_once(onceFlag, [] { enabled = !isSystemdMemoryPressureMonitorAvailable(); });
    return enabled;
}

MemoryPressureMonitor& MemoryPressureMonitor::singleton()
{
    ASSERT(isEnabled());
    static NeverDestroyed<MemoryPressureMonitor> memoryMonitor;
    return memoryMonitor;
}

MemoryPressureMonitor::MemoryPressureMonitor()
    : m_eventFD(eventfd(0, EFD_CLOEXEC))
{
    if (m_eventFD == -1)
        return;

    Thread::create("MemoryPressureMonitor", [this] {
        double pollInterval = s_maxPollingIntervalInSeconds;
        while (true) {
            sleep(pollInterval);

            int usedPercentage = systemMemoryUsedAsPercentage();
            if (usedPercentage == -1) {
                WTFLogAlways("Failed to get the memory usage");
                break;
            }

            if (usedPercentage >= s_memoryPresurePercentageThreshold) {
                uint64_t fdEvent = 1;
                ssize_t bytesWritten = write(m_eventFD, &fdEvent, sizeof(uint64_t));
                if (bytesWritten != sizeof(uint64_t)) {
                    WTFLogAlways("Error writing to MemoryPressureMonitor eventFD: %s", strerror(errno));
                    break;
                }
            }
            pollInterval = pollIntervalForUsedMemoryPercentage(usedPercentage);
        }
        close(m_eventFD);
    })->detach();
}

IPC::Attachment MemoryPressureMonitor::createHandle() const
{
    int duplicatedHandle = dupCloseOnExec(m_eventFD);
    if (duplicatedHandle == -1)
        return { };
    return IPC::Attachment(duplicatedHandle);
}

} // namespace WebKit

#endif // OS(LINUX)
