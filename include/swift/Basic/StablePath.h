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
#include "llvm/ADT/StringRef.h"

namespace swift {

struct StablePath {
public:
  enum class Component : uint8_t {
    Module    = 0,
    Container = 1,
    Name      = 2,
  };

  struct ID {
    using value_type = uint64_t;

    friend StablePath;

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

  template <typename ...T>
  static uint64_t hash_all(const T &...args) {
    auto hasher = StableHasher::defaultHasher();
    hasher.combine(std::forward<T>(args)...);
    return std::move(hasher).finalize();
  }

public:
  template <typename ...T>
  static StablePath root(const T &...extras) {
    return StablePath { ID(0), Component::Module, hash_all(extras...) };
  }

  template <typename ...T>
  static StablePath container(StablePath parent, const T &...extras) {
    return StablePath { parent.fingerprint(), Component::Container, hash_all(extras...) };
  }

  template <typename ...T>
  static StablePath name(StablePath parent, const T &...extras) {
    return StablePath { parent.fingerprint(), Component::Container, hash_all(extras...) };
  }

  StablePath::ID fingerprint() const {
    auto hasher = StableHasher::defaultHasher();
    // Mangle in a discriminator.
    switch (Kind) {
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
};

} // namespace swift

#endif // SWIFT_BASIC_STABLEPATH_H
