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
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"

using namespace swift;

//#if HAVE_OS_SIGNPOST_EMIT
#include <os/base.h>
#include <os/log.h>
#include <os/signpost.h>
#include <thread>
#include <mutex>

std::once_flag globalRequestOnceToken;
void *globalRequestLog = nullptr;

static void *getGlobalRequestLog() {
  if (__builtin_available(macOS 10.14, *)) {
    std::call_once(globalRequestOnceToken, [](){
      globalRequestLog = os_log_create("com.apple.swift.requests", "");
    });
    return globalRequestLog;
  } else {
    return nullptr;
  }
}

RequestInstrumenterRAII::RequestInstrumenterRAII(std::string desc)
  : Description(std::move(desc)), OpaqueLog(getGlobalRequestLog())
{
  if (__builtin_available(macOS 10.14, *)) {
    SignpostID = os_signpost_id_generate((os_log_t)OpaqueLog);
    os_signpost_interval_begin((os_log_t)OpaqueLog, SignpostID,
                               "Request", "%{public}s", Description.data());
  }
}

RequestInstrumenterRAII::~RequestInstrumenterRAII() {
  if (__builtin_available(macOS 10.14, *)) {
    os_signpost_interval_end((os_log_t)OpaqueLog, SignpostID,
                             "Request", "%{public}s", Description.data());
  }
}

//#endif // HAVE_OS_SIGNPOST_EMIT
