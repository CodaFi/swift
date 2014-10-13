//===--- Parser.cpp - ReST parser -----------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2015 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "swift/ReST/Parser.h"
#include "Detail.h"
#include "swift/ReST/LineList.h"
#include "swift/ReST/XMLUtils.h"
#include "llvm/Support/ErrorHandling.h"
#include "clang/Basic/CharInfo.h"

using namespace llvm;
using namespace rest;
using namespace llvm::rest::detail;

using namespace clang;

namespace {
struct ParsedEnumerator {
  LineKind Kind;
  unsigned EnumeratorBytes;
  unsigned Value;
};
} // unnamed namespace

static bool startsWithWhitespaceOrEOL(StringRef Text,
                                      unsigned &WhitespaceBytes) {
  if (Text.empty()) {
    WhitespaceBytes = 0;
    return true;
  }

  if (!isReSTWhitespace(Text.front()))
    return false;

  for (unsigned i = 1, e = Text.size(); i != e; ++i) {
    if (!isReSTWhitespace(Text[i])) {
      WhitespaceBytes = i;
      return true;
    }
  }
  WhitespaceBytes = Text.size();
  return true;
}

/// Returns true on success.
static bool tryParseEnumerator(StringRef Text, ParsedEnumerator &PE) {
  if (Text.empty()) {
    return false;
  }

  if (Text[0] == '#') {
    PE = { LineKind::EnumeratedListAuto, 1, 0 };
    return true;
  }

  if (isDigit(Text[0])) {
    unsigned EnumeratorBytes = 1;
    for (unsigned e = Text.size(); EnumeratorBytes != e; ++EnumeratorBytes) {
      if (!isDigit(Text[EnumeratorBytes]))
        break;
    }
    unsigned Value;
    if (Text.substr(0, EnumeratorBytes).getAsInteger(10, Value)) {
      // FIXME: we should produce a diagnostic if there was an overflow.
      return false;
    }
    PE = { LineKind::EnumeratedListArabic, EnumeratorBytes, Value };
    return true;
  }

  // FIXME: implement other enumerator kinds.

  return false;
}

static LineClassification tryParseEnumeratorWithFormatting(StringRef Text) {
  // [ReST/Syntax Details/Body Elements/Enumerated Lists]
  // Quote:
  //    The following formatting types are recognized:
  //
  //    * suffixed with a period: "1.", "A.", "a.", "I.", "i.".
  //    * surrounded by parentheses: "(1)", "(A)", "(a)", "(I)", "(i)".
  //    * suffixed with a right-parenthesis: "1)", "A)", "a)", "I)", "i)".
  ParsedEnumerator PE;
  if (Text.startswith("(")) {
    if (!tryParseEnumerator(Text.drop_front(1), PE))
      return LineClassification::makeUnknown();

    StringRef WithoutEnumeratorValue = Text.drop_front(1 + PE.EnumeratorBytes);
    if (!WithoutEnumeratorValue.startswith(")"))
      return LineClassification::makeUnknown();

    StringRef WithoutEnumerator = WithoutEnumeratorValue.drop_front(1);
    unsigned WhitespaceBytes;
    if (startsWithWhitespaceOrEOL(WithoutEnumerator, WhitespaceBytes)) {
      bool HasTextAfterEnumerator = WhitespaceBytes != WithoutEnumerator.size();
      return LineClassification::makeEnumerated(
          PE.Kind, EnumeratorStyleKind::SurroundedByParens,
          HasTextAfterEnumerator, 1 + PE.EnumeratorBytes + 1 + WhitespaceBytes);
    }

    return LineClassification::makeUnknown();
  }
  if (tryParseEnumerator(Text, PE)) {
    StringRef WithoutEnumeratorValue = Text.drop_front(PE.EnumeratorBytes);
    bool IsDotAfter = WithoutEnumeratorValue.startswith(".");
    bool IsParenAfter = WithoutEnumeratorValue.startswith(")");
    if (!IsDotAfter && !IsParenAfter)
      return LineClassification::makeUnknown();

    StringRef WithoutEnumerator = WithoutEnumeratorValue.drop_front(1);
    unsigned WhitespaceBytes;
    if (startsWithWhitespaceOrEOL(WithoutEnumerator, WhitespaceBytes)) {
      bool HasTextAfterEnumerator = WhitespaceBytes != WithoutEnumerator.size();
      return LineClassification::makeEnumerated(
          PE.Kind, IsDotAfter ? EnumeratorStyleKind::DotAfter
                              : EnumeratorStyleKind::ParenAfter,
          HasTextAfterEnumerator, PE.EnumeratorBytes + 1 + WhitespaceBytes);
    }

    return LineClassification::makeUnknown();
  }
  return LineClassification::makeUnknown();
}

llvm::rest::detail::LineClassification
llvm::rest::detail::classifyLine(const Line &L) {
  StringRef Text = L.Text.drop_front(L.FirstTextByte);

  if (Text.empty())
    return LineClassification::makeBlank();

  // [ReST/Syntax Details/Body Elements/Field Lists]
  // Quote:
  //    A field name may consist of any characters, but colons (":") inside of
  //    field names must be escaped with a backslash.  Inline markup is parsed
  //    in field names.
  //    [...]
  //    The field marker is followed by whitespace and the field body.
  //
  // The initial check is very lightweight here (just look if there is a
  // colon at the beginning), so handle this case first.
  if (Text.startswith(":") && Text.size() >= 3 && Text[1] != ':') {
    // This might be a field name.  This is a field list if the line contains a
    // colon that is not escaped, and the field name is not empty.
    // REST-FIXME: clarify that the field name can not be empty.
    unsigned i = 1;
    if (Text[i] == '\\') {
      // Skip the next character, it is escaped.
      i += 2;
    }
    for (unsigned e = Text.size(); i != e; ++i) {
      if (Text[i] == ':') {
        unsigned FieldNameBytes = i - 1;
        // Check that the second colon is followed by end of line or
        // whitespace.
        StringRef WithoutFieldMarker = Text.drop_front(i + 1);
        unsigned WhitespaceBytes;
        if (startsWithWhitespaceOrEOL(WithoutFieldMarker, WhitespaceBytes))
          return LineClassification::makeFieldList(FieldNameBytes,
                                                   i + 1 + WhitespaceBytes);
        else
          break;
      }
      if (Text[i] == '\\') {
        // Skip the next character, it is escaped.
        ++i;
        if (i == e)
          break;
      }
    }
  }

  // [ReST/Syntax Details/Body Elements/Bullet Lists]
  // ReST allows the following characters to start a bulleted list:
  // U+002A ASTERISK
  // U+002B PLUS SIGN
  // U+002D HYPHEN-MINUS
  // U+2022 BULLET
  // U+2023 TRIANGULAR BULLET
  // U+2043 HYPHEN BULLET
  //
  // Note: the following code tries to avoid using heavy machinery to decode
  // UTF-8.
  LineKind Kind = LineKind::Unknown;
  unsigned BulletBytes = 0;
  if (Text.startswith("*")) {
    Kind = LineKind::BulletListAsterisk;
    BulletBytes = 1;
  } else if (Text.startswith("+")) {
    Kind = LineKind::BulletListPlus;
    BulletBytes = 1;
  } else if (Text.startswith("-")) {
    Kind = LineKind::BulletListHyphenMinus;
    BulletBytes = 1;
  } else if (Text.startswith("\u2022")) {
    Kind = LineKind::BulletListBullet;
    BulletBytes = 3;
  } else if (Text.startswith("\u2023")) {
    Kind = LineKind::BulletListTriangularBullet;
    BulletBytes = 3;
  } else if (Text.startswith("\u2043")) {
    Kind = LineKind::BulletListHyphenBullet;
    BulletBytes = 3;
  }
  if (Kind != LineKind::Unknown) {
    // We have a bullet.  This is the initial line of a bullet list if the
    // bullet is at the end of the line or is followed by whitespace.
    StringRef WithoutBullet = Text.drop_front(BulletBytes);
    unsigned WhitespaceBytes;
    if (startsWithWhitespaceOrEOL(WithoutBullet, WhitespaceBytes))
      return LineClassification::makeBullet(Kind,
                                            BulletBytes + WhitespaceBytes);

    Kind = LineKind::Unknown;
  }

  {
    LineClassification MaybeEnumerator = tryParseEnumeratorWithFormatting(Text);
    if (MaybeEnumerator.Kind != LineKind::Unknown)
      return MaybeEnumerator;
  }

  // [ReST/Syntax Details/Body Elements/Option Lists]
  // FIXME: implement later.

  return LineClassification::makeUnknown();
}

