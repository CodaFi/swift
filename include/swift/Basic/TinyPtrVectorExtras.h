//===--- TinyPtrVectorExtras.h --------------------------------------------===//
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

#ifndef SWIFT_BASIC_PicoPtrVector_EXTRAS_H
#define SWIFT_BASIC_PicoPtrVector_EXTRAS_H

#include "swift/Basic/LLVM.h"
#include "llvm/ADT/PointerUnion.h"

namespace swift {

/// PicoPtrVector - Like llvm::TinyPtrVector but has a \c PointerLikeTypeTraits specialization.
template <typename EltTy>
class PicoPtrVector {
public:
  using VecTy = SmallVector<EltTy, 4>;
  using value_type = typename VecTy::value_type;
  // EltTy must be the first pointer type so that is<EltTy> is true for the
  // default-constructed PtrUnion. This allows an empty PicoPtrVector to
  // naturally vend a begin/end iterator of type EltTy* without an additional
  // check for the empty state.
  using PtrUnion = PointerUnion<EltTy, VecTy *>;

private:
  friend struct llvm::PointerLikeTypeTraits<PicoPtrVector<EltTy>>;
  PtrUnion Val;

  static PicoPtrVector<EltTy> getFromOpaqueValue(void *Val) {
    PicoPtrVector<EltTy> V;
    V.Val = PtrUnion::getFromOpaqueValue(Val);
    return V;
  }

public:
  PicoPtrVector() = default;

  ~PicoPtrVector() {
    if (VecTy *V = Val.template dyn_cast<VecTy*>())
      delete V;
  }

  PicoPtrVector(const PicoPtrVector &RHS) : Val(RHS.Val) {
    if (VecTy *V = Val.template dyn_cast<VecTy*>())
      Val = new VecTy(*V);
  }

  PicoPtrVector &operator=(const PicoPtrVector &RHS) {
    if (this == &RHS)
      return *this;
    if (RHS.empty()) {
      this->clear();
      return *this;
    }

    // Try to squeeze into the single slot. If it won't fit, allocate a copied
    // vector.
    if (Val.template is<EltTy>()) {
      if (RHS.size() == 1)
        Val = RHS.front();
      else
        Val = new VecTy(*RHS.Val.template get<VecTy*>());
      return *this;
    }

    // If we have a full vector allocated, try to re-use it.
    if (RHS.Val.template is<EltTy>()) {
      Val.template get<VecTy*>()->clear();
      Val.template get<VecTy*>()->push_back(RHS.front());
    } else {
      *Val.template get<VecTy*>() = *RHS.Val.template get<VecTy*>();
    }
    return *this;
  }

  PicoPtrVector(PicoPtrVector &&RHS) : Val(RHS.Val) {
    RHS.Val = (EltTy)nullptr;
  }

  PicoPtrVector &operator=(PicoPtrVector &&RHS) {
    if (this == &RHS)
      return *this;
    if (RHS.empty()) {
      this->clear();
      return *this;
    }

    // If this vector has been allocated on the heap, re-use it if cheap. If it
    // would require more copying, just delete it and we'll steal the other
    // side.
    if (VecTy *V = Val.template dyn_cast<VecTy*>()) {
      if (RHS.Val.template is<EltTy>()) {
        V->clear();
        V->push_back(RHS.front());
        RHS.Val = EltTy();
        return *this;
      }
      delete V;
    }

    Val = RHS.Val;
    RHS.Val = EltTy();
    return *this;
  }

  PicoPtrVector(std::initializer_list<EltTy> IL)
      : Val(IL.size() == 0
                ? PtrUnion()
                : IL.size() == 1 ? PtrUnion(*IL.begin())
                                 : PtrUnion(new VecTy(IL.begin(), IL.end()))) {}

