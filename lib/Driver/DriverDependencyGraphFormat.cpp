//===---- FineGrainedDependencyFormat.cpp - reading and writing swiftdeps -===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2020 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "swift/AST/FileSystem.h"
#include "swift/Driver/DriverDependencyGraphFormat.h"
#include "swift/Driver/FineGrainedDependencyDriverGraph.h"
#include "swift/Basic/PrettyStackTrace.h"
#include "swift/Basic/Version.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Bitstream/BitstreamReader.h"
#include "llvm/Bitstream/BitstreamWriter.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/MemoryBuffer.h"

using namespace swift;
using namespace fine_grained_dependencies;

namespace {

class Deserializer {
  std::vector<std::string> Identifiers;

  llvm::BitstreamCursor &Cursor;

  SmallVector<uint64_t, 64> Scratch;
  StringRef BlobData;

  // These all return true if there was an error.
  bool readSignature();
  bool enterTopLevelBlock();
  bool readMetadata();

  llvm::Optional<std::string> getIdentifier(unsigned n);

public:
  Deserializer(llvm::BitstreamCursor &Cursor) : Cursor(Cursor) {}
  bool readDriverDependencyGraph(ModuleDepGraph &g);
  bool readDriverDependencyGraphsFromSwiftModule(llvm::SmallVectorImpl<ModuleDepGraph> &g);
};

} // end namespace

bool Deserializer::readSignature() {
  for (unsigned char byte : DRIVER_DEPDENENCY_FORMAT_SIGNATURE) {
    if (Cursor.AtEndOfStream())
      return true;
    if (auto maybeRead = Cursor.Read(8)) {
      if (maybeRead.get() != byte)
        return true;
    } else {
      return true;
    }
  }
  return false;
}

bool Deserializer::enterTopLevelBlock() {
  // Read the BLOCKINFO_BLOCK, which contains metadata used when dumping
  // the binary data with llvm-bcanalyzer.
  {
    auto next = Cursor.advance();
    if (!next) {
      consumeError(next.takeError());
      return true;
    }

    if (next->Kind != llvm::BitstreamEntry::SubBlock)
      return true;

    if (next->ID != llvm::bitc::BLOCKINFO_BLOCK_ID)
      return true;

    if (!Cursor.ReadBlockInfoBlock())
      return true;
  }

  // Enters our subblock, which contains the actual dependency information.
  {
    auto next = Cursor.advance();
    if (!next) {
      consumeError(next.takeError());
      return true;
    }

    if (next->Kind != llvm::BitstreamEntry::SubBlock)
      return true;

    if (next->ID != RECORD_BLOCK_ID)
      return true;

    if (auto err = Cursor.EnterSubBlock(RECORD_BLOCK_ID)) {
      consumeError(std::move(err));
      return true;
    }
  }

  return false;
}

bool Deserializer::readMetadata() {
  using namespace record_block;

  auto entry = Cursor.advance();
  if (!entry) {
    consumeError(entry.takeError());
    return true;
  }

  if (entry->Kind != llvm::BitstreamEntry::Record)
    return true;

  auto recordID = Cursor.readRecord(entry->ID, Scratch, &BlobData);
  if (!recordID) {
    consumeError(recordID.takeError());
    return true;
  }

  if (*recordID != METADATA)
    return true;

  unsigned majorVersion, minorVersion;

  MetadataLayout::readRecord(Scratch, majorVersion, minorVersion);
  if (majorVersion != DRIVER_DEPENDENCY_FORMAT_VERSION_MAJOR ||
      minorVersion != DRIVER_DEPENDENCY_FORMAT_VERSION_MINOR) {
    return true;
  }

  return false;
}

static llvm::Optional<NodeKind> getNodeKind(unsigned nodeKind) {
  if (nodeKind < unsigned(NodeKind::kindCount))
    return NodeKind(nodeKind);
  return None;
}

static llvm::Optional<DeclAspect> getDeclAspect(unsigned declAspect) {
  if (declAspect < unsigned(DeclAspect::aspectCount))
    return DeclAspect(declAspect);
  return None;
}

