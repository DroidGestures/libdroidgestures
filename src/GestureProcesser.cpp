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
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <sys/epoll.h>

extern "C" {
#include <libinput.h>
}

#include "GestureProcesser.h"
#include "InputDevice.h"
#include "Log.h"
#include "UInput.h"

static std::thread monitorThread, forwarderThread;

void GestureProcesser::updateTouchPoints(uint16_t code, int32_t value) {
    auto& currentPoint = activeTouchPoints[currentTouchSlot];

    switch (code) {
        case ABS_MT_SLOT:
            currentTouchSlot = value;
            return;
        case ABS_MT_POSITION_X:
            currentPoint.currentPos[0] = value;
            break;
        case ABS_MT_POSITION_Y:
            currentPoint.currentPos[1] = value;
            break;
        case ABS_MT_TRACKING_ID:
            currentPoint.trackingId    = value;
            currentPoint.currentPos[0] = 0;
            currentPoint.currentPos[1] = 0;
            break;
    }
}

void GestureProcesser::cancelGesture(bool restore) {
    static std::atomic<bool> isCancelActivated = false;
    std::lock_guard<std::recursive_mutex> lock(androidUiMutex);

    if (restore) {
        if (!isCancelActivated) return;
        Log::debug("Restoring touch points");
    } else {
        Log::debug("Removing touch points");
    }

    isCancelActivated = !restore;

    for (size_t i = 0; i < activeTouchPoints.size(); i++) {
        const auto& touchPoint = activeTouchPoints[i];
        if (touchPoint.trackingId == -1) continue;

        androidUi.writeEvent(EV_ABS, ABS_MT_SLOT, i);

        if (restore) {
            // Restore tracking id and position
            androidUi.writeEvent(EV_ABS, ABS_MT_TRACKING_ID, touchPoint.trackingId);
            androidUi.writeEvent(EV_ABS, ABS_MT_POSITION_X, touchPoint.currentPos[0]);
            androidUi.writeEvent(EV_ABS, ABS_MT_POSITION_Y, touchPoint.currentPos[1]);
        } else {
            // Cancel touch points gracefully by toggling palm rejection
            androidUi.writeEvent(EV_ABS, ABS_MT_TOOL_TYPE, MT_TOOL_PALM);
            androidUi.writeEvent(EV_ABS, ABS_MT_TRACKING_ID, -1);
            androidUi.writeEvent(EV_ABS, ABS_MT_TOOL_TYPE, MT_TOOL_FINGER);
        }
    }

    // Reset/Restore tap mode
    androidUi.writeEvent(EV_KEY, BTN_TOUCH, restore ? 1 : 0);
    for (const auto& [k, v] : currentTapMode) {
        androidUi.writeEvent(EV_KEY, k, restore ? v : 0);
    }

    androidUi.writeEvent(EV_ABS, ABS_MT_SLOT, currentTouchSlot);
    androidUi.writeEvent(EV_SYN, SYN_REPORT, 0);
}

