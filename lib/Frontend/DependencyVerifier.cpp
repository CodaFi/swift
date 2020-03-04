//===--- DependencyVerifier.cpp - Dependency Verifier ---------------------===//
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
//
//  Implements a verifier for dependencies registered against the
//  ReferencedNameTracker in a SourceFile.
//
//===----------------------------------------------------------------------===//

#include "swift/AST/ASTContext.h"
#include "swift/AST/ASTMangler.h"
#include "swift/AST/ASTPrinter.h"
#include "swift/AST/SourceFile.h"
#include "swift/Demangling/Demangler.h"
#include "swift/Basic/OptionSet.h"
#include "swift/Frontend/DiagnosticVerifier.h"
#include "swift/Parse/Lexer.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/Support/FormatVariadic.h"

using namespace swift;

namespace {

/// An \c Expectation represents a user-provided expectation for a particular
/// dependency entry. An expectation is usually written in-line in a comment
/// attached near the relevant declaration and takes one of the following forms:
///
/// // expected-provides {{ProvidedName}}
/// // expected-private-member {{some.User.member}}
///
/// An expectation contains additional information about the expectation
/// \c Kind, which matches one of the few kinds of dependency entry that are
/// currently representable in the dependency graph, and an expectation
/// \c Scope which must either be \c private or \c cascading.
///
/// As not all combinations of scopes and kinds makes sense, and to ease the addition of further
/// combinations of the two, the supported set of expectations is given by the following matrix:
///
#define EXPECTATION_MATRIX                                                     \
  MATRIX_ENTRY("expected-no-dependency", None, Negative)                       \
  MATRIX_ENTRY("expected-provides", None, Provides)                            \
  MATRIX_ENTRY("expected-private-superclass", Private, Superclass)             \
  MATRIX_ENTRY("expected-cascading-superclass", Cascading, Superclass)         \
  MATRIX_ENTRY("expected-private-conformance", Private, Conformance)           \
  MATRIX_ENTRY("expected-cascading-conformance", Cascading, Conformance)       \
  MATRIX_ENTRY("expected-private-member", Private, Member)                     \
  MATRIX_ENTRY("expected-cascading-member", Cascading, Member)                 \
  MATRIX_ENTRY("expected-private-dynamic-member", Private, DynamicMember)      \
  MATRIX_ENTRY("expected-cascading-dynamic-member", Cascading, DynamicMember)
///
/// To add a new supported combination, update \c Expectation::Kind and
/// \c Expectation::Scope, then define a new \c MATRIX_ENTRY with the following information:
///
/// MATRIX_ENTRY(<Expectation-Selector-String>, Expectation::Scope, Expectation::Kind)
///
/// Where \c <Expectation-Selector-String> matches the grammar for an expectation. The
/// verifier's parsing routines use this matrix to automatically keep the parser in harmony with the
/// internal representation of the expectation.
struct Expectation {
public:
  enum class Kind : uint8_t {
    Negative,
    Provides,
    Member,
    PotentialMember,
    Superclass = PotentialMember,
    Conformance = PotentialMember,
    DynamicMember,
  };

  enum class Scope : uint8_t {
    /// There is no scope information associated with this expectation.
    ///
    /// This is currently only true of negative expectations and provides expectations.
    None,
    /// The dependency does not cascade.
    Private,
    /// The dependency cascades.
    Cascading,
  };

  /// The full range of the "expected-foo {{}}".
  const char *ExpectedStart, *ExpectedEnd = nullptr;

  /// Additional information about the expectation.
  struct {
    Expectation::Kind Kind;
    Expectation::Scope Scope;
  } Info;

  /// The raw input buffer for the message text, the part in the {{...}}
  StringRef MessageRange;

public:
  Expectation(const char *estart, const char *eend, Expectation::Kind k,
              Expectation::Scope f, StringRef r)
      : ExpectedStart(estart), ExpectedEnd(eend), Info{k, f}, MessageRange(r) {
        assert(ExpectedStart <= MessageRange.data() &&
               "Message range appears before expected start!");
        assert(MessageRange.data()+MessageRange.size() <= ExpectedEnd &&
               "Message range extends beyond expected end!");
      }

