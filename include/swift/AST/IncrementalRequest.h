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
#include "swift/Basic/StablePath.h"

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

} // namespace swift

#endif // SWIFT_BASIC_INCREMENTALREQUEST_H
