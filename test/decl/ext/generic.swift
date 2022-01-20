// RUN: %target-typecheck-verify-swift

#if compiler(>=5.5) && $GenericExtensions
struct Foo<T> {}
extension <U> Foo<U> {
//  func foo(_ x: U) {}
//  func bar(_ x: T) {}
}
#endif
