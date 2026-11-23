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

#include <cstdint>
#include <memory>

extern "C" {
#include <libevdev/libevdev-uinput.h>
}

#include "InputDevice.h"

class UInputDevice {
    private:
        int uinputFd;
        libevdev_uinput *uinput;

    public:
        InputDevice emulatedDev;

        UInputDevice();
        ~UInputDevice();

        int cloneFromDevice(std::shared_ptr<InputDevice> deviceToClone);
        int writeEvent(uint32_t type, uint32_t code, int32_t value);
};
