//===--- AliveIRGen.cpp --------------------------===//
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

#include "swift/AST/IRGenOptions.h"
#include "swift/AST/IRGenRequests.h"
#include "swift/Basic/PrimarySpecificPaths.h"
#include "swift/SIL/SILModule.h"
#include "swift/SILOptimizer/PassManager/Transforms.h"
#include "swift/Subsystems.h"
#include "swift/TBDGen/TBDGen.h"

using namespace swift;

namespace {

class AliveIRGen : public SILModuleTransform {
  void run() override {
    llvm::errs() << "\tBegin AliveIRGen\n"; // TODO remove

    // Generate LLLVM IR for the SILModule
    std::unique_ptr<SILModule> SILMod(getModule());
    TBDGenOptions TBDGenOpts{};
    IRGenOptions IRGenOpts{};
    PrimarySpecificPaths primarySpecificPaths{};
    llvm::GlobalVariable *HashGlobal;

    GeneratedModule generatedModule = performIRGeneration(
        SILMod->getSwiftModule(), IRGenOpts, TBDGenOpts, std::move(SILMod),
        /*ModuleName=*/ "", primarySpecificPaths,
        /*parallelOutputFilenames=*/ {}, &HashGlobal);

    llvm::Module *mod = generatedModule.getModule();

    // The version of this tool that translated SIL to Alive IR directly 
    // will be a \c SILFunctionTransform instead of a \c SILModuleTransform.
    // So, for simplicity, only consider the first function.
    llvm::Function *function = &*mod->begin();

    // Lower LLVM IR to Alive IR
    // TODO

    // Store Alive IR in ASTContext
    // TODO

    llvm::errs() << "\tDone AliveIRGen\n"; // TODO remove
  }
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
//                           Top Level Entry Point
//===----------------------------------------------------------------------===//

SILTransform *swift::createAliveIRGen() {
  return new AliveIRGen();
}
