// RUN: %target-typecheck-verify-swift -enable-objc-interop

struct StackBuffer<T, let N: UInt> {
  var count: Int {
    return UInt(N)
  }
}