bool Deserializer::readDriverDependencyGraph(fine_grained_dependencies::ModuleDepGraph &g) {
  using namespace record_block;

  if (readSignature())
    return true;

  if (enterTopLevelBlock())
    return true;

  if (readMetadata())
    return true;

  DependencyKey key;
  fine_grained_dependencies::ModuleDepGraphNode *node = nullptr;

  while (!Cursor.AtEndOfStream()) {
    auto entry = cantFail(Cursor.advance(), "Advance bitstream cursor");

    if (entry.Kind == llvm::BitstreamEntry::EndBlock) {
      Cursor.ReadBlockEnd();
      assert(Cursor.GetCurrentBitNo() % CHAR_BIT == 0);
      break;
    }

    if (entry.Kind != llvm::BitstreamEntry::Record)
      llvm::report_fatal_error("Bad bitstream entry kind");

    Scratch.clear();
    unsigned recordID = cantFail(
      Cursor.readRecord(entry.ID, Scratch, &BlobData),
      "Read bitstream record");

    switch (recordID) {
    case METADATA: {
      // METADATA must appear at the beginning and is handled by readMetadata().
      llvm::report_fatal_error("Unexpected METADATA record");
      break;
    }

    case MODULE_DEP_GRAPH_NODE: {
      unsigned nodeKindID, declAspectID, contextID, nameID, isProvides;
      unsigned hasSwiftdeps, swiftDepsID;
      ModuleDepGraphNodeLayout::readRecord(Scratch, nodeKindID, declAspectID,
                                           contextID, nameID, isProvides,
                                           hasSwiftdeps, swiftDepsID);
      auto nodeKind = getNodeKind(nodeKindID);
      if (!nodeKind)
        llvm::report_fatal_error("Bad node kind");
      auto declAspect = getDeclAspect(declAspectID);
      if (!declAspect)
        llvm::report_fatal_error("Bad decl aspect");
      auto context = getIdentifier(contextID);
      if (!context)
        llvm::report_fatal_error("Bad context");
      auto name = getIdentifier(nameID);
      if (!name)
        llvm::report_fatal_error("Bad identifier");
      Optional<std::string> swiftDeps = None;
      if (hasSwiftdeps != 0) {
        auto swiftDepsIdentifier = getIdentifier(nameID);
        if (!swiftDepsIdentifier)
          llvm::report_fatal_error("Bad identifier");
        swiftDeps.emplace(*swiftDepsIdentifier);
      }

      node = new fine_grained_dependencies::ModuleDepGraphNode(key, None, swiftDeps);
      node->setKey(DependencyKey(*nodeKind, *declAspect, *context, *name));
      g.addToMap(node);
      break;
    }

    case FINGERPRINT_NODE: {
      // FINGERPRINT_NODE must follow a SOURCE_FILE_DEP_GRAPH_NODE.
      if (node == nullptr)
        llvm::report_fatal_error("Unexpected FINGERPRINT_NODE record");

      node->setFingerprint(Fingerprint::fromString(BlobData));
      break;
    }

    case IDENTIFIER_NODE: {
      // IDENTIFIER_NODE must come before SOURCE_FILE_DEP_GRAPH_NODE.
      if (node != nullptr)
        llvm::report_fatal_error("Unexpected IDENTIFIER_NODE record");

      IdentifierNodeLayout::readRecord(Scratch);
      Identifiers.push_back(BlobData.str());
      break;
    }

    case INCREMENTAL_EXTERNAL_DEPENDENCY_NODE: {
      // IDENTIFIER_NODE must come before SOURCE_FILE_DEP_GRAPH_NODE.
      if (node == nullptr)
        llvm::report_fatal_error("Unexpected INCREMENTAL_EXTERNAL_DEPENDENCY_NODE record");

      IncrementalExternalNodeLayout::readRecord(Scratch);
      g.insertIncrementalExternalDependency(BlobData);
      break;
    }

    default: {
      llvm::report_fatal_error("Unknown record ID");
    }
    }
  }

  return false;
}

llvm::Optional<std::string> Deserializer::getIdentifier(unsigned n) {
  if (n == 0)
    return std::string();

  --n;
  if (n >= Identifiers.size())
    return None;

  return Identifiers[n];
}

