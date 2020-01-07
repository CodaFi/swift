//===--- StablePathBuilder.cpp - Stable Path Traversal --------------------===//
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
// FIXME:
//
//===----------------------------------------------------------------------===//

#include "swift/Basic/StablePath.h"
#include "swift/Basic/StableHasher.h"
#include "swift/AST/NameLookupRequests.h"
#include "swift/AST/Evaluator.h"
#include "swift/AST/Decl.h"

using namespace swift;

llvm::Expected<StablePath>
StablePathRequest::evaluate(Evaluator &evaluator, const Decl *decl) const {
  auto *DC = decl->getDeclContext();

  auto parentPath = evaluateOrDefault(evaluator,
                                      StablePathRequest{DC->getAsDecl()},
                                      StablePath::root());
  switch (decl->getKind()) {
  // MARK: Roots
  case DeclKind::Module:
    return StablePath::root(<#const T &extras...#>);

  // MARK: Containers
  case DeclKind::Enum:
    return StablePath::container(parentPath,
                                 <#const T &extras...#>);
  case DeclKind::Struct:
    return StablePath::container(parentPath,
                                 <#const T &extras...#>);
  case DeclKind::Class:
    return StablePath::container(parentPath,
                                 <#const T &extras...#>);
  case DeclKind::Protocol:
    return StablePath::container(parentPath,
                                 <#const T &extras...#>);

  case DeclKind::Extension:
    return StablePath::container(parentPath,
                                 <#const T &extras...#>);
  case DeclKind::TopLevelCode:
    return StablePath::container(parentPath,
                                 <#const T &extras...#>);
  case DeclKind::IfConfig:
    return StablePath::container(parentPath,
                                 <#const T &extras...#>);

  // MARK: Names
  case DeclKind::OpaqueType:
    return StablePath::name(parentPath,
                            <#const T &extras...#>);
  case DeclKind::TypeAlias:
    return StablePath::name(parentPath,
                            <#const T &extras...#>);
  case DeclKind::GenericTypeParam:
    return StablePath::name(parentPath,
                            <#const T &extras...#>);
  case DeclKind::AssociatedType:
    return StablePath::name(parentPath,
                            <#const T &extras...#>);
  case DeclKind::Var:
    return StablePath::name(parentPath,
                            <#const T &extras...#>);
  case DeclKind::Param:
    return StablePath::name(parentPath,
                            <#const T &extras...#>);
  case DeclKind::Subscript:
    return StablePath::name(parentPath,
                            <#const T &extras...#>);
  case DeclKind::Constructor:
    return StablePath::name(parentPath,
                            <#const T &extras...#>);
  case DeclKind::Destructor:
    return StablePath::name(parentPath,
                            <#const T &extras...#>);
  case DeclKind::Func:
    return StablePath::name(parentPath,
                            <#const T &extras...#>);
  case DeclKind::Accessor:
    return StablePath::name(parentPath,
                            <#const T &extras...#>);
  case DeclKind::EnumElement:
    return StablePath::name(parentPath,
                            <#const T &extras...#>);
  case DeclKind::Import:
    return StablePath::name(parentPath,
                            <#const T &extras...#>);
  case DeclKind::PoundDiagnostic:
    return StablePath::name(parentPath,
                            <#const T &extras...#>);
  case DeclKind::PrecedenceGroup:
    return StablePath::name(parentPath,
                        <#const T &extras...#>);
  case DeclKind::MissingMember:
    return StablePath::name(parentPath,
                        <#const T &extras...#>);
  case DeclKind::PatternBinding:
    return StablePath::name(parentPath,
                        <#const T &extras...#>);
  case DeclKind::EnumCase:
    return StablePath::name(parentPath,
                        <#const T &extras...#>);
  case DeclKind::InfixOperator:
    return StablePath::name(parentPath,
                        <#const T &extras...#>);
  case DeclKind::PrefixOperator:
    return StablePath::name(parentPath,
                        <#const T &extras...#>);
  case DeclKind::PostfixOperator:
    return StablePath::name(parentPath,
                        <#const T &extras...#>);
  }
};