static bool isDefinitionList(LineListRef LL) {
  assert(LL.size() != 0);
  if (LL.size() < 2)
    return false;

  if (LL[0].getClassification().Kind != LineKind::Unknown)
    return false;

  if (LL[1].getClassification().Kind == LineKind::Blank)
    return false;

  return LL[0].FirstTextCol < LL[1].FirstTextCol;
}

static bool isEnumeratedListItem(LineListRef LL) {
  Optional<bool> IsListItem;
  if (LL.isNextLineBlank(0))
    IsListItem = true;

  if (!IsListItem.hasValue() && LL[1].FirstTextCol == LL[0].FirstTextCol) {
    bool IsNEELEL = isEnumerated(LL[1].getClassification().Kind) &&
                    LL[1].getClassification().hasTextAfterEnumerator();
    if (IsNEELEL) {
      if (LL[0].getClassification().Kind == LL[1].getClassification().Kind &&
          LL[0].getClassification().getEnumeratorStyle() ==
              LL[1].getClassification().getEnumeratorStyle())
        IsListItem = true;
      // FIXME: check numeric value of enumerator.
    }
    if (!IsListItem.hasValue())
      IsListItem = false;
  }

  if (!IsListItem.hasValue() && LL[1].FirstTextCol < LL[0].FirstTextCol)
    IsListItem = true;

  if (!IsListItem.hasValue() &&
      (LL[1].FirstTextCol <
       LL[0].FirstTextCol +
           LL[0].getClassification().getEnumeratorAndWhitespaceCols())) {
    // Next line does not have enough indentation, so this line is not a
    // list item.
    IsListItem = false;
  }

  if (!IsListItem.hasValue())
    IsListItem = true;
  return IsListItem.getValue();
}

namespace {
class Parser {
  ReSTContext &Context;

  std::pair<ReSTASTNode *, unsigned> parseParagraph(LineListRef LL,
                                                    ColumnNum BaseIndentation);
  std::pair<ReSTASTNode *, unsigned> parseBulletList(LineListRef LL);
  std::pair<ReSTASTNode *, unsigned> parseEnumeratedList(LineListRef LL);
  std::pair<ReSTASTNode *, unsigned> parseDefinitionList(LineListRef LL);
  std::pair<ReSTASTNode *, unsigned> parseFieldList(LineListRef LL);

  /// This might parse an idnented literal block or a block quote.
  std::pair<ReSTASTNode *, unsigned>
  parseUnresolvedIndentedBlock(LineListRef LL);

  unsigned parseLevelImpl(LineListRef LL,
                          SmallVectorImpl<ReSTASTNode *> &Children,
                          ColumnNum BaseIndentation,
                          ColumnNum LeftMarginIndentation,
                          bool IgnoreIndentationOfTheFirstLine,
                          ColumnNum *MinIndentation);

  unsigned parseLevel(LineListRef LL, SmallVectorImpl<ReSTASTNode *> &Children);

  unsigned parseLevelFromListItem(LineListRef LL,
                                  SmallVectorImpl<ReSTASTNode *> &Children,
                                  ColumnNum BulletIndentation);

  TextAndInline *parseInlineContent(LineListRef LL);

public:
  Parser(ReSTContext &Context) : Context(Context) {}

  Document *parseDocument(LineListRef LL);
};
} // unnamed namespace

std::pair<ReSTASTNode *, unsigned>
Parser::parseParagraph(LineListRef LL, ColumnNum BaseIndentation) {
  assert(LL.size() != 0);
  assert(LL[0].getClassification().Kind == LineKind::Unknown ||
         isEnumerated(LL[0].getClassification().Kind));
  unsigned i = 0;
  for (unsigned e = LL.size(); i != e; ++i) {
    if (LL[i].getClassification().Kind != LineKind::Blank &&
        !(i == 0 && LL.isFirstLineTruncated())) {
      if (LL[i].FirstTextCol > BaseIndentation) {
        // Indent.
        assert(i != 1 && "can not be a definition list");
        // Unexpected indent.  Paragraph ends here, the next line starts a new
        // block.
        break;
      } else if (LL[i].FirstTextCol < BaseIndentation) {
        // Unexpected unindent.  Paragraph ends here, the next line should
        // match up with something else we parsed previously.
        break;
      }
    }

    switch (LL[i].getClassification().Kind) {
    case LineKind::Unknown:
      continue;

    case LineKind::Blank: {
      // Paragraph ends at a blank line.
      auto *P = new (Context)
          Paragraph(parseInlineContent(LL.subList(0, i)));
      return { P, i };
    }

    case LineKind::BulletListAsterisk:
    case LineKind::BulletListPlus:
    case LineKind::BulletListHyphenMinus:
    case LineKind::BulletListBullet:
    case LineKind::BulletListTriangularBullet:
    case LineKind::BulletListHyphenBullet:
      assert(!LL.isPreviousLineBlank(i));
      continue;

    case LineKind::EnumeratedListArabic:
    case LineKind::EnumeratedListUppercaseAlphabet:
    case LineKind::EnumeratedListLowercaseAlphabet:
    case LineKind::EnumeratedListUppercaseRoman:
    case LineKind::EnumeratedListLowercaseRoman:
    case LineKind::EnumeratedListUppercaseAmbiguous:
    case LineKind::EnumeratedListLowercaseAmbiguous:
    case LineKind::EnumeratedListAuto:
      assert(i == 0 || !LL.isPreviousLineBlank(i));
      continue;

    case LineKind::FieldList:
      assert(!LL.isPreviousLineBlank(i));
      continue;
    }
  }
  auto *P =
      new (Context) Paragraph(parseInlineContent(LL.subList(0, i)));
  assert(i != 0);
  return { P, i };
}

