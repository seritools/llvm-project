// Note: %s must be preceded by --, otherwise it may be interpreted as a
// command-line option, e.g. on Mac where %s is commonly under /Users.

// RUN: %clang -### --target=i386 -fexcess-precision=fast -c %s 2>&1  \
// RUN:   | FileCheck --check-prefix=CHECK-FAST %s
// RUN: %clang_cl -### --target=i386 -fexcess-precision=fast -c -- %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CHECK-FAST %s

// RUN: %clang -### --target=i386 -fexcess-precision=standard -c %s 2>&1  \
// RUN:   | FileCheck --check-prefix=CHECK-STD %s
// RUN: %clang_cl -### --target=i386 -fexcess-precision=standard -c -- %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CHECK-STD %s

// RUN: %clang -### --target=i386 -fexcess-precision=16 -c %s 2>&1  \
// RUN:   | FileCheck --check-prefix=CHECK-NONE %s
// RUN: %clang_cl -### --target=i386 -fexcess-precision=16 -c -- %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CHECK-NONE %s

// RUN: not %clang -### --target=i386 -fexcess-precision=none -c %s 2>&1  \
// RUN:   | FileCheck --check-prefix=CHECK-ERR-NONE %s
// RUN: not %clang_cl -### --target=i386 -fexcess-precision=none -c -- %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CHECK-ERR-NONE %s

// RUN: %clang -### --target=x86_64 -fexcess-precision=fast -c %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CHECK-FAST %s
// RUN: %clang_cl -### --target=x86_64 -fexcess-precision=fast -c -- %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CHECK-FAST %s

// RUN: %clang -### --target=x86_64 -fexcess-precision=standard -c %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CHECK-STD %s
// RUN: %clang_cl -### --target=x86_64 -fexcess-precision=standard -c \
// RUN: -- %s 2>&1 | FileCheck --check-prefix=CHECK-STD %s

// RUN: %clang -### --target=x86_64 -fexcess-precision=16 -c %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CHECK-NONE %s
// RUN: %clang_cl -### --target=x86_64 -fexcess-precision=16 -c -- %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CHECK-NONE %s

// RUN: not %clang -### --target=x86_64 -fexcess-precision=none -c %s 2>&1 \
// RUN:   | FileCheck --check-prefixes=CHECK-ERR-NONE %s
// RUN: not %clang_cl -### --target=x86_64 -fexcess-precision=none -c -- %s 2>&1 \
// RUN:   | FileCheck --check-prefixes=CHECK-ERR-NONE %s

// RUN: %clang -### --target=aarch64 -fexcess-precision=fast -c %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CHECK %s
// RUN: %clang_cl -### --target=aarch64 -fexcess-precision=fast -c -- %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CHECK %s

// RUN: %clang -### --target=aarch64 -fexcess-precision=standard -c %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CHECK %s
// RUN: %clang_cl -### --target=aarch64 -fexcess-precision=standard -c \
// RUN: -- %s 2>&1 | FileCheck --check-prefix=CHECK %s

// RUN: not %clang -### --target=aarch64 -fexcess-precision=16 -c %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CHECK-ERR-16 %s
// RUN: not %clang_cl -### --target=aarch64 -fexcess-precision=16 -c -- %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CHECK-ERR-16 %s

// RUN: not %clang -### --target=aarch64 -fexcess-precision=none -c %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CHECK-ERR-NONE %s
// RUN: not %clang_cl -### --target=aarch64 -fexcess-precision=none -c -- %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CHECK-ERR-NONE %s

// On X86, "fast" also lets x87 f32/f64 results keep the register's extended
// precision; "standard" and "16" leave the default rounding in place.
// CHECK-FAST: "-ffloat16-excess-precision=fast"
// CHECK-FAST: "-fbfloat16-excess-precision=fast"
// CHECK-FAST: "-target-feature" "+x87-excess-precision"
// CHECK-STD-NOT: "+x87-excess-precision"
// CHECK-STD: "-ffloat16-excess-precision=standard"
// CHECK-STD: "-fbfloat16-excess-precision=standard"
// CHECK-NONE-NOT: "+x87-excess-precision"
// CHECK-NONE: "-ffloat16-excess-precision=none"
// CHECK-NONE: "-fbfloat16-excess-precision=none"
// CHECK-ERR-NONE: unsupported argument 'none' to option '-fexcess-precision='
// CHECK: "-cc1"
// CHECK-NOT: "+x87-excess-precision"
// CHECK-NOT: "-ffloat16-excess-precision=fast"
// CHECK-NOT: "-fbfloat16-excess-precision=fast"
// CHECK-ERR-16: unsupported argument '16' to option '-fexcess-precision='
