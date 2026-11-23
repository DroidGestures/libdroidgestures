/*
 * Copyright (C) 2026 SupeChicken666 <me@supechicken666.dev>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <cassert>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

extern "C" {
#include <libevdev/libevdev-uinput.h>
}

#include "InputDevice.h"
#include "Log.h"
#include "UInput.h"

UInputDevice::UInputDevice() {
    if ((uinputFd = open("/dev/uinput", O_WRONLY | O_CLOEXEC | O_NONBLOCK)) == -1) {
        Log::err("Failed to open uinput device: {}", strerror(errno));
        exit(errno);
    }
}

UInputDevice::~UInputDevice() {
    libevdev_uinput_destroy(uinput);
    close(uinputFd);
}

// cloneFromDevice(): Create a emulated device with all properties of deviceToClone
int UInputDevice::cloneFromDevice(std::shared_ptr<InputDevice> deviceToClone) {
    libevdev_set_name(deviceToClone->evdev, "DroidGestures");

    int ret = libevdev_uinput_create_from_device(deviceToClone->evdev, uinputFd, &uinput);
    if (ret != 0) Log::err("Failed to clone from device {}: {}", deviceToClone->devPath, strerror(errno));

    const char* devPath = libevdev_uinput_get_devnode(uinput);
    assert(devPath);

    // Wait for udev/init to setup node permission
    while (access(devPath, R_OK) != 0) usleep(200000);
    emulatedDev.openDevice(devPath);

    Log::debug("Device {} created from {}", devPath, deviceToClone->devPath);
    return ret;
}

int UInputDevice::writeEvent(uint32_t type, uint32_t code, int32_t value) {
    return libevdev_uinput_write_event(uinput, type, code, value);
}
