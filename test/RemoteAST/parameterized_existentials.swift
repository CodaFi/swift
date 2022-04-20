// RUN: %target-swift-remoteast-test -enable-parameterized-existential-types %s | %FileCheck %s

// REQUIRES: swift-remoteast-test

@_silgen_name("printDynamicTypeAndAddressForExistential")
func printDynamicTypeAndAddressForExistential<T>(_: T)

@_silgen_name("stopRemoteAST")
func stopRemoteAST()

protocol Paddock<Animal> {
  associatedtype Animal
}

struct Chicken {}
struct Coop: Paddock {
  typealias Animal = Chicken
}

struct Pig {}
struct Pen: Paddock {
  typealias Animal = Pig
}

let coop = Coop()
// CHECK: Coop
printDynamicTypeAndAddressForExistential(coop as any Paddock)

// CHECK-NEXT: Coop
printDynamicTypeAndAddressForExistential(coop as any Paddock<Chicken>)

// CHECK-NEXT: Coop.Type
printDynamicTypeAndAddressForExistential(Coop.self as (any Paddock<Chicken>.Type))

// CHECK-NEXT: Coop.Type.Type.Type.Type
printDynamicTypeAndAddressForExistential(Coop.Type.Type.Type.self as (any Paddock<Chicken>.Type.Type.Type.Type))

let pen = Pen()
// CHECK-NEXT: Pen
printDynamicTypeAndAddressForExistential(pen as any Paddock)

// CHECK-NEXT: Pen
printDynamicTypeAndAddressForExistential(pen as any Paddock<Pig>)


stopRemoteAST()
