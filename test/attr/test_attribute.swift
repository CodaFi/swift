// RUN: %target-swift-typecheck-verify -enable-testing

@test public func go1() {
  print("Hello World")
}

@test("") public func go2() {
  print("Silence")
}

@test public func go3() {
  print("No name")
}

class Foo: AnyTestSuite {
  let x: Int
  static var y: Int = 42

  required init() { self.x = 42 }

  @test static func bar() {
    print(Self.y)
  }

  @test("") func foo() {
    print(self.x)
  }
}