namespace {

class Serializer {
  llvm::StringMap<unsigned, llvm::BumpPtrAllocator> IdentifierIDs;
  unsigned LastIdentifierID = 0;
  std::vector<StringRef> IdentifiersToWrite;

  llvm::BitstreamWriter &Out;

  /// A reusable buffer for emitting records.
  SmallVector<uint64_t, 64> ScratchRecord;

  std::array<unsigned, 256> AbbrCodes;

  void addIdentifier(StringRef str);
  unsigned getIdentifier(StringRef str);

  template <typename Layout>
  void registerRecordAbbr() {
    using AbbrArrayTy = decltype(AbbrCodes);
    static_assert(Layout::Code <= std::tuple_size<AbbrArrayTy>::value,
                  "layout has invalid record code");
    AbbrCodes[Layout::Code] = Layout::emitAbbrev(Out);
  }

  void emitBlockID(unsigned ID, StringRef name,
                   SmallVectorImpl<unsigned char> &nameBuffer);

  void emitRecordID(unsigned ID, StringRef name,
                    SmallVectorImpl<unsigned char> &nameBuffer);

  void writeSignature();
  void writeBlockInfoBlock();
  void writeMetadata();

public:
  Serializer(llvm::BitstreamWriter &ExistingOut) : Out(ExistingOut) {}

public:
  void writeDriverDependencyGraph(const fine_grained_dependencies::ModuleDepGraph &g);
};

} // end namespace

/// Record the name of a block.
void Serializer::emitBlockID(unsigned ID, StringRef name,
                             SmallVectorImpl<unsigned char> &nameBuffer) {
  SmallVector<unsigned, 1> idBuffer;
  idBuffer.push_back(ID);
  Out.EmitRecord(llvm::bitc::BLOCKINFO_CODE_SETBID, idBuffer);

  // Emit the block name if present.
  if (name.empty())
    return;
  nameBuffer.resize(name.size());
  memcpy(nameBuffer.data(), name.data(), name.size());
  Out.EmitRecord(llvm::bitc::BLOCKINFO_CODE_BLOCKNAME, nameBuffer);
}

void Serializer::emitRecordID(unsigned ID, StringRef name,
                              SmallVectorImpl<unsigned char> &nameBuffer) {
  assert(ID < 256 && "can't fit record ID in next to name");
  nameBuffer.resize(name.size()+1);
  nameBuffer[0] = ID;
  memcpy(nameBuffer.data()+1, name.data(), name.size());
  Out.EmitRecord(llvm::bitc::BLOCKINFO_CODE_SETRECORDNAME, nameBuffer);
}

void Serializer::writeSignature() {
  for (auto c : DRIVER_DEPDENENCY_FORMAT_SIGNATURE)
    Out.Emit((unsigned) c, 8);
}

void Serializer::writeBlockInfoBlock() {
  llvm::BCBlockRAII restoreBlock(Out, llvm::bitc::BLOCKINFO_BLOCK_ID, 2);

  SmallVector<unsigned char, 64> nameBuffer;
#define BLOCK(X) emitBlockID(X ## _ID, #X, nameBuffer)
#define BLOCK_RECORD(K, X) emitRecordID(K::X, #X, nameBuffer)

  BLOCK(RECORD_BLOCK);
  BLOCK_RECORD(record_block, METADATA);
  BLOCK_RECORD(record_block, MODULE_DEP_GRAPH_NODE);
  BLOCK_RECORD(record_block, FINGERPRINT_NODE);
  BLOCK_RECORD(record_block, IDENTIFIER_NODE);
}

void Serializer::writeMetadata() {
  using namespace record_block;

  MetadataLayout::emitRecord(Out, ScratchRecord,
                             AbbrCodes[MetadataLayout::Code],
                             DRIVER_DEPENDENCY_FORMAT_VERSION_MAJOR,
                             DRIVER_DEPENDENCY_FORMAT_VERSION_MINOR,
                             version::getSwiftFullVersion());
}

