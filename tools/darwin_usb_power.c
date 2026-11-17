/* Inspect host USB power state without descriptor requests or implicit recovery. */
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/usb/USB.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void match_number(CFMutableDictionaryRef matching, CFStringRef key, int32_t value) {
    CFNumberRef number = CFNumberCreate(NULL, kCFNumberSInt32Type, &value);
    CFDictionarySetValue(matching, key, number);
    CFRelease(number);
}

static void report_state(IOUSBDeviceInterface650 **device, const char *stage) {
    UInt32 information = 0;
    IOReturn result = (*device)->GetUSBDeviceInformation(device, &information);
    printf("{\"stage\":\"%s\",\"result\":\"0x%08x\",\"information\":%u,"
           "\"connected\":%s,\"enabled\":%s,\"suspended\":%s,\"resetting\":%s}\n",
           stage,
           result,
           information,
           information & kUSBInformationDeviceIsConnectedMask ? "true" : "false",
           information & kUSBInformationDeviceIsEnabledMask ? "true" : "false",
           information & kUSBInformationDeviceIsSuspendedMask ? "true" : "false",
           information & kUSBInformationDeviceIsInResetMask ? "true" : "false");
    fflush(stdout);
}

int main(int argc, char **argv) {
    const char *action = argc >= 2 ? argv[1] : "status";
    if (strcmp(action, "status") && strcmp(action, "resume") && strcmp(action, "suspend") &&
        strcmp(action, "cycle")) {
        fprintf(stderr, "Usage: %s [status|resume|suspend|cycle]\n", argv[0]);
        return 2;
    }
    CFMutableDictionaryRef matching = IOServiceMatching(kIOUSBDeviceClassName);
    match_number(matching, CFSTR(kUSBVendorID), 0xdeaf);
    match_number(matching, CFSTR(kUSBProductID), 0xbabe);
    io_iterator_t iterator = IO_OBJECT_NULL;
    IOReturn result = IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator);
    if (result != kIOReturnSuccess) {
        fprintf(stderr, "IOServiceGetMatchingServices: 0x%08x\n", result);
        return 1;
    }
    io_service_t service = IOIteratorNext(iterator);
    io_service_t extra = IOIteratorNext(iterator);
    IOObjectRelease(iterator);
    if (!service || extra) {
        fprintf(stderr, "Connect exactly one DEAF:BABE scope\n");
        if (service) {
            IOObjectRelease(service);
        }
        if (extra) {
            IOObjectRelease(extra);
        }
        return 1;
    }
    IOCFPlugInInterface **plugin = NULL;
    SInt32 score;
    result = IOCreatePlugInInterfaceForService(
        service, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &plugin, &score);
    IOObjectRelease(service);
    if (result != kIOReturnSuccess) {
        fprintf(stderr, "IOCreatePlugInInterfaceForService: 0x%08x\n", result);
        return 1;
    }
    IOUSBDeviceInterface650 **device = NULL;
    HRESULT query_result = (*plugin)->QueryInterface(
        plugin, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID650), (LPVOID *)&device);
    IODestroyPlugInInterface(plugin);
    if (query_result || !device) {
        fprintf(stderr, "QueryInterface: 0x%08x\n", (unsigned)query_result);
        return 1;
    }
    report_state(device, "before");
    int exit_status = 0;
    if (strcmp(action, "status")) {
        result = (*device)->USBDeviceOpen(device);
        printf("{\"stage\":\"open\",\"result\":\"0x%08x\"}\n", result);
        if (result == kIOReturnSuccess) {
            Boolean suspend = strcmp(action, "resume") != 0;
            result = (*device)->USBDeviceSuspend(device, suspend);
            printf("{\"stage\":\"%s\",\"result\":\"0x%08x\"}\n",
                   suspend ? "suspend" : "resume",
                   result);
            exit_status = result != kIOReturnSuccess;
            report_state(device, "after");
            if (!strcmp(action, "cycle") && result == kIOReturnSuccess) {
                usleep(250000);
                result = (*device)->USBDeviceSuspend(device, false);
                printf("{\"stage\":\"resume\",\"result\":\"0x%08x\"}\n", result);
                exit_status = result != kIOReturnSuccess;
                report_state(device, "resumed");
            }
            (*device)->USBDeviceClose(device);
        } else {
            exit_status = 1;
        }
    }
    (*device)->Release(device);
    return exit_status;
}
