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

  required init() { self.x = 42 }

  @test static func bar() {
    print("Static")
  }

  @test("") func foo() throws {
    print("Enter")
    print(self)
    print("Exit")
  }
}

typealias GlobalTest = @convention(thin) () throws -> ()
typealias StaticTest = @convention(thin) (AnyTestSuite.Type) throws -> ()
typealias InstanceTest = @convention(thin) (AnyTestSuite) throws -> ()

typealias SectionVisitor = @convention(block) (UnsafeRawPointer, UnsafeRawPointer) -> Void

@_silgen_name("swift_enumerateTests")
func enumerateTests(_ globalVisitor: SectionVisitor, _ metaVisitor: SectionVisitor, _ instanceVisitor: SectionVisitor)

func relativePointer(base: UnsafeRawPointer, offset: Int) -> UnsafeRawPointer {
  let off = (base + offset).load(as: Int32.self)
  return (base + offset).advanced(by: Int(off))
}

// struct TestDescriptor {
//   MetadataPointer metadata;
//   RelativeDirectPointer<void(...)> testFunction;
//   RelativeDirectPointer<const char, /*nullable*/ false> Name;
//   enum : uint32_t {
//     TestCallingConventionGlobal = 0,
//     TestCallingConventionThruMetatype = 1 << 0,
//     TestCallingConventionThruInstance = 1 << 1,
//   } flags;
// }[0];
enumerateTests(
    { section, meta in
      let realPtr = unsafeBitCast(relativePointer(base: section, offset: 4), to: GlobalTest.self)
      try! realPtr()
    },
    { section, meta in

      let typePtr = unsafeBitCast(relativePointer(base: section, offset: 0), to: UnsafePointer<CChar>.self)
      guard let mangledName = String(validatingUTF8: typePtr) else {
        fatalError("Failed to get mangled name!")
      }
      guard let metadata = _typeByName(mangledName) else {
        fatalError("Failed to get metatype for mangled name: \(mangledName)!")
      }
      guard let type = metadata as? AnyTestSuite.Type else {
        // Not a testing context
        print("AnyTestSuite conformance check failed for: \(metadata)")
        return
      }
      let realPtr = unsafeBitCast(relativePointer(base: section, offset: 4), to: StaticTest.self)
      try! realPtr(type)
    },
    { section, meta in
      let typePtr = unsafeBitCast(relativePointer(base: section, offset: 0), to: UnsafePointer<CChar>.self)
      guard let mangledName = String(validatingUTF8: typePtr) else {
        fatalError("Failed to get mangled name!")
      }
      guard let metadata = _typeByName(mangledName) else {
        fatalError("Failed to get metatype for mangled name: \(mangledName)!")
      }
      guard let type = metadata as? AnyTestSuite.Type else {
        // Not a testing context
        print("AnyTestSuite conformance check failed for: \(metadata)")
        return
      }
      let value = type.init()
      let ptr = relativePointer(base: section, offset: 4)
      let realPtr = unsafeBitCast(ptr, to: InstanceTest.self)
      print("\(ptr) - \(meta)")
      try! realPtr(value as AnyTestSuite)
    })