  bool isCascading() const {
    return Info.Scope == Expectation::Scope::Cascading;
  }
};

/// An \c Obligation represents a compiler-provided entry in the set of
/// dependencies for a given source file. Similar to an \c Expectation an
/// \c Obligation contains a name, information about its kind and flavor, and an
/// extra source location that can be used to guide where diagnostics are
/// emitted. Unlike an \c Expectation, it provides an extra piece of state that
/// represents the obligation's "fulfillment status".
///
/// All \c Obligations begin in the \c Owed state. Once an obligation has been
/// paired with a matching \c Expectation, the obligation may then transition to
/// either \c Fulfilled if it has been satisfied completely, or \c Failed
/// otherwise. The verifier turns all unfulfilled obligations into errors.
struct Obligation {
  /// The state of an \c Obligation
  enum class State : uint8_t {
    /// The \c Obligation is owed and has not been paired with a corresponding
    /// \c Expectation.
    Owed,
    /// The \c Obligation is fulfilled.
    Fulfilled,
    /// The \c Obligation was matched against an \c Expectation, but that
    /// expectation could not
    /// fulfill the obligation because additional requirements did not pass.
    Failed,
  };

  /// A token returned when an \c Obligation is fulfilled or failed. An \c
  /// Obligation is the only type that may construct fulfillment tokens.
  ///
  /// \c FullfillmentToken prevents misuse of the \c Obligation
  /// structure by requiring its state to be changed along all program paths.
  struct FullfillmentToken {
    friend Obligation;

  private:
    FullfillmentToken() = default;
  };

  /// An \c Obligation::Key is a reduced set of the common data contained in an
  /// \c Obligation and an \c Expectation.
  ///
  /// This provides a way to use a value of either type to index into an  \c
  /// ObligationMap.
  struct Key {
    StringRef Name;
    Expectation::Kind Kind;

  public:
    Key() = delete;

  public:
    static Key forNegative(StringRef name) {
      return Key{name, Expectation::Kind::Negative};
    }

    static Key forProvides(StringRef name) {
      return Key{name, Expectation::Kind::Provides};
    }

    static Key forDynamicMember(StringRef name) {
      return Key{name, Expectation::Kind::DynamicMember};
    }

    static Key forPotentialMember(StringRef name) {
      return Key{name, Expectation::Kind::PotentialMember};
    }

    static Key forMember(StringRef name) {
      return Key{name, Expectation::Kind::Member};
    }

    static Key forExpectation(const Expectation &E) {
      return Key{E.MessageRange, E.Info.Kind};
    }

  public:
    struct Info {
      static inline Obligation::Key getEmptyKey() {
        return Obligation::Key{llvm::DenseMapInfo<StringRef>::getEmptyKey(),
                               static_cast<Expectation::Kind>(~0)};
      }
      static inline Obligation::Key getTombstoneKey() {
        return Obligation::Key{llvm::DenseMapInfo<StringRef>::getTombstoneKey(),
                               static_cast<Expectation::Kind>(~0U - 1)};
      }
      static unsigned getHashValue(const Obligation::Key &Val) {
        return llvm::hash_combine(Val.Name, Val.Kind);
      }
      static bool isEqual(const Obligation::Key &LHS,
                          const Obligation::Key &RHS) {
        return LHS.Name == RHS.Name && LHS.Kind == RHS.Kind;
      }
    };
  };

private:
  StringRef name;
  std::pair<Expectation::Kind, Expectation::Scope> info;
  State state;

public:
  Obligation(StringRef name, Expectation::Kind k, Expectation::Scope f)
      : name(name), info{k, f}, state(State::Owed) {
    assert(k != Expectation::Kind::Negative &&
           "Cannot form negative obligation!");
  }

  Expectation::Kind getKind() const { return info.first; }
  Expectation::Scope getScope() const { return info.second; }

