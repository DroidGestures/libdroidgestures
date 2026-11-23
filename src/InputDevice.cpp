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

#include <cerrno>
#include <cstring>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>

extern "C" {
#include <libevdev/libevdev.h>
}

#include "InputDevice.h"
#include "Log.h"

InputDevice::~InputDevice() {
    closeDevice();
}

int InputDevice::openDevice(const std::string& path) {
    devPath = path;

    if ((fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK)) == -1) {
        Log::err("Failed to open {}: {}", path, strerror(errno));
        return -errno;
    }

    int ret = libevdev_new_from_fd(fd, &evdev);
    if (ret == -1) Log::err("Failed to init libevdev on device {}: {}", path, strerror(-ret));

    return ret;
}

void InputDevice::closeDevice() {
    if (evdev) {
        libevdev_free(evdev);
        evdev = nullptr;
    }

    if (fd != -1) {
        close(fd);
        fd = -1;
    }
}

int InputDevice::grabDevice(bool grab) {
    int ret = libevdev_grab(evdev, grab ? LIBEVDEV_GRAB : LIBEVDEV_UNGRAB);
    if (ret != 0) Log::warn("Grab failed for device {}: {}", devPath, strerror(-ret));
    return ret;
}

bool InputDevice::isTouchpad() {
    return libevdev_has_event_code(evdev, EV_KEY, BTN_TOOL_FINGER) &&
           libevdev_has_property(evdev, INPUT_PROP_POINTER);
}
