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

using namespace swift;

#if HAVE_OS_SIGNPOST_EMIT
#include <os/base.h>
#include <os/log.h>
#include <os/signpost.h>

static os_log_t getGlobalRequestLog() {
  return os_log_create("com.apple.swift.requests", "");
}

SignpostToken swift::beginSignpostInterval(StringRef buf) {
  os_signpost_interval_begin(_textSelectionLog, token.opaque, "Request", "%{public}s", buf.data());
}

void swift::endSignpostInterval(SignpostToken token, StringRef buf) {
  os_signpost_interval_end(_textSelectionLog, token.opaque, "Request", "%{public}s", buf.data());
}

#endif // HAVE_OS_SIGNPOST_EMIT
