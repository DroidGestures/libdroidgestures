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

#pragma once

#include <string>

extern "C" {
#include <libevdev/libevdev.h>
}

class InputDevice {
    public:
        int fd = -1;
        std::string devPath;
        libevdev *evdev = nullptr;

        ~InputDevice();

        int openDevice(const std::string& path);
        void closeDevice();
        int grabDevice(bool grab);
        bool isTouchpad();
};
