//===--- FrontendToolRequests.h - FrontendTool Requests ---------------------*-
// C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
//  This file defines FrontendTool requests.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_FrontendTool_REQUESTS_H
#define SWIFT_FrontendTool_REQUESTS_H

#include "swift/AST/ASTTypeIDs.h"
#include "swift/AST/SimpleRequest.h"
#include "swift/Basic/AnyValue.h"
#include "swift/Basic/Statistic.h"

namespace swift {

class CompilerInvocation;
class CompilerInstance;
class FileUnit;
class SILModule;

/// Report that a request of the given kind is being evaluated, so it
/// can be recorded by the stats reporter.
template <typename Request>
void reportEvaluatedRequest(UnifiedStatsReporter &stats,
                            const Request &request);

class GenerateSILRequest
    : public SimpleRequest<GenerateSILRequest,
                           SILModule *(const CompilerInvocation *Invocation,
                                       CompilerInstance *Instance),
                           CacheKind::Uncached> {
public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  // Evaluation.
  llvm::Expected<SILModule *> evaluate(Evaluator &evaluator,
                                       const CompilerInvocation *Invocation,
                                       CompilerInstance *Instance) const;

public:
  bool isCached() const { return true; }
};

class GenerateSILForSourceFileRequest
    : public SimpleRequest<GenerateSILForSourceFileRequest,
                           SILModule *(FileUnit *,
                                       const CompilerInvocation *Invocation,
                                       CompilerInstance *Instance),
                           CacheKind::Uncached> {
public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  // Evaluation.
  llvm::Expected<SILModule *> evaluate(Evaluator &evaluator, FileUnit *Unit,
                                       const CompilerInvocation *Invocation,
                                       CompilerInstance *Instance) const;

public:
  bool isCached() const { return true; }
};

/// The zone number for FrontendTool.
#define SWIFT_TYPEID_ZONE FrontendTool
#define SWIFT_TYPEID_HEADER "swift/AST/FrontendToolTypeIDZone.def"
#include "swift/Basic/DefineTypeIDZone.h"
#undef SWIFT_TYPEID_ZONE
#undef SWIFT_TYPEID_HEADER

// Set up reporting of evaluated requests.
#define SWIFT_REQUEST(Zone, RequestType, Sig, Caching, LocOptions)             \
  template <>                                                                  \
  inline void reportEvaluatedRequest(UnifiedStatsReporter &stats,              \
                                     const RequestType &request) {             \
    ++stats.getFrontendCounters().RequestType;                                 \
  }
#include "swift/AST/FrontendToolTypeIDZone.def"
#undef SWIFT_REQUEST

} // end namespace swift

#endif // SWIFT_FrontendTool_REQUESTS_H