std::pair<ReSTASTNode *, unsigned> Parser::parseBulletList(LineListRef LL) {
  SmallVector<BulletList::ListItemInfo, 4> ItemInfos;
  SmallVector<ReSTASTNode *, 4> ItemChildren;

  auto Kind = LL[0].getClassification().Kind;
  ColumnNum BulletIndentation = LL[0].FirstTextCol;

  unsigned i = 0;
  for (unsigned e = LL.size(); i != e;) {
    // At the beginning of every iteration, we are either at the beginning of
    // the next list item or at the end of the list.
    if (LL[i].getClassification().Kind != LineKind::Blank) {
      if (LL[i].FirstTextCol > BulletIndentation) {
        // Indent.  Note that this indent is not large enough to line up with
        // the previous item's children.  The list ends here, the next line
        // will start a block quote, but at the same nesting level as this
        // list.
        break;
      } else if (LL[i].FirstTextCol < BulletIndentation) {
        // Unexpected unindent.  List ends here, the next line should match up
        // with something else we parsed previously.
        break;
      }
    }

    bool IsEndOfList = false;
    switch (LL[i].getClassification().Kind) {
    case LineKind::Unknown:
      IsEndOfList = true;
      break;

    case LineKind::Blank:
      // Skip blank lines?
      i++;
      continue;

    case LineKind::BulletListAsterisk:
    case LineKind::BulletListPlus:
    case LineKind::BulletListHyphenMinus:
    case LineKind::BulletListBullet:
    case LineKind::BulletListTriangularBullet:
    case LineKind::BulletListHyphenBullet:
      if (LL[i].getClassification().Kind != Kind)
        IsEndOfList = true;
      break;

    case LineKind::EnumeratedListArabic:
    case LineKind::EnumeratedListUppercaseAlphabet:
    case LineKind::EnumeratedListLowercaseAlphabet:
    case LineKind::EnumeratedListUppercaseRoman:
    case LineKind::EnumeratedListLowercaseRoman:
    case LineKind::EnumeratedListUppercaseAmbiguous:
    case LineKind::EnumeratedListLowercaseAmbiguous:
    case LineKind::EnumeratedListAuto:
      IsEndOfList = true;
      break;

    case LineKind::FieldList:
      IsEndOfList = true;
      break;
    }
    if (IsEndOfList)
      break;

    // If we got here, this is the start of a list item.
    auto SubLL = LL.dropFrontLines(i);
    SubLL.fromFirstLineDropFront(
        LL[i].getClassification().getBulletAndWhitespaceBytes());
    SmallVector<ReSTASTNode *, 4> CurrItemChildren;
    unsigned NumLines =
        parseLevelFromListItem(SubLL, CurrItemChildren, BulletIndentation);
    i += NumLines;
    ItemInfos.push_back({static_cast<unsigned>(ItemChildren.size()),
                         static_cast<unsigned>(CurrItemChildren.size())});
    ItemChildren.append(CurrItemChildren.begin(), CurrItemChildren.end());
  }

  auto *BL = BulletList::create(Context, ItemInfos, ItemChildren);
  return {BL, i};
}

std::pair<ReSTASTNode *, unsigned> Parser::parseEnumeratedList(LineListRef LL) {
  SmallVector<EnumeratedList::ListItemInfo, 4> ItemInfos;
  SmallVector<ReSTASTNode *, 4> ItemChildren;

  auto Kind = LL[0].getClassification().Kind;
  auto EnumeratorStyle = LL[0].getClassification().getEnumeratorStyle();
  ColumnNum EnumeratorIndentation = LL[0].FirstTextCol;

  unsigned i = 0;
  for (unsigned e = LL.size(); i != e;) {
    // Invariant: at the beginning of every iteration, we are either at the
    // beginning of the next list item or at the end of the list.

    if (LL[i].getClassification().Kind != LineKind::Blank) {
      if (LL[i].FirstTextCol > EnumeratorIndentation) {
        // Indent.  Note that this indent is not large enough to line up with
        // the previous item's children.  The list ends here, the next line
        // will start a block quote, but at the same nesting level as this
        // list.
        break;
      } else if (LL[i].FirstTextCol < EnumeratorIndentation) {
        // Unexpected unindent.  List ends here, the next line should match up
        // with something else we parsed previously.
        break;
      }
    }

    bool IsEndOfList = false;
    switch (LL[i].getClassification().Kind) {
    case LineKind::Unknown:
      IsEndOfList = true;
      break;

    case LineKind::Blank:
      // Skip blank lines?
      i++;
      continue;

    case LineKind::BulletListAsterisk:
    case LineKind::BulletListPlus:
    case LineKind::BulletListHyphenMinus:
    case LineKind::BulletListBullet:
    case LineKind::BulletListTriangularBullet:
    case LineKind::BulletListHyphenBullet:
      IsEndOfList = true;
      break;

    case LineKind::EnumeratedListArabic:
    case LineKind::EnumeratedListUppercaseAlphabet:
    case LineKind::EnumeratedListLowercaseAlphabet:
    case LineKind::EnumeratedListUppercaseRoman:
    case LineKind::EnumeratedListLowercaseRoman:
    case LineKind::EnumeratedListUppercaseAmbiguous:
    case LineKind::EnumeratedListLowercaseAmbiguous:
    case LineKind::EnumeratedListAuto:
      if (LL[i].getClassification().Kind != Kind ||
          LL[i].getClassification().getEnumeratorStyle() != EnumeratorStyle)
        IsEndOfList = true;
      if (!isEnumeratedListItem(LL.dropFrontLines(i)))
        IsEndOfList = true;
      break;

    case LineKind::FieldList:
      IsEndOfList = true;
      break;
    }
    if (IsEndOfList)
      break;

    // If we got here, this is the start of a list item.
    auto SubLL = LL.dropFrontLines(i);
    SubLL.fromFirstLineDropFront(
        LL[i].getClassification().getEnumeratorAndWhitespaceBytes());
    SmallVector<ReSTASTNode *, 4> CurrItemChildren;
    unsigned NumLines = parseLevel(SubLL, CurrItemChildren);
    i += NumLines;
    ItemInfos.push_back({ static_cast<unsigned>(ItemChildren.size()),
                          static_cast<unsigned>(CurrItemChildren.size()) });
    ItemChildren.append(CurrItemChildren.begin(), CurrItemChildren.end());
  }

  auto *EL = EnumeratedList::create(Context, ItemInfos, ItemChildren);
  return { EL, i };
}

