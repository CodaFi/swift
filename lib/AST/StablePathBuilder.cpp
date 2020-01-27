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
#include "swift/AST/Expr.h"
#include "swift/AST/Module.h"

using namespace swift;


namespace swift {

template<>
struct SipHasher::Combiner<Identifier> {
  static void combine(SipHasher &hasher, const Identifier &ident) {
    hasher.combine_range(ident.str().begin(), ident.str().end());
  }
};

template<>
struct SipHasher::Combiner<StringRef> {
  static void combine(SipHasher &hasher, const StringRef &str) {
    hasher.combine_range(str.begin(), str.end());
  }
};

} // namespace llvm

llvm::Expected<StablePath>
StablePathRequest::evaluate(Evaluator &evaluator, const Decl *decl) const {
  auto *DC = decl->getDeclContext();

  auto parentPath = evaluateOrDefault(evaluator,
                                      StablePathRequest{DC->getAsDecl()},
                                      StablePath::root(StringRef()));
  switch (decl->getKind()) {
  // MARK: Ignored
  case DeclKind::TopLevelCode:
    return parentPath;
  case DeclKind::IfConfig:
    return parentPath;

  // MARK: Roots
  case DeclKind::Module:
    return StablePath::root(cast<ModuleDecl>(decl)->getName());

  // MARK: Containers
  case DeclKind::Enum:
    return StablePath::container(parentPath,
                                 cast<EnumDecl>(decl)->getName());
  case DeclKind::Struct:
    return StablePath::container(parentPath,
                                 cast<StructDecl>(decl)->getName());
  case DeclKind::Class:
    return StablePath::container(parentPath,
                                 cast<ClassDecl>(decl)->getName());
  case DeclKind::Protocol:
    return StablePath::container(parentPath,
                                 cast<ProtocolDecl>(decl)->getName());
  case DeclKind::Extension:
    return StablePath::container(parentPath,
                                 cast<ExtensionDecl>(decl)->getExtendedType()->getString());
  case DeclKind::EnumCase:
    return StablePath::container(parentPath,
                                 cast<EnumCaseDecl>(decl)->getElements().size());

  // MARK: Names
  case DeclKind::OpaqueType:
    return StablePath::name(parentPath,
                            cast<OpaqueTypeDecl>(decl)->getName());
  case DeclKind::TypeAlias:
    return StablePath::name(parentPath,
                            cast<TypeAliasDecl>(decl)->getName());
  case DeclKind::GenericTypeParam:
    return StablePath::name(parentPath,
                            cast<GenericTypeParamDecl>(decl)->getName());
  case DeclKind::AssociatedType:
    return StablePath::name(parentPath,
                            cast<AssociatedTypeDecl>(decl)->getName());
  case DeclKind::Var:
    return StablePath::name(parentPath,
                            cast<VarDecl>(decl)->getName());
  case DeclKind::Param:
    return StablePath::name(parentPath,
                            cast<ParamDecl>(decl)->getName());
  case DeclKind::Subscript:
    return StablePath::name(parentPath,
                            cast<SubscriptDecl>(decl)->getFullName());
  case DeclKind::Constructor:
    return StablePath::name(parentPath,
                            cast<ConstructorDecl>(decl)->getFullName());
  case DeclKind::Destructor:
    return StablePath::name(parentPath,
                            cast<DestructorDecl>(decl)->getFullName());
  case DeclKind::Func:
    return StablePath::name(parentPath,
                            cast<FuncDecl>(decl)->getFullName());
  case DeclKind::Accessor:
    return StablePath::name(parentPath,
                            cast<AccessorDecl>(decl)->getKind(),
                            cast<AccessorDecl>(decl)->getFullName());
  case DeclKind::Import:
    return StablePath::name(parentPath,
                            cast<ImportDecl>(decl)->getDeclPath());
  case DeclKind::PoundDiagnostic:
    return StablePath::name(parentPath,
                            cast<PoundDiagnosticDecl>(decl)->getKind(),
                            cast<PoundDiagnosticDecl>(decl)->getMessage()->getValue());
  case DeclKind::PrecedenceGroup:
    return StablePath::name(parentPath,
                            cast<PrecedenceGroupDecl>(decl)->getAssociativity(),
                            cast<PrecedenceGroupDecl>(decl)->getName());
  case DeclKind::MissingMember:
    return StablePath::name(parentPath,
                            cast<MissingMemberDecl>(decl)->getFullName());
  case DeclKind::PatternBinding:
    return StablePath::name(parentPath,
                            cast<PatternBindingDecl>(decl)->getStaticSpelling());
  case DeclKind::InfixOperator:
    return StablePath::name(parentPath,
                            cast<InfixOperatorDecl>(decl)->getName());
  case DeclKind::PrefixOperator:
    return StablePath::name(parentPath,
                            cast<PrefixOperatorDecl>(decl)->getName());
  case DeclKind::PostfixOperator:
    return StablePath::name(parentPath,
                            cast<PostfixOperatorDecl>(decl)->getName());
  case DeclKind::EnumElement:
    return StablePath::name(parentPath,
                            cast<EnumElementDecl>(decl)->getFullName());
  }
};
