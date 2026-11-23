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

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>

#include "InputDevice.h"
#include "UInput.h"

class GestureProcesser {
    private:
        struct TouchPoint {
            int32_t trackingId = -1;
            uint16_t currentPos[2];
        };

        std::recursive_mutex androidUiMutex;

        std::atomic<bool> forwarderRunning = false;
        std::atomic<bool> monitorRunning = false;
        std::atomic<bool> stopFlag = false;

        std::map<uint16_t, uint8_t> currentTapMode = {
            { BTN_TOOL_FINGER,    0 },
            { BTN_TOOL_DOUBLETAP, 0 },
            { BTN_TOOL_TRIPLETAP, 0 },
            { BTN_TOOL_QUADTAP,   0 },
            { BTN_TOOL_QUINTTAP,  0 },
            { BTN_TOUCH, 0 },
        };

        int currentTouchSlot = 0;
        std::array<TouchPoint, 10> activeTouchPoints;

        void updateTouchPoints(uint16_t code, int32_t value);
        void cancelGesture(bool restore);
        void monitorGestures();
        void eventForwarder();

    public:
        std::shared_ptr<InputDevice> touchpad;
        UInputDevice libinputUi, androidUi;

        std::map<std::string, std::optional<std::string>> customGestureCmds = {
            { "default", std::nullopt },
            { "3FingerSwipeUp", std::nullopt },
            { "3FingerSwipeDown", std::nullopt },
            { "3FingerSwipeLeft", std::nullopt },
            { "3FingerSwipeRight", std::nullopt },
            { "3FingerTap", std::nullopt },
            { "4FingerSwipeUp", std::nullopt },
            { "4FingerSwipeDown", std::nullopt },
            { "4FingerSwipeLeft", std::nullopt },
            { "4FingerSwipeRight", std::nullopt },
        };

        GestureProcesser(std::shared_ptr<InputDevice> touchpad);
        ~GestureProcesser();

        void start();
        void stop();
};