  StringRef getName() const { return name; }
  bool getCascades() const {
    return info.second == Expectation::Scope::Cascading;
  }
  StringRef describeCascade() const {
    switch (info.second) {
    case Expectation::Scope::None:
      llvm_unreachable("Cannot describe obligation with no cascade info");
    case Expectation::Scope::Private:
      return "non-cascading";
    case Expectation::Scope::Cascading:
      return "cascading";
    }
  }

public:
  bool isOwed() const { return state == State::Owed; }
  FullfillmentToken fullfill() {
    assert(state == State::Owed &&
           "Cannot fulfill an obligation more than once!");
    state = State::Fulfilled;
    return FullfillmentToken{};
  }
  FullfillmentToken fail() {
    assert(state == State::Owed &&
           "Cannot fail an obligation more than once!");
    state = State::Failed;
    return FullfillmentToken{};
  }
};

/// The \c DependencyVerifier implements routines to verify a set of \c
/// Expectations in a given source file meet and match a set of \c Obligations
/// in the referenced name trackers associated with that file.
class DependencyVerifier {
  SourceManager &SM;
  const DependencyTracker &DT;
  std::vector<llvm::SMDiagnostic> Errors = {};

private:
  using FixitKey = uint16_t;
  llvm::DenseMap<FixitKey, StringRef> FixitTable;

  static FixitKey getFixitKey(Expectation::Kind kind,
                              Expectation::Scope scope) {
    static_assert(std::is_same<std::underlying_type<Expectation::Kind>::type,
                               uint8_t>::value,
                  "underlying type exceeds packing requirements");
    static_assert(std::is_same<std::underlying_type<Expectation::Scope>::type,
                               uint8_t>::value,
                  "underlying type exceeds packing requirements");
    return static_cast<uint8_t>(kind) << 8 | static_cast<uint8_t>(scope);
  }

  StringRef renderObligationAsFixit(ASTContext &Ctx,
                                    const Obligation &o, StringRef key) const {
    auto entry = FixitTable.find(getFixitKey(o.getKind(), o.getScope()));
    assert(entry != FixitTable.end() && "Didn't correctly pack keys?");
    // expected-<scope>-<kind> {{<key>}}
    auto sel = entry->getSecond();
    return Ctx.AllocateCopy(("// " + sel + " {{" + key + "}}").str());
  }

public:
  explicit DependencyVerifier(SourceManager &SM, const DependencyTracker &DT)
      : SM(SM), DT(DT) {
    // Build a table that maps the info in the matrix of supported
    // expectations to their corresponding selector names.
#define MATRIX_ENTRY(EXPECTATION_SELECTOR, SCOPE, KIND)                        \
  FixitTable.insert(                                                           \
      {getFixitKey(Expectation::Kind::KIND, Expectation::Scope::SCOPE),        \
       EXPECTATION_SELECTOR});

    EXPECTATION_MATRIX

#undef MATRIX_ENTRY
  }

  bool verifyFile(const SourceFile *SF, bool applyFixits);

public:
  using ObligationMap = llvm::MapVector<
      Obligation::Key, Obligation,
      llvm::DenseMap<Obligation::Key, unsigned, Obligation::Key::Info>>;
  using NegativeExpectationMap = llvm::StringMap<Expectation>;

private:
  /// These routines return \c true on failure, \c false otherwise.
  bool parseExpectations(const SourceFile *SF,
                         std::vector<Expectation> &Expectations);
  bool constructObligations(const SourceFile *SF, ObligationMap &map);

  bool verifyObligations(const SourceFile *SF,
                         const std::vector<Expectation> &Exs,
                         ObligationMap &Obs,
                         NegativeExpectationMap &NegativeExpectations);

  bool verifyNegativeExpectations(ObligationMap &Obs,
                                  NegativeExpectationMap &Negs);

  bool diagnoseUnfulfilledObligations(const SourceFile *SF, ObligationMap &OM);

  void applyEmittedFixits(const SourceFile *SF);

private:
  /// Given an \c ObligationMap and an \c Expectation, attempt to identify a
  /// corresponding owed \c Obligation and verify it. If there is a matching
  /// obligation, the \p fulfill callback is given the obligation. Otherwise \p
  /// fail is called with the unmatched expectation value.
  void matchExpectationOrFail(
      ObligationMap &OM, const Expectation &expectation,
      llvm::function_ref<Obligation::FullfillmentToken(Obligation &)> fulfill,
      llvm::function_ref<void(const Expectation &)> fail) {
    auto entry = OM.find(Obligation::Key::forExpectation(expectation));
    if (entry == OM.end()) {
      return fail(expectation);
    } else {
      fulfill(entry->second);
    }
  }

