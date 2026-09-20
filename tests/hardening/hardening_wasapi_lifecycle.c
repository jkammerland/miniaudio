#include "../../miniaudio.c"

#include <stdio.h>

static int expect_pending(ma_device_notification_state__wasapi* pState, ma_uint32 expected, const char* pCase)
{
    ma_uint32 actual = ma_atomic_exchange_32(&pState->pendingOperations, 0);
    if (actual != expected) {
        printf("%s: pending=0x%08X expected=0x%08X\n", pCase, actual, expected);
        return 1;
    }

    return 0;
}

int main(void)
{
    ma_device device;
    ma_device_notification_state__wasapi state;
    WCHAR borrowedID[8] = L"device";
    ma_uint32 i;
    int failed = 0;

    MA_ZERO_OBJECT(&device);
    MA_ZERO_OBJECT(&state);

    state.pDevice = &device;
    if (ma_event_init(&state.wakeEvent) != MA_SUCCESS) {
        return 1;
    }
    ma_atomic_store_32(&state.acceptingNotifications, MA_TRUE);
    device.wasapi.pDeviceEnumerator = (ma_ptr)&state;
    device.wasapi.notificationClient.pDevice = &device;
    device.type = ma_device_type_playback;
    device.playback.shareMode = ma_share_mode_shared;
    device.wasapi.allowPlaybackAutoStreamRouting = MA_TRUE;

    ma_IMMNotificationClient_OnDefaultDeviceChanged(&device.wasapi.notificationClient, ma_eRender, ma_eConsole, borrowedID);
    borrowedID[0] = L'X'; /* The callback must not retain this borrowed buffer. */
    failed += expect_pending(&state, MA_WASAPI_NOTIFICATION_OP_RENDER, "render route");

    ma_IMMNotificationClient_OnDefaultDeviceChanged(&device.wasapi.notificationClient, ma_eRender, ma_eCommunications, borrowedID);
    failed += expect_pending(&state, 0, "role filter");

    ma_IMMNotificationClient_OnDefaultDeviceChanged(&device.wasapi.notificationClient, ma_eCapture, ma_eConsole, borrowedID);
    failed += expect_pending(&state, 0, "flow filter");

    device.wasapi.allowPlaybackAutoStreamRouting = MA_FALSE;
    ma_IMMNotificationClient_OnDefaultDeviceChanged(&device.wasapi.notificationClient, ma_eRender, ma_eConsole, borrowedID);
    failed += expect_pending(&state, 0, "routing disabled");

    device.wasapi.allowPlaybackAutoStreamRouting = MA_TRUE;
    device.playback.shareMode = ma_share_mode_exclusive;
    ma_IMMNotificationClient_OnDefaultDeviceChanged(&device.wasapi.notificationClient, ma_eRender, ma_eConsole, borrowedID);
    failed += expect_pending(&state, 0, "exclusive filter");

    device.type = ma_device_type_loopback;
    device.capture.shareMode = ma_share_mode_shared;
    device.wasapi.allowCaptureAutoStreamRouting = MA_TRUE;
    ma_IMMNotificationClient_OnDefaultDeviceChanged(&device.wasapi.notificationClient, ma_eRender, ma_eConsole, borrowedID);
    failed += expect_pending(&state, MA_WASAPI_NOTIFICATION_OP_CAPTURE, "loopback flow");

    for (i = 0; i < 100; i += 1) {
        ma_IMMNotificationClient_OnDeviceStateChanged(&device.wasapi.notificationClient, borrowedID, MA_MM_DEVICE_STATE_ACTIVE);
        ma_IMMNotificationClient_OnDeviceAdded(&device.wasapi.notificationClient, borrowedID);
        ma_IMMNotificationClient_OnDeviceRemoved(&device.wasapi.notificationClient, borrowedID);
    }
    failed += expect_pending(&state, MA_WASAPI_NOTIFICATION_OP_STATE, "coalesced state changes");

    ma_atomic_store_32(&state.acceptingNotifications, MA_FALSE);
    ma_IMMNotificationClient_OnDefaultDeviceChanged(&device.wasapi.notificationClient, ma_eRender, ma_eConsole, borrowedID);
    ma_IMMNotificationClient_OnDeviceStateChanged(&device.wasapi.notificationClient, borrowedID, 0);
    failed += expect_pending(&state, 0, "teardown publication gate");

    ma_event_uninit(&state.wakeEvent);
    return failed != 0;
}
