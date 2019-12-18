//===--- IncrementalRequest.h - Dependency-Oriented Requests ----*- C++ -*-===//
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
//  This file defines the IncrementalRequest class template that serves as a
//  base class for requests that perform automatic dependency tracking.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_AST_INCREMENTALREQUEST_H
#define SWIFT_AST_INCREMENTALREQUEST_H

#include "swift/AST/ASTTypeIDs.h"
#include "swift/AST/SimpleRequest.h"
#include "swift/Basic/Statistic.h"
#include "llvm/ADT/Hashing.h"

namespace swift {

class Evaluator;

enum class DependencyKind {
  Source,
  Sink,
};

template<typename Derived, typename Signature, CacheKind Caching, DependencyKind Kind>
class IncrementalRequest;

template <typename Derived, CacheKind Caching, DependencyKind Kind, typename Output, typename ...Inputs>
class IncrementalRequest<Derived, Output(Inputs...), Caching, Kind> : public SimpleRequest<Derived, Output(Inputs...), Caching> {
public:
  explicit IncrementalRequest(const Inputs& ...inputs)
    : SimpleRequest<Derived, Output(Inputs...), Caching>(inputs...) { }
};

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
  StablePath::ID Parent;
  Component Kind;
  StringRef Data;

  struct StableHasher {
    llvm::MD5 MD5;

    StableHasher() = default;

    StablePath::ID finalize() {
      llvm::MD5::MD5Result Result;
      MD5.final(Result);
      return StablePath::ID { Result.low() };
    }
  };

  void stableHash(StableHasher &hasher) const {
    // Mangle in a discriminator.
    switch (Kind) {
    case Component::Module:
      hasher.MD5.update(static_cast<uint8_t>(Kind));
      hasher.MD5.update(Data);
      break;
    case Component::Container: {
      uint8_t buf[sizeof(ID::value_type)];
      std::memcpy(buf, &Parent.Fingerprint, sizeof(ID::value_type));
      hasher.MD5.update(buf);
      hasher.MD5.update(static_cast<uint8_t>(Kind));
      hasher.MD5.update(Data);
    }
      break;
    case Component::Name: {
      uint8_t buf[sizeof(ID::value_type)];
      std::memcpy(buf, &Parent.Fingerprint, sizeof(ID::value_type));
      hasher.MD5.update(buf);
      hasher.MD5.update(static_cast<uint8_t>(Kind));
      hasher.MD5.update(Data);
    }
      break;
    }
  }

private:
  StablePath(StablePath::ID parent, Component kind, StringRef data)
    : Parent(parent), Kind(kind), Data(data) {}

public:
  static StablePath root(StringRef name) {
    return StablePath{ ID(0), Component::Module, name };
  }

  static StablePath container(StablePath parent, StringRef name) {
    return StablePath { parent.fingerprint(), Component::Container, name };
  }

  static StablePath name(StablePath parent, StringRef name) {
    return StablePath { parent.fingerprint(), Component::Container, name };
  }

  StablePath::ID fingerprint() const {
    StableHasher hasher;
    stableHash(hasher);
    return hasher.finalize();
  }
};

} // namespace swift

#endif // SWIFT_BASIC_INCREMENTALREQUEST_H