void
Serializer::writeDriverDependencyGraph(const ModuleDepGraph &g) {
  writeSignature();
  writeBlockInfoBlock();

  llvm::BCBlockRAII restoreBlock(Out, RECORD_BLOCK_ID, 8);

  using namespace record_block;

  registerRecordAbbr<MetadataLayout>();
  registerRecordAbbr<ModuleDepGraphNodeLayout>();
  registerRecordAbbr<FingerprintNodeLayout>();
  registerRecordAbbr<IdentifierNodeLayout>();
  registerRecordAbbr<IncrementalExternalNodeLayout>();

  writeMetadata();

  // Make a pass to collect all unique strings
  addIdentifier("");
  g.forEachNode([&](fine_grained_dependencies::ModuleDepGraphNode *node) {
    if (auto swiftDeps = node->getSwiftDeps()) {
      addIdentifier(*swiftDeps);
    }
    addIdentifier(node->getKey().getContext());
    addIdentifier(node->getKey().getName());
  });

  for (auto dep : g.getIncrementalExternalDependencies()) {
    addIdentifier(dep);
  }

  // Write the strings
  for (auto str : IdentifiersToWrite) {
    IdentifierNodeLayout::emitRecord(Out, ScratchRecord,
                                     AbbrCodes[IdentifierNodeLayout::Code],
                                     str);
  }

  // Now write each graph node
  g.forEachNode([&](fine_grained_dependencies::ModuleDepGraphNode *node) {
    ModuleDepGraphNodeLayout::emitRecord(Out, ScratchRecord,
                                         AbbrCodes[ModuleDepGraphNodeLayout::Code],
                                         unsigned(node->getKey().getKind()),
                                         unsigned(node->getKey().getAspect()),
                                         getIdentifier(node->getKey().getContext()),
                                         getIdentifier(node->getKey().getName()),
                                         node->getIsProvides() ? 1 : 0,
                                         node->getSwiftDeps().hasValue(),
                                         getIdentifier(node->getSwiftDepsOrEmpty()));
    if (auto fingerprint = node->getFingerprint()) {
      FingerprintNodeLayout::emitRecord(Out, ScratchRecord,
                                        AbbrCodes[FingerprintNodeLayout::Code],
                                        fingerprint->getRawValue());
    }
  });

  for (auto dep : g.getIncrementalExternalDependencies()) {
    IncrementalExternalNodeLayout::emitRecord(Out, ScratchRecord,
                                              AbbrCodes[IncrementalExternalNodeLayout::Code], dep);
  }
}

void Serializer::addIdentifier(StringRef str) {
  if (str.empty())
    return;

  decltype(IdentifierIDs)::iterator iter;
  bool isNew;
  std::tie(iter, isNew) =
      IdentifierIDs.insert({str, LastIdentifierID + 1});

  if (!isNew)
    return;

  ++LastIdentifierID;
  // Note that we use the string data stored in the StringMap.
  IdentifiersToWrite.push_back(iter->getKey());
}

unsigned Serializer::getIdentifier(StringRef str) {
  if (str.empty())
    return 0;

  auto iter = IdentifierIDs.find(str);
  assert(iter != IdentifierIDs.end());
  assert(iter->second != 0);
  return iter->second;
}

void swift::writeDriverDependencyGraphToPath(
    DiagnosticEngine &diags, StringRef path,
    const fine_grained_dependencies::ModuleDepGraph &g) {
  PrettyStackTraceStringAction stackTrace("saving dependency dependency graph", path);
  withOutputFile(diags, path, [&](llvm::raw_ostream &out) {
    SmallVector<char, 0> Buffer;
    llvm::BitstreamWriter Writer{Buffer};
    Serializer serializer{Writer};
    serializer.writeDriverDependencyGraph(g);
    out.write(Buffer.data(), Buffer.size());
    out.flush();
    return false;
  });
}

bool swift::readDriverDependencyGraph(
    StringRef path, fine_grained_dependencies::ModuleDepGraph &g) {
  auto buffer = llvm::MemoryBuffer::getFile(path);
  if (!buffer)
    return false;
  llvm::BitstreamCursor cursor{buffer.get()->getMemBufferRef()};
  Deserializer deserializer{cursor};
  return !deserializer.readDriverDependencyGraph(g);
}
