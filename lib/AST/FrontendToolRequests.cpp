//===--- FrontendToolRequests.cpp - High-Level Frontend-as-a-Tool Requests ===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2020 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "swift/AST/FrontendToolRequests.h"
#include "swift/AST/ASTContext.h"
#include "swift/AST/FileUnit.h"
#include "swift/AST/Module.h"
#include "swift/SIL/SILModule.h"
#include "swift/Subsystems.h"

using namespace swift;

namespace swift {
// Implement the FrontendTool type zone (zone 20).
#define SWIFT_TYPEID_ZONE FrontendTool
#define SWIFT_TYPEID_HEADER "swift/AST/FrontendToolTypeIDZone.def"
#include "swift/Basic/ImplementTypeIDZone.h"
#undef SWIFT_TYPEID_ZONE
#undef SWIFT_TYPEID_HEADER
} // end namespace swift

// Define request evaluation functions for each of the FrontendTool requests.
static AbstractRequestFunction *frontendToolRequestFunctions[] = {
#define SWIFT_REQUEST(Zone, Name, Sig, Caching, LocOptions)                    \
  reinterpret_cast<AbstractRequestFunction *>(&Name::evaluateRequest),
#include "swift/AST/FrontendToolTypeIDZone.def"
#undef SWIFT_REQUEST
};

void swift::registerFrontendToolRequestFunctions(Evaluator &evaluator) {
  evaluator.registerRequestFunctions(Zone::FrontendTool,
                                     frontendToolRequestFunctions);
}
