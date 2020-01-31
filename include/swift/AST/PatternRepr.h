//===--- PatternRepr.h - Swift Pattern Representation -----------*- C++ -*-===//
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
// This file defines PatternRepr and related classes.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_AST_TYPEREPR_H
#define SWIFT_AST_TYPEREPR_H

#include "swift/AST/Attr.h"
#include "swift/AST/Decl.h"
#include "swift/AST/Identifier.h"
#include "swift/AST/Type.h"
#include "swift/AST/TypeAlignments.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/STLExtras.h"
#include "swift/Basic/Debug.h"
#include "swift/Basic/Located.h"
#include "swift/Basic/InlineBitfield.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TrailingObjects.h"

namespace swift {
  class ASTWalker;

enum class PatternReprKind : uint8_t {
#define PATTERNREPR(ID, PARENT) ID,
#define LAST_PATTERNREPR(ID) Last_TypeRepr = ID,
#include "PatternReprNodes.def"
};
enum : unsigned { NumPatternReprKindBits =
  countBitsUsed(static_cast<unsigned>(PatternReprKind::Last_TypeRepr)) };

/// Representation of a type as written in source.
class alignas(8) PatternRepr {
  PatternRepr(const TypeRepr&) = delete;
  void operator=(const PatternRepr&) = delete;

protected:
  union { uint64_t OpaqueBits;

  SWIFT_INLINE_BITFIELD_BASE(PatternRepr, bitmax(NumPatternReprKindBits,8)+1,
    /// The subclass of PatternRepr that this is.
    Kind : bitmax(NumPatternReprKindBits,8),

    IsImplicit : 1
  );

  SWIFT_INLINE_BITFIELD_FULL(TuplePatternRepr, PatternRepr, 32,
    : NumPadBits,
    NumElements : 32
  );

  SWIFT_INLINE_BITFIELD(TypedPatternRepr, PatternRepr, 1,
    IsPropagatedType : 1
  );

  SWIFT_INLINE_BITFIELD(BoolPatternRepr, PatternRepr, 1,
    Value : 1
  );

  SWIFT_INLINE_BITFIELD(VarPatternRepr, PatternRepr, 1,
    /// True if this is a let pattern, false if a var pattern.
    IsLet : 1
  );

  } Bits;

  PatternRepr(PatternReprKind K) {
    Bits.OpaqueBits = 0;
    Bits.PatternRepr.Kind = static_cast<unsigned>(K);
    Bits.PatternRepr.IsImplicit = 0;
  }

private:
  SourceLoc getLocImpl() const { return getStartLoc(); }

public:
  PatternReprKind getKind() const {
    return static_cast<PatternReprKind>(Bits.PatternRepr.Kind);
  }

    /// A pattern is implicit if it is compiler-generated and there
  /// exists no source code for it.
  bool isImplicit() const { return Bits.PatternRepr.IsImplicit; }
  void setImplicit() { Bits.PatternRepr.IsImplicit = true; }

  /// Find the smallest subpattern which obeys the property that matching it is
  /// equivalent to matching this pattern.
  ///
  /// Looks through ParenPattern, VarPattern, and TypedPattern.
  PatternRepr *getSemanticsProvidingPatternRepr();
  const PatternRepr *getSemanticsProvidingPatternRepr() const {
    return const_cast<PatternRepr *>(this)->getSemanticsProvidingPatternRepr();
  }

  /// Get the representative location for pointing at this type.
  SourceLoc getLoc() const;

  SourceLoc getStartLoc() const;
  SourceLoc getEndLoc() const;
  SourceRange getSourceRange() const;

  static bool classof(const PatternRepr *T) { return true; }

  /// Walk this pattern representation.
  TypeRepr *walk(ASTWalker &walker);
  TypeRepr *walk(ASTWalker &&walker) {
    return walk(walker);
  }

  //*** Allocation Routines ************************************************/

  void *operator new(size_t bytes, const ASTContext &C,
                     unsigned Alignment = alignof(PatternRepr));

  void *operator new(size_t bytes, void *data) {
    assert(data);
    return data;
  }

  // Make placement new and vanilla new/delete illegal for PatternRepr.
  void *operator new(size_t bytes) = delete;
  void operator delete(void *data) = delete;

  void print(raw_ostream &OS, const PrintOptions &Opts = PrintOptions()) const;
  void print(ASTPrinter &Printer, const PrintOptions &Opts) const;
  SWIFT_DEBUG_DUMP;

