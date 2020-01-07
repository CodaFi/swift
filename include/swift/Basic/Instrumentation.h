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

#include <string>
#include <cassert>
#include <cstdint>

namespace swift {
  class RequestInstrumenterRAII {
  public:
    RequestInstrumenterRAII(std::string desc);
    ~RequestInstrumenterRAII();

  private:
    std::string Description;
    uint64_t SignpostID;
    void *OpaqueLog;
  };
} // end namespace swift

#endif // SWIFT_BASIC_INSTRUMENTATION_H
