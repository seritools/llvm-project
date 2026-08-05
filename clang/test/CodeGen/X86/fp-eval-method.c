// Without SSE the x87 unit computes at 80 bits, but every f32/f64 result is
// rounded back to its type, so the evaluation method is `source`.

// RUN: %clang_cc1 -triple i386-unknown-netbsd -emit-llvm -o - %s \
// RUN: | FileCheck %s -check-prefixes=CHECK-SRC

// RUN: %clang_cc1 -triple i386--linux -emit-llvm -o - %s \
// RUN: | FileCheck %s -check-prefixes=CHECK-SRC

// -fexcess-precision=fast lets that precision escape again.

// RUN: %clang_cc1 -triple i386--linux -target-feature +x87-excess-precision \
// RUN: -emit-llvm -o - %s | FileCheck %s -check-prefixes=CHECK-EXT

float f(float x, float y) {
  // CHECK: define{{.*}} float @f
  // CHECK: fadd float
  return 2.0f + x + y;
}

int getEvalMethod(void) {
  // CHECK: ret i32 1
  // CHECK-SRC: ret i32 0
  // CHECK-EXT: ret i32 2
  return __FLT_EVAL_METHOD__;
}