  /// Clone the given type representation.
  PatternRepr *clone(const ASTContext &ctx) const;
};

/// A pattern consisting solely of grouping parentheses around a
/// different pattern.
class ParenPatternRepr : public PatternRepr {
  SourceLoc LPLoc, RPLoc;
  PatternRepr *SubPattern;
public:
  ParenPatternRepr(SourceLoc lp, PatternRepr *sub, SourceLoc rp,
                   bool implicit)
    : PatternRepr(PatternReprKind::Paren),
      LPLoc(lp), RPLoc(rp), SubPattern(sub) {
    assert(lp.isValid() == rp.isValid());
    if (implicit)
      setImplicit();
  }

  PatternRepr *getSubPattern() { return SubPattern; }
  const PatternRepr *getSubPattern() const { return SubPattern; }
  void setSubPattern(PatternRepr *p) { SubPattern = p; }

  SourceLoc getLParenLoc() const { return LPLoc; }
  SourceLoc getRParenLoc() const { return RPLoc; }
  SourceRange getSourceRange() const { return SourceRange(LPLoc, RPLoc); }
  SourceLoc getLoc() const { return SubPattern->getLoc(); }

  static bool classof(const PatternRepr *P) {
    return P->getKind() == PatternReprKind::Paren;
  }
};

/// An element of a tuple pattern.
///
/// The fully general form of this is something like:
///    label: (pattern) = initexpr
///
/// The Init and DefArgKind fields are only used in argument lists for
/// functions.  They are not parsed as part of normal pattern grammar.
class TuplePatternEltRepr {
  Identifier Label;
  SourceLoc LabelLoc;
  PatternRepr *ThePattern;

public:
  TuplePatternEltRepr() = default;
  explicit TuplePatternEltRepr(PatternRepr *P) : ThePattern(P) {}

  TuplePatternEltRepr(Identifier Label, SourceLoc LabelLoc, PatternRepr *p)
    : Label(Label), LabelLoc(LabelLoc), ThePattern(p) {}

  Identifier getLabel() const { return Label; }
  SourceLoc getLabelLoc() const { return LabelLoc; }
  void setLabel(Identifier I, SourceLoc Loc) {
    Label = I;
    LabelLoc = Loc;
  }

  PatternRepr *getPattern() { return ThePattern; }
  const PatternRepr *getPattern() const {
    return ThePattern;
  }

  void setPattern(PatternRepr *p) { ThePattern = p; }
};

/// A pattern consisting of a tuple of patterns.
class TuplePatternRepr final : public PatternRepr,
    private llvm::TrailingObjects<TuplePatternRepr, TuplePatternEltRepr> {
  friend TrailingObjects;
  SourceLoc LPLoc, RPLoc;
  // Bits.TuplePattern.NumElements

  TuplePatternRepr(SourceLoc lp, unsigned numElements, SourceLoc rp,
                   bool implicit)
      : PatternRepr(PatternReprKind::Tuple), LPLoc(lp), RPLoc(rp) {
    Bits.TuplePatternRepr.NumElements = numElements;
    assert(lp.isValid() == rp.isValid());
    if (implicit)
      setImplicit();
  }

public:
  static TuplePatternRepr *create(ASTContext &C, SourceLoc lp,
                                  ArrayRef<TuplePatternEltRepr> elements,
                                  SourceLoc rp,
                                  bool implicit);

  /// Create either a tuple pattern or a paren pattern, depending
  /// on the elements.
  static PatternRepr *createSimple(ASTContext &C, SourceLoc lp,
                                   ArrayRef<TuplePatternEltRepr> elements,
                                   SourceLoc rp,
                                   bool implicit);

  unsigned getNumElements() const {
    return Bits.TuplePatternRepr.NumElements;
  }

  MutableArrayRef<TuplePatternEltRepr> getElements() {
    return {getTrailingObjects<TuplePatternEltRepr>(), getNumElements()};
  }
  ArrayRef<TuplePatternEltRepr> getElements() const {
    return {getTrailingObjects<TuplePatternEltRepr>(), getNumElements()};
  }

  const TuplePatternEltRepr &getElement(unsigned i) const {return getElements()[i];}
  TuplePatternEltRepr &getElement(unsigned i) { return getElements()[i]; }

  SourceLoc getLParenLoc() const { return LPLoc; }
  SourceLoc getRParenLoc() const { return RPLoc; }
  SourceRange getSourceRange() const;

  static bool classof(const PatternRepr *P) {
    return P->getKind() == PatternReprKind::Tuple;
  }
};

/// A pattern which binds a name to an arbitrary value of its type.
class NamedPatternRepr : public PatternRepr {
  VarDecl *const Var;

public:
  explicit NamedPatternRepr(VarDecl *Var, bool implicit)
      : PatternRepr(PatternReprKind::Named), Var(Var) {
    if (implicit)
      setImplicit();
  }