  /// For each owed \c Obligation, call the provided callback with its
  /// relevant name data and the Obligation itself.
  void
  forEachOwedObligation(ObligationMap &OM,
                        llvm::function_ref<void(StringRef, Obligation &)> f) {
    for (auto &p : OM) {
      if (p.second.isOwed())
        f(p.first.Name, p.second);
    }
  }

private:
  StringRef copyDemangledTypeName(ASTContext &Ctx, StringRef str) {
    Demangle::DemangleOptions Opts;
    Opts.ShowPrivateDiscriminators = false;
    Opts.DisplayModuleNames = true;
    return Ctx.AllocateCopy(Demangle::demangleTypeAsString(str, Opts));
  }

private:
  template <typename... Ts>
  inline auto addFormattedDiagnostic(const Expectation &dep, const char *Fmt,
                                     Ts &&... Vals) {
    return addFormattedDiagnostic(dep.MessageRange.begin(), Fmt,
                                  std::forward<Ts>(Vals)...);
  }

  template <typename... Ts>
  inline auto addFormattedDiagnostic(const char *Loc, const char *Fmt,
                                     Ts &&... Vals) {
    auto loc = SourceLoc(llvm::SMLoc::getFromPointer(Loc));
    auto diag =
        SM.GetMessage(loc, llvm::SourceMgr::DK_Error,
                      llvm::formatv(Fmt, std::forward<Ts>(Vals)...), {}, {});
    Errors.push_back(diag);
  }

  void addError(const char *Loc, const Twine &Msg) {
    auto loc = SourceLoc(llvm::SMLoc::getFromPointer(Loc));
    auto diag = SM.GetMessage(loc, llvm::SourceMgr::DK_Error, Msg, {}, {});
    Errors.push_back(diag);
  };

  void addNoteWithFixits(const char *Loc, const Twine &Msg,
                         ArrayRef<llvm::SMFixIt> FixIts = {}) {
    auto loc = SourceLoc(llvm::SMLoc::getFromPointer(Loc));
    auto diag = SM.GetMessage(loc, llvm::SourceMgr::DK_Note, Msg, {}, FixIts);
    Errors.push_back(diag);
  }
};
} // end anonymous namespace

bool DependencyVerifier::parseExpectations(
    const SourceFile *SF, std::vector<Expectation> &Expectations) {
  const auto MaybeBufferID = SF->getBufferID();
  if (!MaybeBufferID) {
    llvm::errs() << "source file has no buffer: " << SF->getFilename();
    return true;
  }

  const auto BufferID = MaybeBufferID.getValue();
  const CharSourceRange EntireRange = SM.getRangeForBuffer(BufferID);
  const StringRef InputFile = SM.extractText(EntireRange);

  for (size_t Match = InputFile.find("expected-"); Match != StringRef::npos;
       Match = InputFile.find("expected-", Match + 1)) {
    StringRef MatchStart = InputFile.substr(Match);
    const char *DiagnosticLoc = MatchStart.data();

    Expectation::Kind ExpectedKind;
    Expectation::Scope ExpectedScope;
    {
#define MATRIX_ENTRY(EXPECTATION_SELECTOR, SCOPE, KIND)                        \
  .StartsWith(EXPECTATION_SELECTOR, [&]() {                                    \
    ExpectedKind = Expectation::Kind::KIND;                                    \
    ExpectedScope = Expectation::Scope::SCOPE;                                 \
    MatchStart = MatchStart.substr(strlen(EXPECTATION_SELECTOR));              \
  })

      // clang-format off
        llvm::StringSwitch<llvm::function_ref<void(void)>>{MatchStart}
          EXPECTATION_MATRIX
          .Default([]() {})();
      // clang-format on
#undef MATRIX_ENTRY
    }

    // Skip any whitespace before the {{.
    MatchStart = MatchStart.substr(MatchStart.find_first_not_of(" \t"));

    const size_t TextStartIdx = MatchStart.find("{{");
    if (TextStartIdx == StringRef::npos) {
      addError(MatchStart.data(), "expected {{ in expectation");
      continue;
    }

    const size_t End = MatchStart.find("}}");
    if (End == StringRef::npos) {
      addError(MatchStart.data(),
               "didn't find '}}' to match '{{' in expectation");
      continue;
    }

    // Check if the next expectation should be in the same line.
    StringRef AfterEnd = MatchStart.substr(End + strlen("}}"));
    AfterEnd = AfterEnd.substr(AfterEnd.find_first_not_of(" \t"));
    const char *ExpectedEnd = AfterEnd.data();


    // Strip out the trailing whitespace.
    while (isspace(ExpectedEnd[-1]))
      --ExpectedEnd;

    Expectations.emplace_back(DiagnosticLoc, ExpectedEnd,
                              ExpectedKind, ExpectedScope,
                              MatchStart.slice(2, End));
  }
  return false;
}

