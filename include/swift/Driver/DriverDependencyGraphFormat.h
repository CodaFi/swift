//===---- DriverDependencyGraphFormat.h - swiftdeps format ---*- C++ -*-======//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2021 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_DRIVER_DRIVER_DEPENDENCY_GRAPH_FORMAT_H
#define SWIFT_DRIVER_DRIVER_DEPENDENCY_GRAPH_FORMAT_H

#include "llvm/Bitcode/RecordLayout.h"
#include "llvm/Bitstream/BitCodes.h"

namespace llvm {
class MemoryBuffer;
}

namespace swift {

class DiagnosticEngine;

namespace fine_grained_dependencies {
  class ModuleDepGraph;
}

using llvm::BCFixed;
using llvm::BCVBR;
using llvm::BCBlob;
using llvm::BCRecordLayout;

/// Every .swiftdeps file begins with these 4 bytes, for easy identification when
/// debugging.
const unsigned char DRIVER_DEPDENENCY_FORMAT_SIGNATURE[] = {'D', 'D', 'E', 'P'};

const unsigned DRIVER_DEPENDENCY_FORMAT_VERSION_MAJOR = 1;

/// Increment this on every change.
const unsigned DRIVER_DEPENDENCY_FORMAT_VERSION_MINOR = 0;

using IdentifierIDField = BCVBR<13>;

using NodeKindField = BCFixed<3>;
using DeclAspectField = BCFixed<1>;

const unsigned RECORD_BLOCK_ID = llvm::bitc::FIRST_APPLICATION_BLOCKID;

/// The swiftdeps file format consists of a METADATA record, followed by zero or more
/// IDENTIFIER_NODE records.
///
/// Then, there is one SOURCE_FILE_DEP_GRAPH_NODE for each serialized
/// SourceFileDepGraphNode. These are followed by FINGERPRINT_NODE and
/// DEPENDS_ON_DEFINITION_NODE, if the node has a fingerprint and depends-on
/// definitions, respectively.
namespace record_block {
  enum {
    METADATA = 1,
    MODULE_DEP_GRAPH_NODE,
    FINGERPRINT_NODE,
    IDENTIFIER_NODE,
    INCREMENTAL_EXTERNAL_DEPENDENCY_NODE,
  };

  // Always the first record in the file.
  using MetadataLayout = BCRecordLayout<
    METADATA, // ID
    BCFixed<16>, // Dependency graph format major version
    BCFixed<16>, // Dependency graph format minor version
    BCBlob // Compiler version string
  >;

  // After the metadata record, we have zero or more identifier records,
  // for each unique string that is referenced from a SourceFileDepGraphNode.
  //
  // Identifiers are referenced by their sequence number, starting from 1.
  // The identifier value 0 is special; it always represents the empty string.
  // There is no IDENTIFIER_NODE serialized that corresponds to it, instead
  // the first IDENTIFIER_NODE always has a sequence number of 1.
  using IdentifierNodeLayout = BCRecordLayout<
    IDENTIFIER_NODE,
    BCBlob
  >;

  using ModuleDepGraphNodeLayout = BCRecordLayout<
    MODULE_DEP_GRAPH_NODE, // ID
    // The next four fields correspond to the fields of the DependencyKey
    // structure.
    NodeKindField, // DependencyKey::kind
    DeclAspectField, // DependencyKey::aspect
    IdentifierIDField, // DependencyKey::context
    IdentifierIDField, // DependencyKey::name
    BCFixed<1>, // Is this a "provides" node?
    BCFixed<1>, // Does this node have swiftdeps associated with it?
    IdentifierIDField // Swiftdeps
  >;

  // Follows DEPENDS_ON_DEFINITION_NODE when the SourceFileDepGraphNode has a
  // fingerprint set.
  using FingerprintNodeLayout = BCRecordLayout<
    FINGERPRINT_NODE,
    BCBlob
  >;

  using IncrementalExternalNodeLayout = BCRecordLayout<
    INCREMENTAL_EXTERNAL_DEPENDENCY_NODE,
    BCBlob
  >;
}

/// Tries to read the dependency graph from the given path name.
/// Returns true if there was an error.
bool readDriverDependencyGraph(
         llvm::StringRef path,
         fine_grained_dependencies::ModuleDepGraph &g);


/// Tries to write out the given dependency graph with the given
/// bitstream writer.
void writeDriverDependencyGraphToPath(
    DiagnosticEngine &diags, StringRef path,
    const fine_grained_dependencies::ModuleDepGraph &g);

} // namespace swift

#endif