// monitorGestures(): Start gesture monitoring with libinput
void GestureProcesser::monitorGestures() {
    monitorRunning = true;

    const static libinput_interface interface = {
        .open_restricted = [](const char*, int, void* userdata) -> int {
            int fd = reinterpret_cast<InputDevice*>(userdata)->fd;

            // Obtain exclusive lock of the emulated evdev
            ioctl(fd, EVIOCGRAB, (void*) 1);

            return fd;
        },
        .close_restricted = [](int fd, void *) {
            ioctl(fd, EVIOCGRAB, (void*) 0);
        },
    };

    libinput *li = libinput_path_create_context(&interface, &libinputUi.emulatedDev);
    libinput_device *liDev = libinput_path_add_device(li, libinputUi.emulatedDev.devPath.c_str());

    assert(li != nullptr);
    assert(liDev != nullptr);

    // Enable tap-to-click so we can listen to 3-finger tap events
    libinput_device_config_tap_set_enabled(liDev, LIBINPUT_CONFIG_TAP_ENABLED);
    libinput_dispatch(li);

    int epollFd = epoll_create1(EPOLL_CLOEXEC),
        libinputFd = libinput_get_fd(li);

    epoll_event epollConfig, epollEvents[10];
    epollConfig.events = EPOLLIN;
    epollConfig.data.fd = libinputFd;
    epoll_ctl(epollFd, EPOLL_CTL_ADD, libinputFd, &epollConfig);

    union {
        libinput_event *generic;
        libinput_event_pointer* pointer;
        libinput_event_gesture* gesture;
    } event;

    int fingerCount, gestureOffset[2];
    bool gestureFired = false;

    while (!stopFlag) {
        if (epoll_wait(epollFd, epollEvents, sizeof(epollEvents) / sizeof(epoll_event), 1000) == -1) {
            break;
        }

        libinput_dispatch(li);

        while ((event.generic = libinput_get_event(li)) != NULL) {
            std::string actionName = "default";
            auto actionCmd = customGestureCmds.find(actionName);

            switch (libinput_event_get_type(event.generic)) {
                case LIBINPUT_EVENT_POINTER_BUTTON:
                    // Only handle 3-finger tap (libinput handle it as middle click) here
                    if (libinput_event_pointer_get_button(event.pointer) == BTN_MIDDLE &&
                        libinput_event_pointer_get_button_state(event.pointer) == LIBINPUT_BUTTON_STATE_PRESSED
                    ) {
                        actionName = "3FingerTap";
                        actionCmd = customGestureCmds.find(actionName);

                        if (actionCmd != customGestureCmds.end() && actionCmd->second.has_value()) {
                            // Trigger custom action
                            system(actionCmd->second.value().c_str());
                        } else {
                            std::lock_guard<std::recursive_mutex> lock(androidUiMutex);

                            // Pass gesture to Android as-is when handler isn't set
                            androidUi.writeEvent(EV_KEY, BTN_TOOL_TRIPLETAP, 1);
                        }
                    }
                    break;

                case LIBINPUT_EVENT_GESTURE_SWIPE_BEGIN:
                    gestureFired     = false;
                    fingerCount      = libinput_event_gesture_get_finger_count(event.gesture);
                    gestureOffset[0] = libinput_event_gesture_get_dx(event.gesture);
                    gestureOffset[1] = libinput_event_gesture_get_dy(event.gesture);

                    Log::debug("Start of {} finger swipe", fingerCount);
                    break;

                case LIBINPUT_EVENT_GESTURE_SWIPE_UPDATE:
                    if (gestureFired) break;

                    fingerCount       = libinput_event_gesture_get_finger_count(event.gesture);
                    gestureOffset[0] += libinput_event_gesture_get_dx(event.gesture);
                    gestureOffset[1] += libinput_event_gesture_get_dy(event.gesture);

                    if (abs(gestureOffset[0]) > abs(gestureOffset[1])) {
                        // Swipe left/right
                        actionName = std::format("{}FingerSwipe{}", fingerCount, (gestureOffset[0] < 0) ? "Left" : "Right");
                    } else {
                        // Swipe up/down
                        actionName = std::format("{}FingerSwipe{}", fingerCount, (gestureOffset[1] < 0) ? "Up" : "Down");
                    }

                    actionCmd = customGestureCmds.find(actionName);
                    if (actionCmd != customGestureCmds.end() && actionCmd->second.has_value()) {
                        // Trigger custom action
                        system(actionCmd->second.value().c_str());
                    } else {
                        // Pass gesture to Android as-is when handler isn't set
                        cancelGesture(true);
                    }

                    gestureFired = true;
                    break;

                case LIBINPUT_EVENT_GESTURE_SWIPE_END:
                    fingerCount = libinput_event_gesture_get_finger_count(event.gesture);
                    gestureFired = false;

                    Log::debug("End of {} finger swipe", fingerCount);
                    break;

                default:
                    break;
            }
            libinput_event_destroy(event.generic);
        }
    }

    libinput_unref(li);
    monitorRunning = false;
    monitorRunning.notify_all();
}

