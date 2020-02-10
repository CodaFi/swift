//===--- StableHasher.cpp - Stable Hasher ---------------------------------===//
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

#include "swift/Basic/StableHasher.h"
#include "llvm/ADT/APInt.h"
#include "llvm/Support/Endian.h"
#include <algorithm>

using namespace swift;

namespace {
  static inline uint64_t rotl64(uint64_t x, uint64_t n) {
    return (x << n) | (x >> (64 - n));
  }

  static inline void sip_round(uint64_t &v0, uint64_t &v1,
                               uint64_t &v2, uint64_t &v3) {
    v0 = v0 &+ v1;
    v1 =  rotl64(v1, 13);
    v1 ^= v0;
    v0 = rotl64(v0, 32);
    v2 = v2 &+ v3;
    v3 = rotl64(v3, 16);
    v3 ^= v2;
    v0 = v0 &+ v3;
    v3 = rotl64(v3, 21);
    v3 ^= v0;
    v2 = v2 &+ v1;
    v1 = rotl64(v1, 17);
    v1 ^= v2;
    v2 = rotl64(v2, 32);
  }
}; // end anonymous namespace


void SipHasher::compress(uint64_t value) {
  state.v3 ^= value;
  for (unsigned i = 0; i < 2; ++i) {
    ::sip_round(state.v0, state.v1, state.v2, state.v3);
  }
  state.v0 ^= value;
}

uint64_t SipHasher::finalize() && {
  compress(tailAndByteCount);
  state.v2 ^= 0xff;

  for (unsigned i = 0; i < 4; ++i) {
    ::sip_round(state.v0, state.v1, state.v2, state.v3);
  }

  return state.v0 ^ state.v1 ^ state.v2 ^ state.v3;
}

