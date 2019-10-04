//===--- Testing.h - Swift Language ABI Test Metadata Support --*- C++ -*-===//
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
// Swift runtime support for generating test metadata.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_TESTING_METADATA_H
#define SWIFT_TESTING_METADATA_H

#include "swift/ABI/Metadata.h"
#include "swift/Reflection/Records.h"
#include "swift/Runtime/ExistentialContainer.h"
#include "swift/Runtime/HeapObject.h"

namespace swift {

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type-c-linkage"

struct SwiftError;

struct AnyTestWitnessTable;

/// The layout of AnyTest existential.
struct AnyTest {
  OpaqueExistentialContainer Header;
  const AnyTestWitnessTable *_Nonnull TestWitness;
};

// protocol AnyTest {
struct AnyTestWitnessTable : WitnessTable {
  static_assert(WitnessTableFirstRequirementOffset == 1,
                "Witness table layout changed");

  // init()
  SWIFT_CC(swift)
  void (*_Nonnull init)(OpaqueValue *_Nonnull existentialBox,
                        const Metadata *_Nonnull Self,
                        const Metadata *_Nonnull self,
                        void *_Nonnull *_Nonnull buf);
};

template <typename Invocation>
using swift_test_visitor_t = void (*_Nonnull)(const void *_Nonnull section, void *fptr);

SWIFT_RUNTIME_EXPORT SWIFT_CC(swift)
void swift_enumerateTests_f(
    swift_test_visitor_t<TestInvocation::Global> globalVisitor,
    swift_test_visitor_t<TestInvocation::Metatype> metaVisitor,
    swift_test_visitor_t<TestInvocation::Instance> instanceVisitor);

#ifndef __has_feature
#define __has_feature(x) 0
#endif

#if __has_feature(blocks)
template <typename Invocation>
using swift_test_visitor_block_t =
    void (^_Nonnull)(const void *_Nonnull section, void *fptr);

SWIFT_RUNTIME_EXPORT SWIFT_CC(swift)
void swift_enumerateTests(
    swift_test_visitor_block_t<TestInvocation::Global> globalVisitor,
    swift_test_visitor_block_t<TestInvocation::Metatype> metaVisitor,
    swift_test_visitor_block_t<TestInvocation::Instance> instanceVisitor);

#endif // __has_feature(blocks)

#pragma clang diagnostic pop

}; // end namespace swift

#endif // SWIFT_TESTING_METADATA_H