  VarDecl *getDecl() const { return Var; }
  Identifier getBoundName() const;
  StringRef getNameStr() const { return Var->getNameStr(); }

  SourceLoc getLoc() const { return Var->getLoc(); }
  SourceRange getSourceRange() const { return Var->getSourceRange(); }

  static bool classof(const PatternRepr *P) {
    return P->getKind() == PatternReprKind::Named;
  }
};

/// A pattern which matches an arbitrary value of a type, but does not
/// bind a name to it.  This is spelled "_".
class AnyPatternRepr : public PatternRepr {
  SourceLoc Loc;

public:
  explicit AnyPatternRepr(SourceLoc Loc, bool implicit)
      : PatternRepr(PatternReprKind::Any), Loc(Loc) {
    if (implicit)
      setImplicit();
  }

  SourceLoc getLoc() const { return Loc; }
  SourceRange getSourceRange() const { return Loc; }

  static bool classof(const PatternRepr *P) {
    return P->getKind() == PatternReprKind::Any;
  }
};

/// A pattern which matches a sub-pattern and annotates it with a
/// type. It is a compile-time error if the pattern does not statically match
/// a value of the type. This is different from IsPattern, which is a refutable
/// dynamic type match.
class TypedPatternRepr : public PatternRepr {
  PatternRepr *SubPattern;
  TypeRepr *PatTypeRepr;

public:
  /// Creates a new TypedPattern which annotates the provided sub-pattern with
  /// the provided TypeRepr. If 'implicit' is true, the pattern will be
  /// set to implicit. If false, it will not. If 'implicit' is not provided,
  /// then the pattern will be set to 'implicit' if there is a provided TypeRepr
  /// which has a valid SourceRange.
  TypedPatternRepr(PatternRepr *pattern, TypeRepr *tr, bool implicit);

  PatternRepr *getSubPattern() { return SubPattern; }
  const PatternRepr *getSubPattern() const { return SubPattern; }
  void setSubPattern(PatternRepr *p) { SubPattern = p; }

  TypeRepr *getTypeRepr() const { return PatTypeRepr; }

  TypeLoc getTypeLoc() const;
  SourceLoc getLoc() const;
  SourceRange getSourceRange() const;

  static bool classof(const PatternRepr *P) {
    return P->getKind() == PatternReprKind::Typed;
  }
};

/// A pattern which performs a dynamic type check. The match succeeds if the
/// class, archetype, or existential value is dynamically of the given type.
class IsPatternRepr : public PatternRepr {
  SourceLoc IsLoc;

  PatternRepr *SubPattern;

  /// The semantics of the type check (class downcast, archetype-to-concrete,
  /// etc.)
  CheckedCastKind CastKind;

  /// The type being checked for.
  TypeLoc CastType;

public:
  IsPatternRepr(SourceLoc IsLoc, TypeLoc CastTy,
                PatternRepr *SubPattern,
                CheckedCastKind Kind,
                bool implicit)
    : PatternRepr(PatternReprKind::Is),
      IsLoc(IsLoc),
      SubPattern(SubPattern),
      CastKind(Kind),
      CastType(CastTy) {
    assert(IsLoc.isValid() == CastTy.hasLocation());
    if (implicit)
      setImplicit();
  }

  CheckedCastKind getCastKind() const { return CastKind; }
  void setCastKind(CheckedCastKind kind) { CastKind = kind; }

  bool hasSubPattern() const { return SubPattern; }
  PatternRepr *getSubPattern() { return SubPattern; }
  const PatternRepr *getSubPattern() const { return SubPattern; }
  void setSubPattern(PatternRepr *p) { SubPattern = p; }

  SourceLoc getLoc() const { return IsLoc; }
  SourceRange getSourceRange() const {
    SourceLoc beginLoc =
      SubPattern ? SubPattern->getSourceRange().Start : IsLoc;
    SourceLoc endLoc =
      (isImplicit() ? beginLoc : CastType.getSourceRange().End);
    return { beginLoc, endLoc };
  }

  TypeLoc &getCastTypeLoc() { return CastType; }
  TypeLoc getCastTypeLoc() const { return CastType; }

