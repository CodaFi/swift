// RUN: %target-swift-typecheck-verify -enable-testing

@_test("Simple test") func go1() {
  print("Hello World")
}

@_test("") func go2() {
  print("Silence")
}

@_test func go3() {
  print("No name")
}

typealias testVisitor_t = @convention(block) (UnsafePointer<CChar>, @convention(c) () -> ()) -> ()

@_silgen_name("swift_enumerateTests")
func enumerateTests(_ visitor: testVisitor_t)

enumerateTests { name, call in
  print("Running Test Named \"\(String(cString: name))\"")
  call()
}