std::pair<ReSTASTNode *, unsigned> Parser::parseDefinitionList(LineListRef LL) {
  assert(isDefinitionList(LL));

  ColumnNum TermIndentation = LL[0].FirstTextCol;

  SmallVector<DefinitionListItem *, 4> Children;

  unsigned i = 0;
  for (unsigned e = LL.size(); i != e;) {
    // Invariant: at the beginning of every iteration, we are either at the
    // beginning of the next list item or at the end of the list.

    if (LL[i].FirstTextCol < TermIndentation) {
      // Unindent.  Definition list ends here.
      break;
    }

    if (!isDefinitionList(LL.dropFrontLines(i)))
      break;

    // FIXME: parse the term line into term and classifiers.
    auto Term = parseInlineContent(LL.subList(i, 1));

    ColumnNum ItemBaseIndentation = LL[i + 1].FirstTextCol;
    SmallVector<ReSTASTNode *, 4> ItemChildren;
    unsigned NumLines = parseLevelImpl(
        LL.dropFrontLines(i + 1), ItemChildren, ItemBaseIndentation,
        TermIndentation + ColumnNum::make(1),
        /*IgnoreIndentationOfTheFirstLine=*/false, nullptr);
    Children.push_back(
        DefinitionListItem::create(Context, Term, {}, ItemChildren));
    i += 1 + NumLines;
  }

  auto *DL = DefinitionList::create(Context, Children);
  assert(i != 0);
  return { DL, i };
}

std::pair<ReSTASTNode *, unsigned> Parser::parseFieldList(LineListRef LL) {
  assert(LL[0].getClassification().Kind == LineKind::FieldList);

  ColumnNum FirstColonIndentation = LL[0].FirstTextCol;

  SmallVector<Field *, 4> Children;

  unsigned i = 0;
  for (unsigned e = LL.size(); i != e;) {
    // Invariant: at the beginning of every iteration, we are either at the
    // beginning of the next list item or at the end of the list.

    if (LL[i].FirstTextCol < FirstColonIndentation) {
      // Unindent.  Field list ends here.
      break;
    }

    if (LL[i].getClassification().Kind != LineKind::FieldList)
      break;

    LinePart FieldNameLinePart =
        LL.getLinePart(i, LL[i].FirstTextByte + 1,
                       LL[i].getClassification().getFieldNameBytes());
    // FIXME: parse inline markup?
    auto FieldNamePlainText = new (Context) PlainText(FieldNameLinePart);
    auto FieldName = TextAndInline::create(Context, {FieldNamePlainText});

    // [ReST/Syntax Details/Body Elements/Field Lists]
    // Quote:
    //     The first line after the field name marker determines the
    //     indentation of the field body.
    ColumnNum ItemBaseIndentation;
    if (i + 1 != e) {
      for (unsigned j = i + 1; j != e; ++j) {
        if (LL[j].getClassification().Kind != LineKind::Blank) {
          ItemBaseIndentation = LL[j].FirstTextCol;
          break;
        }
      }
    }
    // If the line that we found does not have more indentation than the colon,
    // then it is not related.  Set the desired base indentation to be larger
    // than that so that parseLevelImpl() below does not pick up this unrelated
    // line.
    if (ItemBaseIndentation <= FirstColonIndentation)
      ItemBaseIndentation = FirstColonIndentation + ColumnNum::make(1);

    SmallVector<ReSTASTNode *, 4> BodyChildren;
    auto SubLL = LL.dropFrontLines(i);
    SubLL.fromFirstLineDropFront(
        SubLL[0].getClassification().getFieldMarkerAndWhitespaceBytes());
    unsigned NumLines =
        parseLevelImpl(SubLL, BodyChildren, ItemBaseIndentation,
                       FirstColonIndentation + ColumnNum::make(1),
                       /*IgnoreIndentationOfTheFirstLine=*/true, nullptr);
    Children.push_back(Field::create(Context, FieldName, BodyChildren));
    i += NumLines;
  }

  auto *FL = FieldList::create(Context, Children);
  assert(i != 0);
  return {FL, i};
}

std::pair<ReSTASTNode *, unsigned>
Parser::parseUnresolvedIndentedBlock(LineListRef LL) {
  SmallVector<ReSTASTNode *, 4> Children;
  unsigned NumLines = parseLevel(LL, Children);

  auto *BQ = BlockQuote::create(Context, Children);
  assert(NumLines != 0);
  return { BQ, NumLines };
}

unsigned Parser::parseLevelImpl(LineListRef LL,
                                SmallVectorImpl<ReSTASTNode *> &Children,
                                ColumnNum BaseIndentation,
                                ColumnNum LeftMarginIndentation,
                                bool IgnoreIndentationOfTheFirstLine,
                                ColumnNum *MinIndentation) {
  assert(Children.size() == 0);
  if (LL.empty())
    return 0;

  unsigned i = 0;
  for (unsigned e = LL.size(); i != e;) {
    if (LL[i].getClassification().Kind != LineKind::Blank &&
        !(i == 0 && IgnoreIndentationOfTheFirstLine)) {
      if (LL[i].FirstTextCol > BaseIndentation) {
        // Indent.
        //
        // FIXME: parse a definition list or a block quote.
        ReSTASTNode *N;
        unsigned NumLines;
        std::tie(N, NumLines) =
            parseUnresolvedIndentedBlock(LL.dropFrontLines(i));
        Children.push_back(N);
        i += NumLines;
        continue;
      } else if (LL[i].FirstTextCol < LeftMarginIndentation) {
        // Unexpected unindent.  Current indentation level ends here, the next
        // line should match up with something else we parsed previously.
        break;
      } else if (LL[i].FirstTextCol < BaseIndentation) {
        auto *BQ = BlockQuote::create(Context, Children);
        Children.clear();
        Children.push_back(BQ);
        BaseIndentation = LL[i].FirstTextCol;
      }
    }

    switch (LL[i].getClassification().Kind) {
    case LineKind::Unknown: {
      auto SubLL = LL.dropFrontLines(i);
      ReSTASTNode *N;
      unsigned NumLines;
      if (isDefinitionList(SubLL) &&
          !(i == 0 && IgnoreIndentationOfTheFirstLine))
        std::tie(N, NumLines) = parseDefinitionList(SubLL);
      else
        std::tie(N, NumLines) = parseParagraph(SubLL, BaseIndentation);
      Children.push_back(N);
      i += NumLines;
      continue;
    }

    case LineKind::Blank:
      // Skip blank lines?
      i++;
      continue;

    case LineKind::BulletListAsterisk:
    case LineKind::BulletListPlus:
    case LineKind::BulletListHyphenMinus:
    case LineKind::BulletListBullet:
    case LineKind::BulletListTriangularBullet:
    case LineKind::BulletListHyphenBullet: {
      // If the line looks like a bullet list item, it is always a bullet list
      // item, no further checks required.
      ReSTASTNode *N;
      unsigned NumLines;
      std::tie(N, NumLines) = parseBulletList(LL.dropFrontLines(i));
      Children.push_back(N);
      i += NumLines;
      continue;
    }

    case LineKind::EnumeratedListArabic:
    case LineKind::EnumeratedListUppercaseAlphabet:
    case LineKind::EnumeratedListLowercaseAlphabet:
    case LineKind::EnumeratedListUppercaseRoman:
    case LineKind::EnumeratedListLowercaseRoman:
    case LineKind::EnumeratedListUppercaseAmbiguous:
    case LineKind::EnumeratedListLowercaseAmbiguous:
    case LineKind::EnumeratedListAuto: {
      auto SubLL = LL.dropFrontLines(i);
      bool IsListItem = isEnumeratedListItem(SubLL);
      // FIXME: more checks on indentation?
      ReSTASTNode *N;
      unsigned NumLines;
      if (IsListItem)
        std::tie(N, NumLines) = parseEnumeratedList(SubLL);
      else
        std::tie(N, NumLines) = parseParagraph(SubLL, BaseIndentation);
      Children.push_back(N);
      i += NumLines;
      continue;
    }

    case LineKind::FieldList: {
      ReSTASTNode *N;
      unsigned NumLines;
      std::tie(N, NumLines) = parseFieldList(LL.dropFrontLines(i));
      Children.push_back(N);
      i += NumLines;
      continue;
    }
    }
  }
  if (MinIndentation)
    *MinIndentation = BaseIndentation;
  assert(i != 0);
  return i;
}

