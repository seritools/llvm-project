// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -target-feature -sse \
// RUN:   -O1 -emit-llvm -o - %s | FileCheck %s --check-prefixes=CHECK,SRC
// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -target-feature -sse \
// RUN:   -target-feature +x87-excess-precision -O1 -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,FAST
// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -target-feature -sse \
// RUN:   -ffp-eval-method=extended -O1 -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefix=EXT

// The x87 evaluation format only reaches the IR when it is asked for with
// -ffp-eval-method=extended. -fexcess-precision= is a codegen knob deciding
// whether the backend rounds each x87 result to its type, and reaches the
// frontend only as the +x87-excess-precision target feature, which changes
// __FLT_EVAL_METHOD__ but not the arithmetic Clang emits.

// CHECK-LABEL: define {{.*}}float @expr(
// CHECK: fmul float
// CHECK: fmul float
// CHECK: fadd float
// CHECK-NOT: x86_fp80
//
// EXT-LABEL: define {{.*}}float @expr(
// EXT: fmul x86_fp80
// EXT: fmul x86_fp80
// EXT: fadd x86_fp80
// EXT: fptrunc x86_fp80 {{.*}} to float
float expr(float a, float b, float c, float d) {
  return a * b + c * d;
}

// CHECK-LABEL: define {{.*}}i32 @eval_method(
// SRC: ret i32 0
// FAST: ret i32 2
//
// EXT-LABEL: define {{.*}}i32 @eval_method(
// EXT: ret i32 2
int eval_method(void) {
  return __FLT_EVAL_METHOD__;
}