  /// Constructor from an ArrayRef.
  ///
  /// This also is a constructor for individual array elements due to the single
  /// element constructor for ArrayRef.
  explicit PicoPtrVector(ArrayRef<EltTy> Elts)
      : Val(Elts.empty()
                ? PtrUnion()
                : Elts.size() == 1
                      ? PtrUnion(Elts[0])
                      : PtrUnion(new VecTy(Elts.begin(), Elts.end()))) {}

  PicoPtrVector(size_t Count, EltTy Value)
      : Val(Count == 0 ? PtrUnion()
                       : Count == 1 ? PtrUnion(Value)
                                    : PtrUnion(new VecTy(Count, Value))) {}

  // implicit conversion operator to ArrayRef.
  operator ArrayRef<EltTy>() const {
    if (Val.isNull())
      return None;
    if (Val.template is<EltTy>())
      return *Val.getAddrOfPtr1();
    return *Val.template get<VecTy*>();
  }

  // implicit conversion operator to MutableArrayRef.
  operator MutableArrayRef<EltTy>() {
    if (Val.isNull())
      return None;
    if (Val.template is<EltTy>())
      return *Val.getAddrOfPtr1();
    return *Val.template get<VecTy*>();
  }

  // Implicit conversion to ArrayRef<U> if EltTy* implicitly converts to U*.
  template <
      typename U,
      std::enable_if_t<std::is_convertible<ArrayRef<EltTy>, ArrayRef<U>>::value,
                       bool> = false>
  operator ArrayRef<U>() const {
    return operator ArrayRef<EltTy>();
  }

  bool empty() const {
    // This vector can be empty if it contains no element, or if it
    // contains a pointer to an empty vector.
    if (Val.isNull()) return true;
    if (VecTy *Vec = Val.template dyn_cast<VecTy*>())
      return Vec->empty();
    return false;
  }

  unsigned size() const {
    if (empty())
      return 0;
    if (Val.template is<EltTy>())
      return 1;
    return Val.template get<VecTy*>()->size();
  }

  using iterator = EltTy *;
  using const_iterator = const EltTy *;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  iterator begin() {
    if (Val.template is<EltTy>())
      return Val.getAddrOfPtr1();

    return Val.template get<VecTy *>()->begin();
  }

  iterator end() {
    if (Val.template is<EltTy>())
      return begin() + (Val.isNull() ? 0 : 1);

    return Val.template get<VecTy *>()->end();
  }

  const_iterator begin() const {
    return (const_iterator)const_cast<PicoPtrVector*>(this)->begin();
  }

  const_iterator end() const {
    return (const_iterator)const_cast<PicoPtrVector*>(this)->end();
  }

  reverse_iterator rbegin() { return reverse_iterator(end()); }
  reverse_iterator rend() { return reverse_iterator(begin()); }

  const_reverse_iterator rbegin() const {
    return const_reverse_iterator(end());
  }

  const_reverse_iterator rend() const {
    return const_reverse_iterator(begin());
  }

  EltTy operator[](unsigned i) const {
    assert(!Val.isNull() && "can't index into an empty vector");
    if (Val.template is<EltTy>()) {
      assert(i == 0 && "tinyvector index out of range");
      return Val.template get<EltTy>();
    }

    assert(i < Val.template get<VecTy*>()->size() &&
           "tinyvector index out of range");
    return (*Val.template get<VecTy*>())[i];
  }

  EltTy front() const {
    assert(!empty() && "vector empty");
    if (Val.template is<EltTy>())
      return Val.template get<EltTy>();
    return Val.template get<VecTy*>()->front();
  }

  EltTy back() const {
    assert(!empty() && "vector empty");
    if (Val.template is<EltTy>())
      return Val.template get<EltTy>();
    return Val.template get<VecTy*>()->back();
  }

  void push_back(EltTy NewVal) {
    // If we have nothing, add something.
    if (Val.isNull()) {
      Val = NewVal;
      assert(!Val.isNull() && "Can't add a null value");
      return;
    }

    // If we have a single value, convert to a vector.
    if (Val.template is<EltTy>()) {
      EltTy V = Val.template get<EltTy>();
      Val = new VecTy();
      Val.template get<VecTy*>()->push_back(V);
    }

    // Add the new value, we know we have a vector.
    Val.template get<VecTy*>()->push_back(NewVal);
  }