unsigned Parser::parseLevel(LineListRef LL,
                            SmallVectorImpl<ReSTASTNode *> &Children) {
  if (LL.size() == 0)
    return 0;

  assert(LL[0].getClassification().Kind != LineKind::Blank);
  ColumnNum Indentation = LL[0].FirstTextCol;
  return parseLevelImpl(LL, Children, Indentation, Indentation,
                        /*IgnoreIndentationOfTheFirstLine=*/false, nullptr);
}

unsigned
Parser::parseLevelFromListItem(LineListRef LL,
                               SmallVectorImpl<ReSTASTNode *> &Children,
                               ColumnNum BulletIndentation) {
  // The text that follows the bullet determines the indentation.  Find that
  // text.
  unsigned DroppedLines = 0;
  while (LL.size() != 0 && LL[0].getClassification().Kind == LineKind::Blank) {
    LL = LL.dropFrontLines(1);
    ++DroppedLines;
  }
  if (LL.size() == 0 || LL[0].FirstTextCol <= BulletIndentation)
    return DroppedLines == 0 ? 0 : 1;
  return parseLevel(LL, Children) + DroppedLines;
}

static inline bool canPrecedeInlineMarkupStartString(unsigned C) {
  // [ReST/Syntax Details/Inline Markup]
  // Quote:
  //    1. Inline markup start-strings must start a text block or be
  //       immediately preceded by
  //       * whitespace,
  //       * one of the ASCII characters - : / ' " < ( [ { or
  //       * a non-ASCII punctuation character with Unicode category
  //         Pd (Dash), Po (Other), Ps (Open), Pi (Initial quote), or
  //         Pf (Final quote) [8].

  if (isReSTWhitespace(C))
    return true;

  switch (C) {
  case '-':
  case ':':
  case '/':
  case '\'':
  case '\"':
  case '<':
  case '(':
  case '[':
  case '{':
    return true;

  // FIXME: Unicode.
  default:
    return false;
  }
}

static inline bool canFollowInlineMarkupEndString(unsigned C) {
  // [ReST/Syntax Details/Inline Markup]
  // Quote:
  //    4. Inline markup end-strings must end a text block or be immediately
  //       followed by
  //       * whitespace,
  //       * one of the ASCII characters - . , : ; ! ? \ / ' " ) ] } > or
  //       * a non-ASCII punctuation character with Unicode category
  //         Pd (Dash), Po (Other), Pe (Close), Pf (Final quote), or
  //         Pi (Initial quote) [8].
  //
  if (isReSTWhitespace(C))
    return true;

  switch (C) {
  case '-':
  case '.':
  case ',':
  case ':':
  case ';':
  case '!':
  case '?':
  case '\\':
  case '/':
  case '\'':
  case '\"':
  case ')':
  case ']':
  case '}':
  case '>':
    return true;

  // FIXME: Unicode.
  default:
    return false;
  }
}

namespace {
enum class InlineMarkupStartStringKind {
  Emphasis,
  StrongEmphasis,
  InterpretedText_HyperlinkReference,
  InlineLiteral,
  InlineHyperlinkTarget,
  FootnoteReference,
  SubstitutionReference,
};

enum class InlineMarkupKind {
  Emphasis,
  StrongEmphasis,
  InterpretedText,
  InlineLiteral,
  HyperlinkReference,
  InlineHyperlinkTarget,
  FootnoteReference,
  // No CitationReference because the markup start-string and end-string are the
  // same as for FootnoteReference.
  SubstitutionReference,
};
} // unnamed namespace

static Optional<
    std::tuple<LineListRefIndex, LineListRefIndex, InlineMarkupStartStringKind>>
findInlineMarkupStart(LineListRefIndex I) {
  // [ReST/Syntax Details/Inline Markup]
  // Quote:
  //    1. Inline markup start-strings must start a text block or be
  //       immediately preceded by
  //       * whitespace,
  //         [...]
  //
  // If the index points to the start of the text block, then pretend that the
  // previous character is whitespace.
  unsigned PrevCh = static_cast<unsigned>(I.isStart() ? ' ' : 'x');
  while (true) {
    if (I.isEnd())
      break;

    // Try to identify a start-string at the current position.
    Optional<InlineMarkupStartStringKind> StartStringKind = None;
    auto MarkupStart = I;
    unsigned FirstCh = I.consumeUnicodeScalar();
    switch (FirstCh) {
    case '\\':
      // Consume the escape sequence completely.
      if (!I.isEnd())
        FirstCh = I.consumeUnicodeScalar();
      StartStringKind = None;
      break;

    case '*': // emphasis or strong emphasis
      if (!I.isEnd() && I.getUnicodeScalar() == '*') {
        I.consumeUnicodeScalar();
        FirstCh = '*';
        StartStringKind = InlineMarkupStartStringKind::StrongEmphasis;
      } else
        StartStringKind = InlineMarkupStartStringKind::Emphasis;
      break;

    case '`': // interpreted text or inline literal or hyperlink reference
      if (!I.isEnd() && I.getUnicodeScalar() == '`') {
        I.consumeUnicodeScalar();
        FirstCh = '`';
        StartStringKind = InlineMarkupStartStringKind::InlineLiteral;
      } else
        StartStringKind =
            InlineMarkupStartStringKind::InterpretedText_HyperlinkReference;
      break;

    case '_': // maybe inline hyperlink target
      if (!I.isEnd() && I.getUnicodeScalar() == '`') {
        I.consumeUnicodeScalar();
        FirstCh = '`';
        StartStringKind = InlineMarkupStartStringKind::InlineHyperlinkTarget;
      }
      break;

    case '[': // footnote reference
      StartStringKind = InlineMarkupStartStringKind::FootnoteReference;
      break;

    case '|': // substitution reference
      StartStringKind = InlineMarkupStartStringKind::SubstitutionReference;
      break;
    }
    if (!StartStringKind) {
      PrevCh = FirstCh;
      continue;
    }

    // [ReST/Syntax Details/Inline Markup]
    // Quote:
    //    2. Inline markup start-strings must be immediately followed by
    //       non-whitespace.
    if (I.isEnd())
      break;
    if (isReSTWhitespace(I.getUnicodeScalar())) {
      // FIXME: write a test for not consuming this.
      I.consumeUnicodeScalar();
      continue;
    }

    if (!canPrecedeInlineMarkupStartString(PrevCh))
      continue;

    return std::make_tuple(MarkupStart, I, StartStringKind.getValue());
  }
  return None;
}