bool DependencyVerifier::constructObligations(const SourceFile *SF,
                                              ObligationMap &Obligations) {
  auto *tracker = SF->getReferencedNameTracker();
  assert(tracker && "Constructed source file without referenced name tracker!");

  auto &Ctx = SF->getASTContext();
  tracker->enumerateAllUses(
      /*includeIntrafileDeps*/ true, DT,
      [&](const fine_grained_dependencies::NodeKind kind, StringRef context,
          StringRef name, const bool isCascadingUse) {
        using NodeKind = fine_grained_dependencies::NodeKind;
        switch (kind) {
        case NodeKind::externalDepend:
          // We only care about the referenced name trackers for now. The set of
          // external dependencies is often quite a large subset of the SDK.
          return;
        case NodeKind::nominal:
          // Nominals duplicate member entries. We care about the member itself.
          return;
        case NodeKind::potentialMember: {
          auto key = copyDemangledTypeName(Ctx, context);
          Obligations.insert({Obligation::Key::forPotentialMember(key),
                              {name, Expectation::Kind::PotentialMember,
                               isCascadingUse ? Expectation::Scope::Cascading
                                              : Expectation::Scope::Private}});
        }
          break;
        case NodeKind::member: {
          auto demContext = copyDemangledTypeName(Ctx, context);
          auto key = Ctx.AllocateCopy((demContext + "." + name).str());
          Obligations.insert({Obligation::Key::forMember(key),
                              {context, Expectation::Kind::Member,
                               isCascadingUse ? Expectation::Scope::Cascading
                                              : Expectation::Scope::Private}});
        }
          break;
        case NodeKind::dynamicLookup:
          Obligations.insert({Obligation::Key::forDynamicMember(name),
                              {context, Expectation::Kind::DynamicMember,
                               isCascadingUse ? Expectation::Scope::Cascading
                                              : Expectation::Scope::Private}});
          break;
        case NodeKind::topLevel:
        case NodeKind::sourceFileProvide:
          Obligations.insert({Obligation::Key::forProvides(name),
                              {name, Expectation::Kind::Provides,
                               Expectation::Scope::None}});
          break;
        case NodeKind::kindCount:
          llvm_unreachable("Given count node?");
        }
      });

  return false;
}

bool DependencyVerifier::verifyObligations(
    const SourceFile *SF, const std::vector<Expectation> &ExpectedDependencies,
    ObligationMap &OM, llvm::StringMap<Expectation> &NegativeExpectations) {
  auto *tracker = SF->getReferencedNameTracker();
  assert(tracker && "Constructed source file without referenced name tracker!");

  for (auto &expectation : ExpectedDependencies) {
    const bool wantsCascade = expectation.isCascading();
    switch (expectation.Info.Kind) {
    case Expectation::Kind::Negative:
      // We'll verify negative expectations separately.
      NegativeExpectations.insert({expectation.MessageRange, expectation});
      break;
    case Expectation::Kind::Member:
      matchExpectationOrFail(
          OM, expectation,
          [&](Obligation &p) {
            const auto haveCascade = p.getCascades();
            if (haveCascade != wantsCascade) {
              addFormattedDiagnostic(
                  expectation,
                  "expected {0} dependency; found {1} dependency instead",
                  wantsCascade ? "cascading" : "non-cascading",
                  haveCascade ? "cascading" : "non-cascading");
              return p.fail();
            }

            return p.fullfill();
          },
          [this](const Expectation &e) {
            addFormattedDiagnostic(
                e, "expected member dependency does not exist: {0}",
                e.MessageRange);
          });
      break;
    case Expectation::Kind::PotentialMember:
      matchExpectationOrFail(
          OM, expectation,
          [&](Obligation &p) {
            assert(p.getName().empty());
            const auto haveCascade = p.getCascades();
            if (haveCascade != wantsCascade) {
              addFormattedDiagnostic(
                  expectation,
                  "expected {0} potential member dependency; found {1} "
                  "potential member dependency instead",
                  wantsCascade ? "cascading" : "non-cascading",
                  haveCascade ? "cascading" : "non-cascading");
              return p.fail();
            }

            return p.fullfill();
          },
          [this](const Expectation &e) {
            addFormattedDiagnostic(
                e, "expected potential member dependency does not exist: {0}",
                e.MessageRange);
          });
      break;
    case Expectation::Kind::Provides:
      matchExpectationOrFail(
          OM, expectation, [](Obligation &O) { return O.fullfill(); },
          [this](const Expectation &e) {
            addFormattedDiagnostic(
                e, "expected provided dependency does not exist: {0}",
                e.MessageRange);
          });
      break;
    case Expectation::Kind::DynamicMember:
      matchExpectationOrFail(
          OM, expectation, [](Obligation &O) { return O.fullfill(); },
          [this](const Expectation &e) {
            addFormattedDiagnostic(
                e, "expected dynamic member dependency does not exist: {0}",
                e.MessageRange);
          });
      break;
    }
  }

  return false;
}

