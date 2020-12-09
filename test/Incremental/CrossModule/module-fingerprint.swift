// RUN: %empty-directory(%t)
// RUN: cp -r %S/Inputs/module-fingerprint/* %t

//
// This test establishes a "linear" chain of modules that import one another
// and ensures that a change to a top-level entity changes the module's
// interface hash and forces all of its dependents to rebuild.
//
// Module C    Module B    Module A
// -------- -> -------- -> --------
//    |                        ^
//    |                        |
//    ------------X------------

//
// Set up a clean incremental build of all three modules
//

// RUN: cd %t && %target-swiftc_driver -c -incremental -emit-dependencies -emit-module -emit-module-path %t/C.swiftmodule -enable-experimental-cross-module-incremental-build -module-name C -I %t -output-file-map %t/C.json -working-directory %t -driver-show-incremental -driver-show-job-lifecycle C.swift
// RUN: cd %t && %target-swiftc_driver -c -incremental -emit-dependencies -emit-module -emit-module-path %t/B.swiftmodule -enable-experimental-cross-module-incremental-build -module-name B -I %t -output-file-map %t/B.json -working-directory %t -driver-show-incremental -driver-show-job-lifecycle B.swift
// RUN: cd %t && %target-swiftc_driver -c -incremental -emit-dependencies -emit-module -emit-module-path %t/A.swiftmodule -enable-experimental-cross-module-incremental-build -module-name A -I %t -output-file-map %t/A.json -working-directory %t -driver-show-incremental -driver-show-job-lifecycle A.swift

//
// Now change C and ensure that B rebuilds but A does not
//

// RUN: cd %t && echo "public func other() {}" >> C.swift

// RUN: cd %t && %target-swiftc_driver -c -incremental -emit-dependencies -emit-module -emit-module-path %t/C.swiftmodule -enable-experimental-cross-module-incremental-build -module-name C -I %t -output-file-map %t/C.json -working-directory %t -driver-show-incremental -driver-show-job-lifecycle C.swift 2>&1 | %FileCheck -check-prefix MODULE-C %s
// RUN: touch %t/C.swiftmodule
// RUN: cd %t && %target-swiftc_driver -c -incremental -emit-dependencies -emit-module -emit-module-path %t/B.swiftmodule -enable-experimental-cross-module-incremental-build -module-name B -I %t -output-file-map %t/B.json -working-directory %t -driver-show-incremental -driver-show-job-lifecycle B.swift 2>&1 | %FileCheck -check-prefix MODULE-B %s
// RUN: cd %t && %target-swiftc_driver -c -incremental -emit-dependencies -emit-module -emit-module-path %t/A.swiftmodule -enable-experimental-cross-module-incremental-build -module-name A -I %t -output-file-map %t/A.json -working-directory %t -driver-show-incremental -driver-show-job-lifecycle A.swift 2>&1 | %FileCheck -check-prefix MODULE-A %s

// MODULE-C: Queuing (initial): {compile: C.o <= C.swift}

// MODULE-B: Queuing because of incremental external dependencies: {compile: B.o <= B.swift}
// MODULE-B: Job finished: {compile: B.o <= B.swift}
// MODULE-B: Job finished: {merge-module: B.swiftmodule <= B.o}

// MODULE-A: Job finished: {compile: A.o <= A.swift}
// MODULE-A: Job finished: {merge-module: A.swiftmodule <= A.o}
