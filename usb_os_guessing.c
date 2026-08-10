/*
 * Host detection derived from TinyUSB's uac2_speaker_fb example.
 * Original code copyright (c) 2024 HiFiPhile, MIT licensed.
 */

#include "usb_os_guessing.h"

static tusb_desc_type_t descriptor_requests[2];
static uint8_t descriptor_request_count;

static void remember_request(tusb_desc_type_t type)
{
    if (descriptor_request_count >= 2u) {
        return;
    }
    if (descriptor_request_count == 0u || descriptor_requests[0] != type) {
        descriptor_requests[descriptor_request_count++] = type;
    }
}

void usb_os_guessing_device_descriptor(void)
{
    descriptor_request_count = 0u;
}

void usb_os_guessing_configuration_descriptor(void)
{
    remember_request(TUSB_DESC_CONFIGURATION);
}

void usb_os_guessing_bos_descriptor(void)
{
    remember_request(TUSB_DESC_BOS);
}

void usb_os_guessing_string_descriptor(void)
{
    remember_request(TUSB_DESC_STRING);
}

usb_host_os_t usb_os_guessing_get(void)
{
    if (descriptor_request_count < 2u) {
        return USB_HOST_OS_UNKNOWN;
    }

    if (descriptor_requests[0] == TUSB_DESC_BOS &&
        descriptor_requests[1] == TUSB_DESC_CONFIGURATION) {
        return USB_HOST_OS_LINUX;
    }
    if (descriptor_requests[0] == TUSB_DESC_CONFIGURATION &&
        descriptor_requests[1] == TUSB_DESC_BOS) {
        return USB_HOST_OS_WINDOWS;
    }
    if (descriptor_requests[0] == TUSB_DESC_STRING &&
        (descriptor_requests[1] == TUSB_DESC_BOS ||
         descriptor_requests[1] == TUSB_DESC_CONFIGURATION)) {
        return USB_HOST_OS_MACOS;
    }

    return USB_HOST_OS_UNKNOWN;
}