static Optional<
    std::tuple<LineListRefIndex, LineListRefIndex, InlineMarkupKind>>
findInlineMarkupEnd(LineListRefIndex I,
                    InlineMarkupStartStringKind StartStringKind) {
  // [ReST/Syntax Details/Inline Markup]
  // Quote:
  //    6. An inline markup end-string must be separated by at least one
  //       character from the start-string.
  auto MaybePrevCh = I.consumePossiblyEscapedUnicodeScalar();
  if (!MaybePrevCh)
    return None;

  while (true) {
    // 3. Inline markup end-strings must be immediately preceded by
    //    non-whitespace.
    if (I.isEnd())
      break;
    unsigned FirstCh = I.consumeUnicodeScalar();
    if (FirstCh == '\\') {
      if (I.isEnd())
        continue;
      // Consume the escape sequence completely.
      FirstCh = I.consumeUnicodeScalar();
    }
    if (isReSTWhitespace(FirstCh))
      continue;

    Optional<InlineMarkupKind> MarkupKind = None;
    if (I.isEnd())
      continue;
    auto MarkupEndString = I;
    FirstCh = I.consumeUnicodeScalar();
    switch (FirstCh) {
    case '\\':
      // Consume the escape sequence completely.
      if (!I.isEnd())
        FirstCh = I.consumeUnicodeScalar();
      break;

    case '*': // emphasis or strong emphasis
      if (StartStringKind == InlineMarkupStartStringKind::Emphasis) {
        MarkupKind = InlineMarkupKind::Emphasis;
      } else if (StartStringKind ==
                     InlineMarkupStartStringKind::StrongEmphasis &&
                 !I.isEnd() && I.getUnicodeScalar() == '*') {
        FirstCh = I.consumeUnicodeScalar();
        MarkupKind = InlineMarkupKind::StrongEmphasis;
      }
      break;

    case '`': // interpreted text or inline literal or inline hyperlink target
              // or hyperlink reference
      if (StartStringKind ==
          InlineMarkupStartStringKind::InterpretedText_HyperlinkReference) {
        if (!I.isEnd() && I.getUnicodeScalar() == '_') {
          FirstCh = I.consumeUnicodeScalar();
          MarkupKind = InlineMarkupKind::HyperlinkReference;
        } else
          MarkupKind = InlineMarkupKind::InterpretedText;
      } else if (StartStringKind ==
                     InlineMarkupStartStringKind::InlineLiteral &&
                 !I.isEnd() && I.getUnicodeScalar() == '`') {
        FirstCh = I.consumeUnicodeScalar();
        MarkupKind = InlineMarkupKind::InlineLiteral;
      } else if (StartStringKind ==
                 InlineMarkupStartStringKind::InlineHyperlinkTarget) {
        MarkupKind = InlineMarkupKind::InlineHyperlinkTarget;
      }
      break;

    case ']': // footnote reference
      if (StartStringKind ==
              InlineMarkupStartStringKind::SubstitutionReference &&
          !I.isEnd() && I.getUnicodeScalar() == '_') {
        FirstCh = I.consumeUnicodeScalar();
        MarkupKind = InlineMarkupKind::SubstitutionReference;
      }
      break;

    case '|': // substitution reference
      if (StartStringKind == InlineMarkupStartStringKind::SubstitutionReference)
        MarkupKind = InlineMarkupKind::SubstitutionReference;
      break;
    }
    if (!MarkupKind) {
      // We have consumed some characters assuming we have found an end-string.
      // Restore the pointer back.
      I = MarkupEndString;
      continue;
    }

    // [ReST/Syntax Details/Inline Markup]
    // Quote:
    //    4. Inline markup end-strings must end a text block or [...]
    if (I.isEnd())
      return std::make_tuple(MarkupEndString, I, MarkupKind.getValue());

    if (!canFollowInlineMarkupEndString(I.getUnicodeScalar()))
      continue;

    return std::make_tuple(MarkupEndString, I, MarkupKind.getValue());
  }
  return None;
}

