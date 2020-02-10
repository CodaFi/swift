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

#ifndef SWIFT_BASIC_STABLEHASHER_H
#define SWIFT_BASIC_STABLEHASHER_H

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
  template<typename T>
  struct Combiner {
    //static void combine(SipHasher &hasher, const T &Val);
  };

public:
  /// Consume this stable hasher and compute the final 64-bit stable hash value.
  uint64_t finalize() &&;

  template<uint64_t N>
  void combine(uint8_t (&bits)[N]) {
    static_assert(N > 0, "Cannot append VLA");
    static_assert(N <= 8, "Can only append up to 64 bits at a time");

    auto fillBitsFromBuffer = [](uint64_t fill, uint8_t *bits) {
      uint64_t head = 0;
      switch (fill) {
      case 7:
        head |= uint64_t(bits[6]) << 48;
        LLVM_FALLTHROUGH;
      case 6:
        head |= uint64_t(bits[5]) << 40;
        LLVM_FALLTHROUGH;
      case 5:
        head |= uint64_t(bits[4]) << 32;
        LLVM_FALLTHROUGH;
      case 4:
        head |= uint64_t(bits[3]) << 24;
        LLVM_FALLTHROUGH;
      case 3:
        head |= uint64_t(bits[2]) << 16;
        LLVM_FALLTHROUGH;
      case 2:
        head |= uint64_t(bits[1]) << 8;
        LLVM_FALLTHROUGH;
      case 1:
        head |= uint64_t(bits[0]);
      }
      return head;
    };

    const uint64_t needed = 8 - getTailSize();

    // Fill in the head
    const uint64_t nhead = std::min(N, needed);
    const uint64_t head = fillBitsFromBuffer(nhead, bits);
    tailAndByteCount = (tailAndByteCount | head) + (uint64_t(0x8) << 56);
    compress((head << 32) | getTailSize());

    // Then the tail.
    const uint64_t ntail = N - needed;
    const uint64_t tail = fillBitsFromBuffer(ntail, bits+needed);
    tailAndByteCount = ((ntail + 8) << 56) | (tail >> 32);
  }

  template<typename T>
  typename std::enable_if<std::is_integral<T>::value>::type
  combine(T bits) {
    uint8_t buf[sizeof(T)] = { 0 };
    std::memcpy(buf, &bits, sizeof(T));
    combine<sizeof(T)>(buf);
  }

  template<
    typename EnumType,
    typename std::enable_if<std::is_enum<EnumType>::value>::type* = nullptr>
  void combine(EnumType value) {
    using Underlying = typename std::underlying_type<EnumType>::type;
    return this->template combine<Underlying>(static_cast<Underlying>(value));
  }

  template <typename T>
  auto combine(const T *ptr) -> decltype("Cannot hash-combine pointers!") {};

  template <typename T, typename ...Ts>
  void combine(const T &arg, const Ts &...args) {
    return combine_many(arg, args...);
  }

  template <typename T, typename U>
  void combine(const std::pair<T, U> &arg) {
    return combine_many(arg.first, arg.second);
  }

  template <typename T>
  void combine(const std::basic_string<T> &arg) {
    return combine_range(arg.begin(), arg.end());
  }

  template <typename T,
            decltype(SipHasher::Combiner<T>::combine) * = nullptr>
  void combine(const T &val) {
    return SipHasher::Combiner<T>::combine(*this, val);
  }

  template <typename ValueT>
  void combine_range(ValueT first, ValueT last) {
    if (first == last) {
      return combine(0);
    }

    do {
      combine(*first++);
    } while (first != last);
  }

  template <typename ...Ts>
  void combine(const std::tuple<Ts...> &arg) {
    return combine_tuple(arg, typename std::index_sequence_for<Ts...>{});
  }

private:
  template <typename ...Ts, unsigned ...Indices>
  void combine_tuple(const std::tuple<Ts...> &arg,
                     std::index_sequence<Indices...> indices) {
    return combine_many(hash_value(std::get<Indices>(arg))...);
  }

  // base case.
  void combine_many() { }

  // recursive case
  template <typename T, typename ...Ts>
  void combine_many(const T &arg, const Ts &...args) {
    combine(arg);
    return combine_many(args...);
  }
};

} // namespace swift

#endif // SWIFT_BASIC_STABLEHASHER_H
