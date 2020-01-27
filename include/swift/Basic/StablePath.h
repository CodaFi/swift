//===--- StablePath.h - Stable Paths ----------------------------*- C++ -*-===//
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

#include "swift/Basic/StableHasher.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/StringRef.h"

namespace swift {

class StablePath final {
public:
  enum class Component : uint8_t {
    Tombstone = 0,
    Module    = 1,
    Container = 2,
    Name      = 3,
  };

  struct ID {
    using value_type = uint64_t;

    friend StablePath;

  public:
    bool operator==(ID RHS) const {
      return Fingerprint == RHS.Fingerprint;
    }
    bool operator!=(ID RHS) const { return !(*this == RHS); }

    friend llvm::DenseMapInfo<swift::StablePath>;

  private:
    explicit ID(value_type fp) : Fingerprint(fp) {};

  private:
    value_type Fingerprint;
  };

private:
  using StableHasher = swift::SipHasher;

  StablePath::ID Parent;
  Component Kind;
  uint64_t ExtraData;

private:
  StablePath(StablePath::ID parent, Component kind, uint64_t data)
    : Parent(parent), Kind(kind), ExtraData(data) {}

public:
  // Special tombstone value.
  StablePath() : Parent(0), Kind(Component::Tombstone), ExtraData(0) {}

  constexpr StablePath(const StablePath &) = default;
  StablePath &operator=(const StablePath &) = default;
  constexpr StablePath(StablePath &&) = default;
  StablePath &operator=(StablePath &&) = default;

private:
  template <typename T>
  static uint64_t hash_all(const T &arg) {
    auto hasher = StableHasher::defaultHasher();
    hasher.combine(arg);
    return std::move(hasher).finalize();
  }

  template <typename T, typename ...Ts>
  static uint64_t hash_all(const T &arg, const Ts &...args) {
    auto hasher = StableHasher::defaultHasher();
    hasher.combine(arg, args...);
    return std::move(hasher).finalize();
  }

public:
  template <typename ...T>
  static StablePath root(const T &...extras) {
    return StablePath { ID(0), Component::Module, StablePath::hash_all(extras...) };
  }

  template <typename ...T>
  static StablePath container(StablePath parent, const T &...extras) {
    return StablePath { parent.fingerprint(), Component::Container, StablePath::hash_all(extras...) };
  }

  template <typename ...T>
  static StablePath name(StablePath parent, const T &...extras) {
    return StablePath { parent.fingerprint(), Component::Container, StablePath::hash_all(extras...) };
  }

  StablePath::ID fingerprint() const {
    auto hasher = StableHasher::defaultHasher();
    // Mangle in a discriminator.
    switch (Kind) {
    case Component::Tombstone:
      llvm_unreachable("Tried to fingerprint a tombstone!");
    case Component::Module:
      hasher.combine(Kind, ExtraData);
      break;
    case Component::Container:
      hasher.combine(Parent.Fingerprint, Kind, ExtraData);
      break;
    case Component::Name:
      hasher.combine(Parent.Fingerprint, Kind, ExtraData);
      break;
    }
    return StablePath::ID{std::move(hasher).finalize()};
  }

  bool operator==(StablePath RHS) const {
    return Parent == RHS.Parent &&
           Kind == RHS.Kind &&
           ExtraData == RHS.ExtraData;
  }
  bool operator!=(StablePath RHS) const { return !(*this == RHS); }
};

} // namespace swift

namespace llvm {

template<>
struct DenseMapInfo<swift::StablePath> {
  static inline swift::StablePath getEmptyKey() {
    return swift::StablePath();
  }
  static inline swift::StablePath getTombstoneKey() {
    return swift::StablePath();
  }
  static inline unsigned getHashValue(swift::StablePath ref) {
    return (unsigned)ref.fingerprint().Fingerprint;
  }
  static bool isEqual(swift::StablePath a, swift::StablePath b) {
    return a == b;
  }
};

} // namespace llvm

#endif // SWIFT_BASIC_STABLEPATH_H