TextAndInline *Parser::parseInlineContent(LineListRef LL) {
  if (!Context.LangOpts.ExperimentalInlineMarkupParsing)
    return TextAndInline::create(Context, {});

  SmallVector<InlineContent *, 8> ItemChildren;
  Optional<std::pair<LineListRefIndex, LineListRefIndex>> StartStringRange =
      None;
  SmallVector<InlineContent *, 4> CurrentMarkupChildren;
  Optional<InlineMarkupStartStringKind> StartStringKind = None;
  for (unsigned LineIndex = 0, LineIndexEnd = LL.size();
       LineIndex != LineIndexEnd; ++LineIndex) {
    auto I = LineListRefIndex(LL, LineIndex);
    while (true) {
      LineListRefIndex MarkupStartString = I;
      if (!StartStringKind) {
        auto StartStringSearchResult = findInlineMarkupStart(I);
        if (!StartStringSearchResult)
          break;

        if (I != std::get<0>(StartStringSearchResult.getValue())) {
          auto Remainder = LL.getLinePart(
              I, std::get<0>(StartStringSearchResult.getValue()));
          ItemChildren.push_back(new (Context) PlainText(Remainder));
        }

        StartStringKind = InlineMarkupStartStringKind::Emphasis;
        std::tie(MarkupStartString, I, *StartStringKind) =
            StartStringSearchResult.getValue();

        // If there is no end-string at all, then we need to parse the
        // start-string as plain text.  Save the range to decide later.
        StartStringRange = std::make_pair(MarkupStartString, I);
      }

      auto EndStringSearchResult =
          findInlineMarkupEnd(I, StartStringKind.getValue());
      if (!EndStringSearchResult) {
        // There is no end-string in this line.  Try to find it later.
        break;
      }

      auto Part =
          LL.getLinePart(I, std::get<0>(EndStringSearchResult.getValue()));
      CurrentMarkupChildren.push_back(new (Context) PlainText(Part));

      LineListRefIndex MarkupEndString = I;
      InlineMarkupKind FoundMarkupKind;
      std::tie(MarkupEndString, I, FoundMarkupKind) =
          EndStringSearchResult.getValue();

      switch (FoundMarkupKind) {
      case InlineMarkupKind::Emphasis:
        ItemChildren.push_back(
            Emphasis::create(Context, CurrentMarkupChildren));
        break;
      case InlineMarkupKind::StrongEmphasis:
        ItemChildren.push_back(
            StrongEmphasis::create(Context, CurrentMarkupChildren));
        break;
      case InlineMarkupKind::InterpretedText:
        ItemChildren.push_back(
            InterpretedText::create(Context, CurrentMarkupChildren));
        break;
      case InlineMarkupKind::InlineLiteral:
        ItemChildren.push_back(
            InlineLiteral::create(Context, CurrentMarkupChildren));
        break;
      case InlineMarkupKind::HyperlinkReference:
        ItemChildren.push_back(
            HyperlinkReference::create(Context, CurrentMarkupChildren));
        break;
      case InlineMarkupKind::InlineHyperlinkTarget:
        ItemChildren.push_back(
            InlineHyperlinkTarget::create(Context, CurrentMarkupChildren));
        break;
      case InlineMarkupKind::FootnoteReference:
        // FIXME: tell apart FootnoteReference and CitationReference.
        // ItemChildren.push_back(FootnoteReference::create(Context,
        // CurrentMarkupChildren));
        break;
      case InlineMarkupKind::SubstitutionReference:
        // FIXME: don't create a SubstitutionReference node because we
        // don't know how to resolve substitution references.
        //
        // ItemChildren.push_back(SubstitutionReference::create(Context,
        // CurrentMarkupChildren));

        auto Part = LL.getLinePart(MarkupStartString, I);
        ItemChildren.push_back(new (Context) PlainText(Part));
        break;
      }
      CurrentMarkupChildren.clear();
      StartStringKind = None;
    }

    auto Remainder = LL.getLinePart(I, LL.end(LineIndex));
    CurrentMarkupChildren.push_back(new (Context) PlainText(Remainder));

    if (LineIndex != LineIndexEnd - 1) {
      LinePart Newline;
      Newline.Text = "\n";
      Newline.Range = {Remainder.Range.End, Remainder.Range.End};
      CurrentMarkupChildren.push_back(new (Context) PlainText(Newline));
    }
    if (!StartStringKind) {
      ItemChildren.append(CurrentMarkupChildren.begin(),
                          CurrentMarkupChildren.end());
      CurrentMarkupChildren.clear();
    }
  }

  if (StartStringKind) {
    auto StartStringText = LL.getLinePart(StartStringRange.getValue().first,
                                          StartStringRange.getValue().second);
    ItemChildren.push_back(new (Context) PlainText(StartStringText));
  }
  ItemChildren.append(CurrentMarkupChildren.begin(),
                      CurrentMarkupChildren.end());
  return TextAndInline::create(Context, ItemChildren);
}

Document *Parser::parseDocument(LineListRef LL) {
  unsigned i = 0;
  for (unsigned e = LL.size(); i != e; ++i) {
    if (LL[i].getClassification().Kind != LineKind::Blank)
      break;
  }

  auto SubLL = LL.dropFrontLines(i);

  if (SubLL.empty())
    return Document::create(Context, {});

  SmallVector<ReSTASTNode *, 8> Children;
  ColumnNum MinIndentation;
  unsigned NumLines = parseLevelImpl(
      SubLL, Children, SubLL[0].FirstTextCol, ColumnNum::make(0),
      /*IgnoreIndentationOfTheFirstLine=*/false, &MinIndentation);
  assert(NumLines == SubLL.size());

  if (!Context.LangOpts.IgnoreUniformIndentation &&
      MinIndentation != ColumnNum::make(0)) {
    auto *BQ = BlockQuote::create(Context, Children);
    Children.clear();
    Children.push_back(BQ);
  }

  return Document::create(Context, Children);
}

Document *llvm::rest::parseDocument(ReSTContext &C, LineListRef LL) {
  Parser P(C);
  return P.parseDocument(LL);
}

struct CommentToDocutilsXMLConverter {
  raw_ostream &OS;

  CommentToDocutilsXMLConverter(raw_ostream &OS) : OS(OS) {}

  void printASTNode(const ReSTASTNode *N) {
    switch (N->getKind()) {
    case ASTNodeKind::Document:
      printDocument(cast<Document>(N));
      break;

    case ASTNodeKind::Section:
    case ASTNodeKind::Topic:
    case ASTNodeKind::Sidebar:
    case ASTNodeKind::Title:
    case ASTNodeKind::Subtitle:
    case ASTNodeKind::Transition:
      llvm_unreachable("implement");

    case ASTNodeKind::Paragraph:
      printParagraph(cast<Paragraph>(N));
      break;
    case ASTNodeKind::BulletList:
      printBulletList(cast<BulletList>(N));
      break;
    case ASTNodeKind::EnumeratedList:
      printEnumeratedList(cast<EnumeratedList>(N));
      break;
    case ASTNodeKind::DefinitionListItem:
      printDefinitionListItem(cast<DefinitionListItem>(N));
      break;
    case ASTNodeKind::DefinitionList:
      printDefinitionList(cast<DefinitionList>(N));
      break;
    case ASTNodeKind::Field:
      printField(cast<Field>(N));
      break;
    case ASTNodeKind::FieldList:
      printFieldList(cast<FieldList>(N));
      break;
    case ASTNodeKind::BlockQuote:
      printBlockQuote(cast<BlockQuote>(N));
      break;
    case ASTNodeKind::TextAndInline:
      printTextAndInline(cast<TextAndInline>(N));
      break;
    case ASTNodeKind::PlainText:
      printPlainText(cast<PlainText>(N));
      break;
    case ASTNodeKind::Emphasis:
      printEmphasis(cast<Emphasis>(N));
      break;
    case ASTNodeKind::StrongEmphasis:
      printStrongEmphasis(cast<StrongEmphasis>(N));
      break;
    case ASTNodeKind::InterpretedText:
      printInterpretedText(cast<InterpretedText>(N));
      break;
    case ASTNodeKind::InlineLiteral:
      printInlineLiteral(cast<InlineLiteral>(N));
      break;
    case ASTNodeKind::HyperlinkReference:
      printHyperlinkReference(cast<HyperlinkReference>(N));
      break;
    case ASTNodeKind::InlineHyperlinkTarget:
      printInlineHyperlinkTarget(cast<InlineHyperlinkTarget>(N));
      break;

    case ASTNodeKind::FootnoteReference:
      llvm_unreachable("implement");
    case ASTNodeKind::CitationReference:
      llvm_unreachable("implement");
    case ASTNodeKind::SubstitutionReference:
      llvm_unreachable("implement");

    case ASTNodeKind::PrivateExtension:
      printPrivateExtension(cast<PrivateExtension>(N));
      break;
    }
  }

