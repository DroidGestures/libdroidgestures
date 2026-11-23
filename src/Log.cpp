/*
 * Copyright (C) 2026 The WayDroid-ATV Project
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <iostream>
#include <string>
#include <format>

#ifdef __ANDROID__
#include <android/log.h>
#define LOG_TAG "DroidGesturesDaemon"
#endif

#include "Log.h"

namespace Log {
#ifdef __ANDROID__
    static android_LogPriority levelToDroidLevel(const enum LogLevel &level) {
        switch (level) {
            case LogLevel::LOG_ERR:
                return ANDROID_LOG_ERROR;
            case LogLevel::LOG_WARN:
                return ANDROID_LOG_WARN;
            case LogLevel::LOG_INFO:
                return ANDROID_LOG_INFO;
            case LogLevel::LOG_DEBUG:
                return ANDROID_LOG_DEBUG;
        }

        return ANDROID_LOG_UNKNOWN;
    }
#endif

    static std::string levelToString(const enum LogLevel &level) {
        switch (level) {
            case LogLevel::LOG_ERR:
                return "\e[0;31mERR\e[0m";
            case LogLevel::LOG_WARN:
                return "\e[0;33mWARN\e[0m";
            case LogLevel::LOG_INFO:
                return "\e[0;32mINFO\e[0m";
            case LogLevel::LOG_DEBUG:
                return "\e[0;90mDEBUG\e[0m";
        }

        return "UNKNOWN";
    }

    void printMessage(const enum LogLevel &level, const std::string &message, const std::format_args &args) {
        std::string formattedStr = std::vformat("[" + levelToString(level) + "] " + message, args);

#ifdef __ANDROID__
        __android_log_write(levelToDroidLevel(level), LOG_TAG, formattedStr.c_str());
#endif

        if (level == LOG_INFO || level == LOG_DEBUG) {
            std::cout << formattedStr << std::endl;
        } else {
            std::cerr << formattedStr << std::endl;
        }
    }
}