  static bool classof(const PatternRepr *P) {
    return P->getKind() == PatternReprKind::Is;
  }
};

/// A pattern that matches an enum case. If the enum value is in the matching
/// case, then the value is extracted. If there is a subpattern, it is then
/// matched against the associated value for the case.
class EnumElementPatternRepr : public PatternRepr {
  TypeLoc ParentType;
  SourceLoc DotLoc;
  DeclNameLoc NameLoc;
  DeclNameRef Name;
  PointerUnion<EnumElementDecl *, Expr*> ElementDeclOrUnresolvedOriginalExpr;
  PatternRepr /*nullable*/ *SubPattern;

public:
  EnumElementPatternRepr(TypeLoc ParentType, SourceLoc DotLoc, DeclNameLoc NameLoc,
                         DeclNameRef Name, EnumElementDecl *Element,
                         PatternRepr *SubPattern, bool Implicit)
    : PatternRepr(PatternReprKind::EnumElement),
      ParentType(ParentType), DotLoc(DotLoc), NameLoc(NameLoc), Name(Name),
      ElementDeclOrUnresolvedOriginalExpr(Element),
      SubPattern(SubPattern) {
    if (Implicit)
      setImplicit();
  }

  /// Create an unresolved EnumElementPatternRepr for a `.foo` pattern relying on
  /// contextual type.
  EnumElementPatternRepr(SourceLoc DotLoc,
                         DeclNameLoc NameLoc,
                         DeclNameRef Name,
                         PatternRepr *SubPattern,
                         Expr *UnresolvedOriginalExpr)
    : PatternRepr(PatternReprKind::EnumElement),
      ParentType(), DotLoc(DotLoc), NameLoc(NameLoc), Name(Name),
      ElementDeclOrUnresolvedOriginalExpr(UnresolvedOriginalExpr),
      SubPattern(SubPattern) {

  }

  bool hasSubPattern() const { return SubPattern; }

  const PatternRepr *getSubPattern() const {
    return SubPattern;
  }

  PatternRepr *getSubPattern() {
    return SubPattern;
  }

  bool isParentTypeImplicit() {
    return !ParentType.hasLocation();
  }

  void setSubPattern(PatternRepr *p) { SubPattern = p; }

  DeclNameRef getName() const { return Name; }

  EnumElementDecl *getElementDecl() const {
    return ElementDeclOrUnresolvedOriginalExpr.dyn_cast<EnumElementDecl*>();
  }
  void setElementDecl(EnumElementDecl *d) {
    ElementDeclOrUnresolvedOriginalExpr = d;
  }

  Expr *getUnresolvedOriginalExpr() const {
    return ElementDeclOrUnresolvedOriginalExpr.get<Expr*>();
  }
  bool hasUnresolvedOriginalExpr() const {
    return ElementDeclOrUnresolvedOriginalExpr.is<Expr*>();
  }

  DeclNameLoc getNameLoc() const { return NameLoc; }
  SourceLoc getLoc() const { return NameLoc.getBaseNameLoc(); }
  SourceLoc getStartLoc() const {
    return ParentType.hasLocation() ? ParentType.getSourceRange().Start :
           DotLoc.isValid()         ? DotLoc
                                    : NameLoc.getBaseNameLoc();
  }
  SourceLoc getEndLoc() const {
    if (SubPattern && SubPattern->getSourceRange().isValid()) {
      return SubPattern->getSourceRange().End;
    }
    return NameLoc.getEndLoc();
  }
  SourceRange getSourceRange() const { return {getStartLoc(), getEndLoc()}; }

  TypeLoc &getParentType() { return ParentType; }
  TypeLoc getParentType() const { return ParentType; }

  static bool classof(const PatternRepr *P) {
    return P->getKind() == PatternReprKind::EnumElement;
  }
};

/// A pattern that matches an enum case. If the enum value is in the matching
/// case, then the value is extracted. If there is a subpattern, it is then
/// matched against the associated value for the case.
class BoolPatternRepr : public PatternRepr {
  SourceLoc NameLoc;

public:
  BoolPatternRepr(SourceLoc NameLoc, bool Value)
      : PatternRepr(PatternReprKind::Bool), NameLoc(NameLoc) {
    Bits.BoolPatternRepr.Value = Value;
  }

  bool getValue() const { return Bits.BoolPatternRepr.Value; }
  void setValue(bool v) { Bits.BoolPatternRepr.Value = v; }

  SourceLoc getNameLoc() const { return NameLoc; }
  SourceLoc getLoc() const { return NameLoc; }
  SourceLoc getStartLoc() const {
    return NameLoc;
  }
  SourceLoc getEndLoc() const {
    return NameLoc;
  }
  SourceRange getSourceRange() const { return {getStartLoc(), getEndLoc()}; }

