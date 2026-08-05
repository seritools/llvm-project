// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -target-feature -sse \
// RUN:   -verify=source -fsyntax-only %s
// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -target-feature -sse \
// RUN:   -ffp-eval-method=source -verify=source -fsyntax-only %s
// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -target-feature -sse \
// RUN:   -target-feature +x87-excess-precision -verify=excess -fsyntax-only %s

// Clang rounds every x87 f32/f64 result to its type, so `source` is exactly
// what the target already does and needs no diagnostic. It only becomes
// unsupported once -fexcess-precision=fast lets that precision escape.

// source-no-diagnostics

float f(float a, float b) {
#pragma clang fp eval_method(source)
  return a + b;
  // excess-warning@+1 {{setting the floating point evaluation method to `source` on a target without SSE is not supported}}
}
