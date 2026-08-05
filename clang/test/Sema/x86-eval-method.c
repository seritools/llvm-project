// RUN: %clang_cc1 -fexperimental-strict-floating-point \
// RUN: -triple i386-pc-windows -target-cpu pentium4 -target-feature -sse \
// RUN: -target-feature +x87-excess-precision \
// RUN: -emit-llvm -ffp-eval-method=source  -o - -verify=warn %s
//
// Without +x87-excess-precision the backend rounds every x87 result to its
// type, which is what `source` asks for, so there is nothing to warn about.
// RUN: %clang_cc1 -fexperimental-strict-floating-point \
// RUN: -triple i386-pc-windows -target-cpu pentium4 -target-feature -sse \
// RUN: -emit-llvm -ffp-eval-method=source  -o - -verify=no-warn %s
//
// RUN: %clang_cc1 -fexperimental-strict-floating-point \
// RUN: -triple i386-pc-windows -target-cpu pentium4 \
// RUN: -emit-llvm -ffp-eval-method=source  -o - -verify=no-warn %s

// no-warn-no-diagnostics

float add1(float a, float b, float c) {
  return a + b + c;
} // warn-warning{{setting the floating point evaluation method to `source` on a target without SSE is not supported}}

float add2(float a, float b, float c) {
#pragma clang fp eval_method(source)
  return a + b + c;
} // warn-warning{{setting the floating point evaluation method to `source` on a target without SSE is not supported}}