  void printDocument(const Document *D) {
    OS << "<document>";
    for (const auto *N : D->getChildren()) {
      printASTNode(N);
    }
    OS << "</document>";
  }

  void printParagraph(const Paragraph *P) {
    OS << "<paragraph>";
    printTextAndInline(P->getContent());
    OS << "</paragraph>";
  }

  void printBulletList(const BulletList *BL) {
    OS << "<bullet_list>";
    for (unsigned i = 0, e = BL->getNumItems(); i != e; ++i) {
      OS << "<list_item>";
      for (const auto *N : BL->getItemChildren(i)) {
        printASTNode(N);
      }
      OS << "</list_item>";
    }
    OS << "</bullet_list>";
  }

  void printEnumeratedList(const EnumeratedList *EL) {
    OS << "<enumerated_list>";
    for (unsigned i = 0, e = EL->getNumItems(); i != e; ++i) {
      OS << "<list_item>";
      for (const auto *N : EL->getItemChildren(i)) {
        printASTNode(N);
      }
      OS << "</list_item>";
    }
    OS << "</enumerated_list>";
  }

  void printDefinitionListItem(const DefinitionListItem *DLI) {
    OS << "<definition_list_item>";

    OS << "<term>";
    printASTNode(DLI->getTerm());
    OS << "</term>";

    for (const auto *N : DLI->getClassifiers()) {
      OS << "<classifier>";
      printASTNode(N);
      OS << "</classifier>";
    }

    OS << "<definition>";
    for (const auto *N : DLI->getDefinitionChildren()) {
      printASTNode(N);
    }
    OS << "</definition>";

    OS << "</definition_list_item>";
  }

  void printDefinitionList(const DefinitionList *DL) {
    OS << "<definition_list>";
    for (const auto *N : DL->getChildren()) {
      printASTNode(N);
    }
    OS << "</definition_list>";
  }

  void printField(const Field *F) {
    OS << "<field>";
    OS << "<field_name>";
    printASTNode(F->getName());
    OS << "</field_name>";
    OS << "<field_body>";
    for (const auto *N : F->getBodyChildren()) {
      printASTNode(N);
    }
    OS << "</field_body>";
    OS << "</field>";
  }

  void printFieldList(const FieldList *FL) {
    OS << "<field_list>";
    for (const auto *F : FL->getChildren()) {
      printASTNode(F);
    }
    OS << "</field_list>";
  }

  void printBlockQuote(const BlockQuote *BQ) {
    OS << "<block_quote>";
    for (const auto *N : BQ->getChildren()) {
      printASTNode(N);
    }
    OS << "</block_quote>";
  }

  void printTextAndInline(const TextAndInline *T) {
    for (const auto *IC : T->getChildren()) {
      printASTNode(IC);
    }
  }

  void printPlainText(const PlainText *PT) {
    LinePart LP = PT->getLinePart();
    appendWithXMLEscaping(OS, LP.Text);
  }

  void printEmphasis(const Emphasis *E) {
    OS << "<emphasis>";
    for (const auto *N : E->getChildren()) {
      printASTNode(N);
    }
    OS << "</emphasis>";
  }

  void printStrongEmphasis(const StrongEmphasis *SE) {
    OS << "<strong>";
    for (const auto *N : SE->getChildren()) {
      printASTNode(N);
    }
    OS << "</strong>";
  }

  void printInterpretedText(const InterpretedText *SE) {
    // FIXME: print role.
    OS << "<interpreted_text>";
    for (const auto *N : SE->getChildren()) {
      printASTNode(N);
    }
    OS << "</interpreted_text>";
  }

  void printInlineLiteral(const InlineLiteral *IL) {
    OS << "<literal>";
    for (const auto *N : IL->getChildren()) {
      printASTNode(N);
    }
    OS << "</literal>";
  }

  void printHyperlinkReference(const HyperlinkReference *IHT) {
    OS << "<reference>";
    for (const auto *N : IHT->getChildren()) {
      printASTNode(N);
    }
    OS << "</reference>";
  }

  void printInlineHyperlinkTarget(const InlineHyperlinkTarget *IHT) {
    OS << "<target>";
    for (const auto *N : IHT->getChildren()) {
      printASTNode(N);
    }
    OS << "</target>";
  }

  // ASTNodeKind::FootnoteReference:
  // ASTNodeKind::CitationReference:
  // ASTNodeKind::SubstitutionReference:

  void printPrivateExtension(const PrivateExtension *PE) {
    OS << "<llvm:private_extension />";
  }
};

void llvm::rest::convertToDocutilsXML(const Document *D, raw_ostream &OS) {
  CommentToDocutilsXMLConverter Converter(OS);
  Converter.printASTNode(D);
}

void ReSTASTNode::dump() const {
  CommentToDocutilsXMLConverter Converter(llvm::errs());
  Converter.printASTNode(this);
  llvm::errs() << '\n';
}

static unsigned measureReSTWhitespace(StringRef Text) {
  unsigned i = 0;
  for (unsigned e = Text.size(); i != e; ++i) {
    if (!isReSTWhitespace(Text[i]))
      break;
  }
  return i;
}

static unsigned measureReSTWord(StringRef Text) {
  unsigned i = 0;
  for (unsigned e = Text.size(); i != e; ++i) {
    if (isReSTWhitespace(Text[i]))
      break;
  }
  return i;
}

Optional<std::pair<LinePart, LinePart>> llvm::rest::extractWord(LinePart LP) {
  unsigned NumWordBytes = measureReSTWord(LP.Text);
  if (NumWordBytes == 0)
    return None;

  unsigned NumWhitespaceBytes =
      measureReSTWhitespace(LP.Text.drop_front(NumWordBytes));
  LinePart Word = {
      LP.Text.substr(0, NumWordBytes),
      SourceRange(LP.Range.Start, LP.Range.Start.getAdvancedLoc(NumWordBytes))};
  LinePart Rest = {LP.Text.drop_front(NumWordBytes + NumWhitespaceBytes),
                   SourceRange(LP.Range.Start.getAdvancedLoc(
                                   NumWordBytes + NumWhitespaceBytes),
                               LP.Range.End)};

  return std::make_pair(Word, Rest);
}

Optional<LinePart> llvm::rest::extractWord(TextAndInline *TAI) {
  if (TAI->getChildren().empty())
    return None;
  if (auto *FirstTextChild = dyn_cast<PlainText>(TAI->getChildren().front())) {
    auto WordAndRest = ::extractWord(FirstTextChild->getLinePart());
    if (!WordAndRest)
      return None;
    FirstTextChild->setLinePart(WordAndRest.getValue().second);
    return WordAndRest.getValue().first;
  }
  return None;
}

