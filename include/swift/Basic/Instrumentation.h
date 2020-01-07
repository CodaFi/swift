//===------------ Instrumentation.h - Instruments Support -------*- C++ -*-===//
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

#ifndef SWIFT_BASIC_INSTRUMENTATION_H
#define SWIFT_BASIC_INSTRUMENTATION_H

#include "swift/Config.h"
#include "swift/Basic/TypeID.h"
#include "llvm/Support/raw_ostream.h"

namespace swift {

void *getGlobalRequestLog();

struct OSLog {
private:
  llvm::StringRef Description;
  uint64_t SignpostID;

public:
  OSLog() : Description(), SignpostID(0) {}

  void setUp(llvm::StringRef desc);
  void tearDown();

private:
  static void *requestLog();
};

template<typename Request>
class RequestInstrumenterRAII {
public:
  RequestInstrumenterRAII(const Request &req);
  ~RequestInstrumenterRAII();

private:
  OSLog data;
};

template<typename Request>
inline RequestInstrumenterRAII<Request>::RequestInstrumenterRAII(const Request &req) {
#if HAVE_OS_SIGNPOST_EMIT
//  std::string Description;
//  {
//    llvm::raw_string_ostream out(Description);
//    simple_display(out, req);
//  }
  data.setUp(TypeID<Request>::getName());
#endif
}

template<typename Request>
inline RequestInstrumenterRAII<Request>::~RequestInstrumenterRAII() {
#if HAVE_OS_SIGNPOST_EMIT
  data.tearDown();
#endif
}

} // end namespace swift

#endif // SWIFT_BASIC_INSTRUMENTATION_H