bool DependencyVerifier::verifyNegativeExpectations(
    ObligationMap &Obligations, NegativeExpectationMap &NegativeExpectations) {
  forEachOwedObligation(Obligations, [&](StringRef key, Obligation &p) {
    auto entry = NegativeExpectations.find(key);
    if (entry == NegativeExpectations.end()) {
      return;
    }

    auto &expectation = entry->second;
    addFormattedDiagnostic(expectation, "unexpected dependency exists: {0}",
                           expectation.MessageRange);
    p.fail();
  });
  return false;
}

bool DependencyVerifier::diagnoseUnfulfilledObligations(
    const SourceFile *SF, ObligationMap &Obligations) {
  CharSourceRange EntireRange = SM.getRangeForBuffer(*SF->getBufferID());
  StringRef InputFile = SM.extractText(EntireRange);
  auto &Ctx = SF->getASTContext();
  forEachOwedObligation(Obligations, [&](StringRef key, Obligation &p) {
    // HACK: Diagnosing the end of the buffer will print a carat pointing
    // at the file path, but not print any of the buffer's contents, which
    // might be misleading.
    const char *Loc = InputFile.end();
    switch (p.getKind()) {
    case Expectation::Kind::Negative:
      llvm_unreachable("Obligations may not be negative; only Expectations!");
    case Expectation::Kind::Member:
      addFormattedDiagnostic(Loc, "unexpected {0} dependency: {1}",
                             p.describeCascade(), key);
      addNoteWithFixits(Loc, "expect a member dependency",
                        {
                            llvm::SMFixIt(llvm::SMLoc::getFromPointer(Loc),
                                          renderObligationAsFixit(Ctx, p, key)),
                        });
      break;
    case Expectation::Kind::DynamicMember:
      addFormattedDiagnostic(Loc,
                             "unexpected {0} dynamic member dependency: {1}",
                             p.describeCascade(), p.getName());
      addNoteWithFixits(Loc, "expect a dynamic member dependency",
                        {
                            llvm::SMFixIt(llvm::SMLoc::getFromPointer(Loc),
                                          renderObligationAsFixit(Ctx, p, key)),
                        });
      break;
    case Expectation::Kind::PotentialMember:
      addFormattedDiagnostic(Loc,
                             "unexpected {0} potential member dependency: {1}",
                             p.describeCascade(), key);
      addNoteWithFixits(Loc, "expect a potential member",
                        {
                            llvm::SMFixIt(llvm::SMLoc::getFromPointer(Loc),
                                          renderObligationAsFixit(Ctx, p, key)),
                        });
      break;
    case Expectation::Kind::Provides:
      addFormattedDiagnostic(Loc, "unexpected provided entity: {0}",
                             p.getName());
      addNoteWithFixits(Loc, "expect a provide",
                        {
                            llvm::SMFixIt(llvm::SMLoc::getFromPointer(Loc),
                                          renderObligationAsFixit(Ctx, p, key)),
                        });
      break;
    }
  });

  return false;
}

