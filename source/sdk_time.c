// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string.h>
#include <switch.h>

#include "sdk.h"

// nn::time's exported functions (sdk.h) and their own code: the requests to time:u that libnx
// sends, on the session timeInitialize opens. The functions the SDK declares void ignore the
// result, as their callers cannot see one.

/// How many times the own code's Initialize succeeded, for IsInitialized.
static uint32_t own_initialized;

SDK_FUNCTION uint32_t NnTimeInitialize(void) {
    SdkOwnCall(SDK_TIME_INITIALIZE);
    const Result rc = timeInitialize();
    if (R_SUCCEEDED(rc)) {
        __atomic_add_fetch(&own_initialized, 1u, __ATOMIC_RELAXED);
    }
    return rc;
}

SDK_FUNCTION bool NnTimeIsInitialized(void) {
    SdkOwnCall(SDK_TIME_IS_INITIALIZED);
    return __atomic_load_n(&own_initialized, __ATOMIC_RELAXED) != 0u;
}

SDK_FUNCTION uint32_t NnTimeUserGetCurrentTime(int64_t *out) {
    SdkOwnCall(SDK_TIME_USER_TIME);
    u64 now = 0;
    const Result rc = timeGetCurrentTime(TimeType_UserSystemClock, &now);
    *out = (int64_t)now;
    return rc;
}

SDK_FUNCTION uint32_t NnTimeUserGetContext(TimeSystemClockContext *out) {
    SdkOwnCall(SDK_TIME_USER_CONTEXT);
    return serviceDispatchOut(timeGetServiceSession_SystemClock(TimeType_UserSystemClock), 2, *out);
}

SDK_FUNCTION uint32_t NnTimeNetworkGetCurrentTime(int64_t *out) {
    SdkOwnCall(SDK_TIME_NETWORK_TIME);
    u64 now = 0;
    const Result rc = timeGetCurrentTime(TimeType_NetworkSystemClock, &now);
    *out = (int64_t)now;
    return rc;
}

SDK_FUNCTION uint32_t NnTimeSteadyGetCurrentTimePoint(TimeSteadyClockTimePoint *out) {
    SdkOwnCall(SDK_TIME_STEADY_POINT);
    return timeGetStandardSteadyClockTimePoint(out);
}

SDK_FUNCTION uint32_t NnTimeUserGetCurrentTimespec(struct timespec *out) {
    SdkOwnCall(SDK_TIME_USER_TIMESPEC);
    u64 now = 0;
    const Result rc = timeGetCurrentTime(TimeType_UserSystemClock, &now);
    out->tv_sec = (time_t)now;
    out->tv_nsec = 0;
    return rc;
}

SDK_FUNCTION void NnTimeGetDeviceLocationName(TimeLocationName *out) {
    SdkOwnCall(SDK_TIME_DEVICE_LOCATION);
    (void)timeGetDeviceLocationName(out);
}

SDK_FUNCTION uint32_t NnTimeLoadTimeZoneRule(TimeZoneRule *rule, const TimeLocationName *name) {
    SdkOwnCall(SDK_TIME_LOAD_RULE);
    return timeLoadTimeZoneRule(name, rule);
}

SDK_FUNCTION uint32_t NnTimeToCalendarTimeMine(TimeCalendarTime *date,
                                               TimeCalendarAdditionalInfo *info,
                                               const int64_t *time) {
    SdkOwnCall(SDK_TIME_TO_CALENDAR_MINE);
    TimeCalendarAdditionalInfo ignored;
    return timeToCalendarTimeWithMyRule((u64)*time, date, info != NULL ? info : &ignored);
}

SDK_FUNCTION uint32_t NnTimeToCalendarTime(TimeCalendarTime *date,
                                           TimeCalendarAdditionalInfo *info, const int64_t *time,
                                           const TimeZoneRule *rule) {
    SdkOwnCall(SDK_TIME_TO_CALENDAR);
    TimeCalendarAdditionalInfo ignored;
    return timeToCalendarTime(rule, (u64)*time, date, info != NULL ? info : &ignored);
}

SDK_FUNCTION uint32_t NnTimeToPosixTimeMine(int32_t *count, int64_t *out, int32_t max,
                                            const TimeCalendarTime *date) {
    SdkOwnCall(SDK_TIME_TO_POSIX_MINE);
    s32 got = 0;
    const Result rc = timeToPosixTimeWithMyRule(date, (u64 *)out, max, &got);
    *count = got;
    return rc;
}

