// RUN: %target-typecheck-verify-swift -enable-experimental-bound-generic-extensions

extension Array<Int> {
  func someIntFuncOnArray() {}
}

let _ = [0, 1, 2].someIntFuncOnArray()

extension [Character] {
  func makeString() -> String { fatalError() }
}

let _ = ["a", "b", "c"].makeString()

extension Set<_> {} // expected-error {{cannot extend a type that contains placeholders}}

// https://bugs.swift.org/browse/SR-4875

struct Foo<T, U> {
    var x: T
    var y: U
}

typealias IntFoo<U> = Foo<Int, U>

extension IntFoo where U == Int {
    func hello() {
        print("hello")
    }
}

Foo(x: "test", y: 1).hello() // expected-error {{cannot convert value of type 'String' to expected argument type 'Int'}}