  void pop_back() {
    // If we have a single value, convert to empty.
    if (Val.template is<EltTy>())
      Val = (EltTy)nullptr;
    else if (VecTy *Vec = Val.template get<VecTy*>())
      Vec->pop_back();
  }

  void clear() {
    // If we have a single value, convert to empty.
    if (Val.template is<EltTy>()) {
      Val = EltTy();
    } else if (VecTy *Vec = Val.template dyn_cast<VecTy*>()) {
      // If we have a vector form, just clear it.
      Vec->clear();
    }
    // Otherwise, we're already empty.
  }

  iterator erase(iterator I) {
    assert(I >= begin() && "Iterator to erase is out of bounds.");
    assert(I < end() && "Erasing at past-the-end iterator.");

    // If we have a single value, convert to empty.
    if (Val.template is<EltTy>()) {
      if (I == begin())
        Val = EltTy();
    } else if (VecTy *Vec = Val.template dyn_cast<VecTy*>()) {
      // multiple items in a vector; just do the erase, there is no
      // benefit to collapsing back to a pointer
      return Vec->erase(I);
    }
    return end();
  }

  iterator erase(iterator S, iterator E) {
    assert(S >= begin() && "Range to erase is out of bounds.");
    assert(S <= E && "Trying to erase invalid range.");
    assert(E <= end() && "Trying to erase past the end.");

    if (Val.template is<EltTy>()) {
      if (S == begin() && S != E)
        Val = EltTy();
    } else if (VecTy *Vec = Val.template dyn_cast<VecTy*>()) {
      return Vec->erase(S, E);
    }
    return end();
  }

  iterator insert(iterator I, const EltTy &Elt) {
    assert(I >= this->begin() && "Insertion iterator is out of bounds.");
    assert(I <= this->end() && "Inserting past the end of the vector.");
    if (I == end()) {
      push_back(Elt);
      return std::prev(end());
    }
    assert(!Val.isNull() && "Null value with non-end insert iterator.");
    if (Val.template is<EltTy>()) {
      EltTy V = Val.template get<EltTy>();
      assert(I == begin());
      Val = Elt;
      push_back(V);
      return begin();
    }

    return Val.template get<VecTy*>()->insert(I, Elt);
  }

  template<typename ItTy>
  iterator insert(iterator I, ItTy From, ItTy To) {
    assert(I >= this->begin() && "Insertion iterator is out of bounds.");
    assert(I <= this->end() && "Inserting past the end of the vector.");
    if (From == To)
      return I;

    // If we have a single value, convert to a vector.
    ptrdiff_t Offset = I - begin();
    if (Val.isNull()) {
      if (std::next(From) == To) {
        Val = *From;
        return begin();
      }

      Val = new VecTy();
    } else if (Val.template is<EltTy>()) {
      EltTy V = Val.template get<EltTy>();
      Val = new VecTy();
      Val.template get<VecTy*>()->push_back(V);
    }
    return Val.template get<VecTy*>()->insert(begin() + Offset, From, To);
  }
};

} // end namespace swift

namespace llvm {
template <typename EltTy>
struct PointerLikeTypeTraits<swift::PicoPtrVector<EltTy>> {
  static inline void *getAsVoidPointer(swift::PicoPtrVector<EltTy> &V) {
    return PointerLikeTypeTraits<typename swift::PicoPtrVector<EltTy>::PtrUnion>::getAsVoidPointer(V.Val);
  }

  static inline swift::PicoPtrVector<EltTy> getFromVoidPointer(void *P) {
    return swift::PicoPtrVector<EltTy>::getFromOpaqueValue(P);
  }

  static constexpr int NumLowBitsAvailable = PointerLikeTypeTraits<typename swift::PicoPtrVector<EltTy>::PtrUnion>::NumLowBitsAvailable;
};
}

#endif
