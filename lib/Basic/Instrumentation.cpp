//===--- Instrumentation.cpp - Instruments Support ------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#include "swift/Basic/Instrumentation.h"

#if HAVE_OS_SIGNPOST_EMIT
#include <os/base.h>
#include <os/log.h>
#include <os/signpost.h>
#include <mutex>
#endif

using namespace swift;

void *OSLog::requestLog() {
#if HAVE_OS_SIGNPOST_EMIT
  static std::once_flag GlobalRequestOnceToken;
  static void *GlobalRequestLog = nullptr;
  if (__builtin_available(macOS 10.14, *)) {
    std::call_once(GlobalRequestOnceToken, [](){
      GlobalRequestLog = os_log_create("com.apple.swift.requests", "");
    });
  }
  return GlobalRequestLog;
#endif

  return nullptr;
}

void OSLog::setUp(std::string &&desc) {
  if (__builtin_available(macOS 10.14, *)) {
    Description = std::move(desc);
    SignpostID = os_signpost_id_generate((os_log_t)OSLog::requestLog());
    os_signpost_interval_begin((os_log_t)OSLog::requestLog(), SignpostID,
                               "Request", "%{public}s", Description.data());
  }
}

void OSLog::tearDown() {
  if (__builtin_available(macOS 10.14, *)) {
    os_signpost_interval_end((os_log_t)OSLog::requestLog(), SignpostID,
                             "Request", "%{public}s", Description.data());
  }
}
