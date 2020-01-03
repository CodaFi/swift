//===--- StableHasher.h - Stable Hashing ------------------------*- C++ -*-===//
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
// FIXME
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_BASIC_STABLEPATH_H
#define SWIFT_BASIC_STABLEPATH_H

#include "llvm/ADT/Hashing.h"
#include "swift/Basic/StableHasher.h"

namespace swift {

class SipHasher final {
private:
  struct State {
    uint64_t v0 = 0x736f6d6570736575;
    uint64_t v1 = 0x646f72616e646f6d;
    uint64_t v2 = 0x6c7967656e657261;
    uint64_t v3 = 0x7465646279746573;
  } state;

  // msb                                                             lsb
  // +---------+-------+-------+-------+-------+-------+-------+-------+
  // |byteCount|                 tail (<= 56 bits)                     |
  // +---------+-------+-------+-------+-------+-------+-------+-------+
  uint64_t tailAndByteCount = 0;

public:
  static SipHasher defaultHasher() {
    return SipHasher{0, 0};
  }

  explicit SipHasher(uint64_t leftSeed, uint64_t rightSeed) {
    state.v3 ^= rightSeed;
    state.v2 ^= leftSeed;
    state.v1 ^= rightSeed;
    state.v0 ^= leftSeed;
  }

private:
  uint64_t getNumBytes() const {
    return tailAndByteCount >> 56;
  }

  uint64_t getTailSize() const {
    return tailAndByteCount & ~(uint64_t(0xFF) << 56);
  }

  void compress(uint64_t value);

public:
  /// Consume this stable hasher and compute the final 64-bit stable hash value.
  uint64_t finalize() &&;

  template<uint64_t N>
  void append(uint8_t bits[N]);

  template<typename T>
  void append(typename std::enable_if<std::is_integral<T>::value>::type bits) {
    uint8_t buf[sizeof(T)] = { 0 };
    std::memcpy(buf, &bits, sizeof(T));
    append<sizeof(T)>(buf);
  }
};

} // namespace swift

#endif // SWIFT_BASIC_STABLEPATH_H