// eventForwarder(): Forward all event from device to emulated uinput devices (androidUi/libinputUi)
//
//   For libinputUi, forward all incoming events
//   For androidUi, forward all incoming events plus injected events from monitorGestures()
//
void GestureProcesser::eventForwarder() {
    forwarderRunning = true;

    input_event ev;

    int epollFd = epoll_create1(EPOLL_CLOEXEC);
    epoll_event epollConfig, epollEvents[10];
    epollConfig.events = EPOLLIN;
    epollConfig.data.fd = touchpad->fd;
    epoll_ctl(epollFd, EPOLL_CTL_ADD, touchpad->fd, &epollConfig);

    while (!stopFlag) {
        int ret = libevdev_next_event(touchpad->evdev, LIBEVDEV_READ_FLAG_NORMAL, &ev);

        switch (ret) {
            case LIBEVDEV_READ_STATUS_SUCCESS: {
                std::lock_guard<std::recursive_mutex> lock(androidUiMutex);

                if (ev.type == EV_ABS) {
                    // Track touch points by our own, as libinput doesn't expose them
                    updateTouchPoints(ev.code, ev.value);
                } else if (ev.type == EV_KEY) {
                    // Save tap mode value for use in cancelGesture()
                    auto touchModeIt = currentTapMode.find(ev.code);
                    if (touchModeIt != currentTapMode.end()) touchModeIt->second = ev.value;
                }

                // Forward the event (including EV_SYN/SYN_REPORT) to libinput
                libinputUi.writeEvent(ev.type, ev.code, ev.value);

                // Block 3/4-finger gestures when needed
                bool interceptGesture = false;

                if (ev.type == EV_KEY && ev.value == 1) {
                    switch (ev.code) {
                        case BTN_TOOL_TRIPLETAP:
                            interceptGesture = customGestureCmds.at("3FingerSwipeUp").has_value() ||
                                               customGestureCmds.at("3FingerSwipeDown").has_value() ||
                                               customGestureCmds.at("3FingerSwipeLeft").has_value() ||
                                               customGestureCmds.at("3FingerSwipeRight").has_value();
                            break;

                        case BTN_TOOL_QUADTAP:
                            interceptGesture = customGestureCmds.at("4FingerSwipeUp").has_value() ||
                                               customGestureCmds.at("4FingerSwipeDown").has_value() ||
                                               customGestureCmds.at("4FingerSwipeLeft").has_value() ||
                                               customGestureCmds.at("4FingerSwipeRight").has_value();
                            break;
                    }
                }

                if (interceptGesture) {
                    // Cancel all active touch points
                    cancelGesture(false);
                } else {
                    // Forward the event (including EV_SYN/SYN_REPORT) to inputflinger
                    androidUi.writeEvent(ev.type, ev.code, ev.value);
                }

                break;
            }

            case LIBEVDEV_READ_STATUS_SYNC:
                // TODO
                break;

            default:
                if (ret == -EAGAIN) {
                    if (epoll_wait(epollFd, epollEvents, sizeof(epollEvents) / sizeof(epoll_event), 1000) == -1) {
                        goto stop;
                    }
                } else {
                    Log::err("Device disconnected or fatal read error ({})", strerror(-ret));
                    goto stop;
                }
        }
    }

stop:
    forwarderRunning = false;
    forwarderRunning.notify_all();
    return;
}

GestureProcesser::GestureProcesser(std::shared_ptr<InputDevice> touchpad) : touchpad(touchpad) {
    // Grab the original device to prevent Android from reading it
    touchpad->grabDevice(true);

    libinputUi.cloneFromDevice(touchpad);
    androidUi.cloneFromDevice(touchpad);
}

GestureProcesser::~GestureProcesser() {
    touchpad->grabDevice(false);
}

void GestureProcesser::start() {
    stopFlag = false;
    forwarderThread = std::thread(&GestureProcesser::eventForwarder, this);
    monitorThread = std::thread(&GestureProcesser::monitorGestures, this);
}

void GestureProcesser::stop() {
    stopFlag = true;
    forwarderRunning.wait(true);
    monitorRunning.wait(true);
}