SDK_FUNCTION uint32_t NnTimeToPosixTime(int32_t *count, int64_t *out, int32_t max,
                                        const TimeCalendarTime *date, const TimeZoneRule *rule) {
    SdkOwnCall(SDK_TIME_TO_POSIX);
    s32 got = 0;
    const Result rc = timeToPosixTime(rule, date, (u64 *)out, max, &got);
    *count = got;
    return rc;
}

SDK_FUNCTION int32_t NnTimeGetTotalLocationNameCount(void) {
    SdkOwnCall(SDK_TIME_LOCATION_COUNT);
    s32 count = 0;
    return R_SUCCEEDED(timeGetTotalLocationNameCount(&count)) ? count : 0;
}

SDK_FUNCTION void NnTimeLoadLocationNameList(int32_t *count, TimeLocationName *out, int32_t max,
                                             int32_t index) {
    SdkOwnCall(SDK_TIME_LOCATION_LIST);
    s32 got = 0;
    if (R_FAILED(timeLoadLocationNameList(index, out, max, &got))) {
        got = 0;
    }
    *count = got;
}

SDK_FUNCTION void NnTimeGetDeviceLocationNameAndUpdatedTime(TimeLocationName *name,
                                                            TimeSteadyClockTimePoint *point) {
    SdkOwnCall(SDK_TIME_LOCATION_UPDATED);
    // GetDeviceLocationNameAndUpdatedTime (6): the name, then the time point at the next
    // eight-byte boundary.
    struct {
        TimeLocationName name;
        u8 pad[4];
        TimeSteadyClockTimePoint point;
    } updated;
    memset(&updated, 0, sizeof(updated));
    (void)serviceDispatchOut(timeGetServiceSession_TimeZoneService(), 6, updated);
    *name = updated.name;
    *point = updated.point;
}

SDK_FUNCTION void NnTimeGetTimeZoneRuleVersion(char out[0x10]) {
    SdkOwnCall(SDK_TIME_RULE_VERSION);
    struct {
        char v[0x10];
    } version;
    memset(&version, 0, sizeof(version));
    (void)serviceDispatchOut(timeGetServiceSession_TimeZoneService(), 5, version);
    memcpy(out, version.v, sizeof(version.v));
}

SDK_FUNCTION void NnTimeSnapshotCreate(NnTimeClockSnapshot *out) {
    SdkOwnCall(SDK_TIME_SNAPSHOT);
    // GetClockSnapshot (400) of the standard clocks (type 0).
    const u8 type = 0;
    (void)serviceDispatchIn(
        timeGetServiceSession(), 400, type,
        .buffer_attrs = { SfBufferAttr_FixedSize | SfBufferAttr_HipcPointer | SfBufferAttr_Out },
        .buffers = { { out->bytes, sizeof(out->bytes) } }, );
}

SDK_FUNCTION bool NnTimeCalculateSpanBetween(int64_t *span, const NnTimeClockSnapshot *a,
                                             const NnTimeClockSnapshot *b) {
    SdkOwnCall(SDK_TIME_SPAN_BETWEEN);
    // CalculateSpanBetween (501), in nanoseconds.
    s64 ns = 0;
    const Result rc = serviceDispatchOut(
        timeGetServiceSession(), 501, ns,
        .buffer_attrs = { SfBufferAttr_FixedSize | SfBufferAttr_HipcPointer | SfBufferAttr_In,
                          SfBufferAttr_FixedSize | SfBufferAttr_HipcPointer | SfBufferAttr_In },
        .buffers = { { a->bytes, sizeof(a->bytes) }, { b->bytes, sizeof(b->bytes) } }, );
    if (R_FAILED(rc)) {
        return false;
    }
    *span = ns;
    return true;
}

// An application's time:u hands out no local clock: the user clock's SetCurrentTime (1) and
// SetSystemClockContext (3) are the writes it can ask, and the network clock's SetCurrentTime.

SDK_FUNCTION uint32_t NnTimeSetLocalCurrentTime(const int64_t *time) {
    SdkOwnCall(SDK_TIME_SET_LOCAL_TIME);
    return serviceDispatchIn(timeGetServiceSession_SystemClock(TimeType_UserSystemClock), 1, *time);
}

SDK_FUNCTION uint32_t NnTimeSetLocalContext(const TimeSystemClockContext *context) {
    SdkOwnCall(SDK_TIME_SET_LOCAL_CONTEXT);
    return serviceDispatchIn(timeGetServiceSession_SystemClock(TimeType_UserSystemClock), 3,
                             *context);
}

SDK_FUNCTION uint32_t NnTimeSetNetworkCurrentTime(const int64_t *time) {
    SdkOwnCall(SDK_TIME_SET_NETWORK_TIME);
    return serviceDispatchIn(timeGetServiceSession_SystemClock(TimeType_NetworkSystemClock), 1,
                             *time);
}