void DependencyVerifier::applyEmittedFixits(const SourceFile *SF) {
  // Walk the list of diagnostics, pulling out any fixits into an array of just
  // them.
  SmallVector<llvm::SMFixIt, 4> FixIts;
  for (auto &diag : Errors)
    FixIts.append(diag.getFixIts().begin(), diag.getFixIts().end());

  // If we have no fixits to apply, avoid touching the file.
  if (FixIts.empty())
    return;

  // Sort the fixits by their start location.
  std::sort(FixIts.begin(), FixIts.end(),
            [&](const llvm::SMFixIt &lhs, const llvm::SMFixIt &rhs) -> bool {
              return lhs.getRange().Start.getPointer()
                   < rhs.getRange().Start.getPointer();
            });

  // Get the contents of the original source file.
  auto memBuffer = SM.getLLVMSourceMgr().getMemoryBuffer(*SF->getBufferID());
  auto bufferRange = memBuffer->getBuffer();

  // Apply the fixes, building up a new buffer as an std::string.
  const char *LastPos = bufferRange.begin();
  std::string Result;

  for (auto &fix : FixIts) {
    // We cannot handle overlapping fixits, so assert that they don't happen.
    assert(LastPos <= fix.getRange().Start.getPointer() &&
           "Cannot handle overlapping fixits");

    // Keep anything from the last spot we've checked to the start of the fixit.
    Result.append(LastPos, fix.getRange().Start.getPointer());

    // Replace the content covered by the fixit with the replacement text.
    Result.append(fix.getText().begin(), fix.getText().end());

    // Append a newline.
    Result.append("\n");

    // Next character to consider is at the end of the fixit.
    LastPos = fix.getRange().End.getPointer();
  }

  // Retain the end of the file.
  Result.append(LastPos, bufferRange.end());

  std::error_code error;
  llvm::raw_fd_ostream outs(memBuffer->getBufferIdentifier(), error,
                            llvm::sys::fs::OpenFlags::F_None);
  if (!error)
    outs << Result;
}


bool DependencyVerifier::verifyFile(const SourceFile *SF, bool applyFixits) {
  std::vector<Expectation> ExpectedDependencies;
  if (parseExpectations(SF, ExpectedDependencies)) {
    return true;
  }

  ObligationMap Obligations;
  if (constructObligations(SF, Obligations)) {
    return true;
  }

  NegativeExpectationMap Negatives;
  if (verifyObligations(SF, ExpectedDependencies, Obligations, Negatives)) {
    return true;
  }

  if (verifyNegativeExpectations(Obligations, Negatives)) {
    return true;
  }

  if (diagnoseUnfulfilledObligations(SF, Obligations)) {
    return true;
  }

  if (applyFixits) {
    applyEmittedFixits(SF);
  }

  // Sort the diagnostics by location so we get a stable ordering.
  std::sort(Errors.begin(), Errors.end(),
            [&](const llvm::SMDiagnostic &lhs,
                const llvm::SMDiagnostic &rhs) -> bool {
              return lhs.getLoc().getPointer() < rhs.getLoc().getPointer();
            });

  for (auto Err : Errors)
    SM.getLLVMSourceMgr().PrintMessage(llvm::errs(), Err);

  return !Errors.empty();
}

//===----------------------------------------------------------------------===//
// MARK: Main entrypoints
//===----------------------------------------------------------------------===//

bool swift::verifyDependencies(SourceManager &SM, const DependencyTracker &DT,
                               bool autoApplyFixits,
                               ArrayRef<FileUnit *> SFs) {
  bool HadError = false;
  DependencyVerifier Verifier{SM, DT};
  for (const auto *FU : SFs) {
    if (const auto *SF = dyn_cast<SourceFile>(FU))
      HadError |= Verifier.verifyFile(SF, autoApplyFixits);
  }
  return HadError;
}

bool swift::verifyDependencies(SourceManager &SM, const DependencyTracker &DT,
                               bool autoApplyFixits,
                               ArrayRef<SourceFile *> SFs) {
  bool HadError = false;
  DependencyVerifier Verifier{SM, DT};
  for (const auto *SF : SFs) {
    HadError |= Verifier.verifyFile(SF, autoApplyFixits);
  }
  return HadError;
}

#undef EXPECTATION_MATRIX