  static bool classof(const PatternRepr *P) {
    return P->getKind() == PatternReprKind::Bool;
  }
};

/// A pattern "x?" which matches ".some(x)".
class OptionalSomePatternRepr : public PatternRepr {
  PatternRepr *SubPattern;
  SourceLoc QuestionLoc;
  EnumElementDecl *ElementDecl = nullptr;

public:
  explicit OptionalSomePatternRepr(PatternRepr *SubPattern,
                                   SourceLoc QuestionLoc,
                                   bool implicit = false)
  : PatternRepr(PatternReprKind::OptionalSome), SubPattern(SubPattern),
    QuestionLoc(QuestionLoc) {
    if (implicit)
      setImplicit();
  }

  SourceLoc getQuestionLoc() const { return QuestionLoc; }
  SourceRange getSourceRange() const {
    return SourceRange(SubPattern->getStartLoc(), QuestionLoc);
  }

  const PatternRepr *getSubPattern() const { return SubPattern; }
  PatternRepr *getSubPattern() { return SubPattern; }
  void setSubPattern(PatternRepr *p) { SubPattern = p; }

  EnumElementDecl *getElementDecl() const { return ElementDecl; }
  void setElementDecl(EnumElementDecl *d) { ElementDecl = d; }

  static bool classof(const PatternRepr *P) {
    return P->getKind() == PatternReprKind::OptionalSome;
  }
};



/// A pattern which matches a value obtained by evaluating an expression.
/// The match will be tested using user-defined '~=' operator function lookup;
/// the match succeeds if 'patternValue ~= matchedValue' produces a true value.
class ExprPatternRepr : public PatternRepr {
  Expr *SubExpr;

public:
  /// Construct an ExprPattern.
  ExprPatternRepr(Expr *e, bool isResolved, Expr *matchExpr, VarDecl *matchVar,
                  bool implicit);

  /// Construct an unresolved ExprPattern.
  ExprPatternRepr(Expr *e)
    : ExprPatternRepr(e, false, nullptr, nullptr, /*implicit*/false)
  {}

  /// Construct a resolved ExprPattern.
  ExprPatternRepr(Expr *e, Expr *matchExpr, VarDecl *matchVar)
    : ExprPatternRepr(e, true, matchExpr, matchVar, /*implicit*/false)
  {}

  Expr *getSubExpr() const { return SubExpr; }
  void setSubExpr(Expr *e) { SubExpr = e; }

  SourceLoc getLoc() const;
  SourceRange getSourceRange() const;

  static bool classof(const PatternRepr *P) {
    return P->getKind() == PatternReprKind::Expr;
  }
};

/// A pattern which introduces variable bindings. This pattern node has no
/// semantics of its own, but has a syntactic effect on the subpattern. Bare
/// identifiers in the subpattern create new variable bindings instead of being
/// parsed as expressions referencing existing entities.
class VarPatternRepr : public PatternRepr {
  SourceLoc VarLoc;
  PatternRepr *SubPattern;
public:
  VarPatternRepr(SourceLoc loc, bool isLet, PatternRepr *sub,
                 bool implicit)
      : PatternRepr(PatternReprKind::Var), VarLoc(loc), SubPattern(sub) {
    Bits.VarPatternRepr.IsLet = isLet;
    if (implicit)
      setImplicit();
  }

  bool isLet() const { return Bits.VarPatternRepr.IsLet; }

  SourceLoc getLoc() const { return VarLoc; }
  SourceRange getSourceRange() const {
    SourceLoc EndLoc = SubPattern->getSourceRange().End;
    if (EndLoc.isInvalid())
      return VarLoc;
    return {VarLoc, EndLoc};
  }

  const PatternRepr *getSubPattern() const { return SubPattern; }
  PatternRepr *getSubPattern() { return SubPattern; }
  void setSubPattern(PatternRepr *p) { SubPattern = p; }

  static bool classof(const PatternRepr *P) {
    return P->getKind() == PatternReprKind::Var;
  }
};


inline PatternRepr *PatternRepr::getSemanticsProvidingPatternRepr() {
  if (auto *pp = dyn_cast<ParenPatternRepr>(this))
    return pp->getSubPattern()->getSemanticsProvidingPatternRepr();
  if (auto *tp = dyn_cast<TypedPatternRepr>(this))
    return tp->getSubPattern()->getSemanticsProvidingPatternRepr();
  if (auto *vp = dyn_cast<VarPatternRepr>(this))
    return vp->getSubPattern()->getSemanticsProvidingPatternRepr();
  return this;
}

} // end namespace swift

namespace llvm {
  static inline raw_ostream &
  operator<<(raw_ostream &OS, swift::PatternRepr *PatRepr) {
    PatRepr->print(OS);
    return OS;
  }
} // end namespace llvm

#endif
