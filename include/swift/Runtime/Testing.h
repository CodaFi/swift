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

namespace swift {

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type-c-linkage"

#ifdef SWIFT_RUNTIME_USE_EXISTENTIAL_TEST_ABI

struct AnyTestWitnessTable;

/// The layout of AnyTest existential.
struct AnyTest {
  OpaqueExistentialContainer Header;
  const AnyTestWitnessTable *TestWitness;
};


// FIXME: Roll this into a versioned struct.
using swift_test_visitor_t = void (*)(AnyTest);

SWIFT_RUNTIME_EXPORT
void swift_enumerateTests_f(swift_test_visitor_t visitor);

#ifndef __has_feature
# define __has_feature(x) 0
#endif

#if __has_feature(blocks)
// FIXME: Roll this into a versioned struct.
using swift_test_visitor_block_t = void (^)(AnyTest);

SWIFT_RUNTIME_EXPORT
void swift_enumerateTests(swift_test_visitor_block_t _Nonnull block);
#endif

#else

// FIXME: Roll this into a versioned struct.
using swift_test_visitor_t = void (const char * _Nonnull Name, SWIFT_CC(swift) void (*invoke)(void));

SWIFT_RUNTIME_EXPORT
void swift_enumerateTests_f(swift_test_visitor_t visitor);

#ifndef __has_feature
# define __has_feature(x) 0
#endif

#if __has_feature(blocks)
// FIXME: Roll this into a versioned struct.
using swift_test_visitor_block_t = void (^)(const char * _Nonnull, SWIFT_CC(swift) void (*invoke)(void));

SWIFT_RUNTIME_EXPORT
void swift_enumerateTests(swift_test_visitor_block_t _Nonnull block);
#endif

#endif // SWIFT_RUNTIME_USE_EXISTENTIAL_TEST_ABI

#pragma clang diagnostic pop

} // end namespace swift

#endif // SWIFT_TESTING_METADATA_H
