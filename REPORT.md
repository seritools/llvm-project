# x87 excess precision miscompile on i586 (`-m32 -mno-sse`)

## Test case

```c
int main(void) {
    volatile float c = 1.0f;
    float d = c + 0x1p-52f;
    c = d;
    return c == d ? 0 : 127;
}
```

`clang -m32 -mno-sse -O3 -fexcess-precision=standard` returns 127. It should return 0:
`d` has type `float`, `c` is a `volatile float` that was just assigned `d`, so `c == d`
must hold (the comparison may be evaluated at extended precision, but both operands are
`float` objects holding the same bits).

GCC returns 0 for the same source.

Reproduced with clang 22.1.8 (`i386-suse-linux`). It is not specific to `-fexcess-precision=standard`;
that flag is a no-op here (see §3). `-O0` happens to produce correct code, `-O1`/`-O2`/`-O3` do not.

## 1. The IR is correct

```llvm
%1 = alloca float, align 4
store volatile float 1.000000e+00, ptr %1
%2 = load volatile float, ptr %1
%3 = fadd float %2, 0x3CB0000000000000     ; 1.0f + 0x1p-52f
store volatile float %3, ptr %1
%4 = load volatile float, ptr %1
%5 = fcmp oeq float %4, %3
```

In LLVM IR semantics this is unambiguous: `%3` is an `f32` value, i.e. the correctly rounded
IEEE single result of the addition, which is exactly `1.0f`. The volatile store/reload round-trips
that `f32` through 4 bytes of memory, so `%4 == %3` and the `fcmp oeq` is true. The IR is fine and
so is the frontend. The bug is entirely in the X86 backend.

## 2. The generated code

```asm
    movl    $1065353216, (%esp)        # c = 1.0f
    flds    (%esp)                     # st0 = 1.0                     (%2)
    fadds   .LCPI0_0@GOTOFF(%eax)      # st0 = 1.0 + 2^-52, 80-bit!    (%3)
    fsts    (%esp)                     # c = (float)st0 == 1.0f, st0 unchanged
    flds    (%esp)                     # st0 = 1.0f                    (%4)
    fxch    %st(1)
    fucompp                            # compares 1.0f against the unrounded 80-bit %3
```

`fadds` only *loads* its memory operand as `f32`; the arithmetic and the result are performed at
the precision selected by the FPU control word's PC field, which on i386 Linux is extended
(64-bit significand). So `st0` after the `fadd` holds `1.0000000000000000002...`, not `1.0f`.

`fsts` rounds on the way to memory but leaves the register untouched. The comparison therefore
sees `1.0f` on one side and the excess-precision value on the other, and fails.

Compare GCC, which materialises the assignment `float d = c + 0x1p-52f` as a real rounding step:

```asm
    fadd    DWORD PTR [esp+16]
    fstp    DWORD PTR [esp]      ; round to f32 and pop
    fld     DWORD PTR [esp]      ; reload the rounded value  <-- d is now a real float
    fst     DWORD PTR [esp+16]   ; c = d
    fld     DWORD PTR [esp+16]
    fucomip st, st(1)
```

## 3. Why `-fexcess-precision=standard` does not help

On x86, clang's `-fexcess-precision=` only controls `_Float16`/`__bf16` excess precision
(`clang/lib/Driver/ToolChains/Clang.cpp:3232`); it sets `Float16ExcessPrecision`/`BFloat16ExcessPrecision`
and never touches the FP evaluation method. It is not forwarded to `-ffp-eval-method=`.

`-ffp-eval-method=extended` does reach Sema (`X86TargetInfo::getFPEvalMethod` already returns
`FEM_Extended` when `SSELevel == NoSSE`, `clang/lib/Basic/Targets/X86.h:184`), and Sema does the
GCC-style promotion in `Sema::UsualUnaryFPConversions` (`clang/lib/Sema/SemaExpr.cpp:792`). At `-O0`
that yields exactly the right IR:

```llvm
%5  = fpext float %4 to x86_fp80
%6  = fadd x86_fp80 %5, 0xK3FCB8000000000000000
%7  = fptrunc x86_fp80 %6 to float      ; <-- the rounding barrier
store float %7, ptr %3
```

But it still miscompiles at `-O1`+, for two independent reasons:

1. **InstCombine narrows it back.** `fptrunc (fadd (fpext a), (fpext b))` → `fadd float a, b` is a
   valid IR transform (a single correctly-rounded wide op then rounded to `float` is bit-identical to
   the narrow op), and it removes the barrier. After that the IR is byte-for-byte the same as §1.
2. **Even if the `fptrunc` survived, isel drops it.** See §4.

So `-ffp-eval-method=extended` produces identical (wrong) assembly at `-O3`.

`-ffloat-store` is not implemented in clang (`warning: optimization flag '-ffloat-store' is not supported`).

## 4. Root cause in the backend

Two related false assumptions.

### 4a. `RFP32`/`RFP64` claim to hold rounded values, but x87 arithmetic does not round to them

`llvm/lib/Target/X86/X86RegisterInfo.td:750`:

```tablegen
def RFP32 : RegisterClass<"X86",[f32], 32, (sequence "FP%u", 0, 6)>;
def RFP64 : RegisterClass<"X86",[f64], 32, (add RFP32)>;
def RFP80 : RegisterClass<"X86",[f80], 32, (add RFP32)>;
```

and `llvm/lib/Target/X86/X86InstrFPStack.td:47`:

> These sizes apply to the values, not the registers, which are always 80 bits; RFP32, RFP64 and
> RFP80 can be copied to each other without losing information.

The first half is the model: an `RFP32` vreg is *supposed* to contain an `f32` value. The x87
instructions selected for `ADD_Fp32`/`ADD_Fp32m`/... (`X86InstrFPStack.td:75-115`) do not maintain
that invariant, because they round to the current FPCW precision control, not to 24 bits.

Rounding therefore happens only by accident, when the value passes through memory:

- an explicit store of the right width, or
- a spill: `X86InstrInfo.cpp:4480` maps `RFP32` to `LD_Fp32m`/`ST_Fp32m` (a 4-byte slot) and
  `RFP64` to `LD_Fp64m`/`ST_Fp64m` (8 bytes).

That means the numeric result of a function depends on register allocator decisions. `-O0`
"works" here purely because everything is spilled; at `-O3` `%3` stays live in `st0`.

### 4b. `fptrunc` between x87 register classes is selected as a no-op copy

`llvm/lib/Target/X86/X86InstrFPStack.td:722-732`:

```tablegen
// FP truncations map onto simple pseudo-value conversions if they are to/from
// the FP stack.  We have validated that only value-preserving truncations make
// it through isel.
def : Pat<(f32 (any_fpround RFP64:$src)), (COPY_TO_REGCLASS RFP64:$src, RFP32)>, ...
def : Pat<(f32 (any_fpround RFP80:$src)), (COPY_TO_REGCLASS RFP80:$src, RFP32)>, ...
def : Pat<(f64 (any_fpround RFP80:$src)), (COPY_TO_REGCLASS RFP80:$src, RFP64)>, ...
```

The comment's claim ("only value-preserving truncations make it through isel") is exactly the
invariant that 4a breaks: an `RFP80` vreg produced by `fadd f80` generally does *not* hold a value
representable in `f32`. So an explicit `fptrunc x86_fp80 -> float` compiles to nothing at all.

This is why the GCC-style frontend fix cannot work on LLVM as-is, even with InstCombine taught to
leave the `fptrunc` alone: the backend would still discard it.

### Observability

The bug is latent in most code because the middle end folds away the reload that would expose it.
In `int f(float a, float b, float *p) { float x = a+b; *p = x; return *p == x; }` the `*p` reload is
forwarded to `x` in IR, so the comparison becomes `x == x` and no discrepancy is visible. It takes a
`volatile` (or an opaque call between store and reload) for the two differently-rounded copies to
both reach the assembly. The precision error itself is always there, though: any `float`/`double`
temporary that stays in an x87 register carries extra bits into subsequent arithmetic, and whether
it does depends on regalloc.

## 5. How to fix it

The invariant to restore is: **a vreg in `RFP32`/`RFP64` holds a value exactly representable in
`f32`/`f64`.** Three approaches, in descending order of how much I'd recommend them.

### Option A (recommended): stop pretending, and round explicitly after every x87 op

Legalize `f32`/`f64` arithmetic on non-SSE x86 by promoting to `f80` and inserting an explicit,
*non-erasable* round back to the narrow type.

Concretely, in `X86ISelLowering.cpp` where the x87 stack is set up (the `!Subtarget.useSoftFloat() &&
Subtarget.hasX87()` block around line 839, and the `FPStackf32`/`FPStackf64` paths):

1. Make `FADD`/`FSUB`/`FMUL`/`FDIV`/`FSQRT` (plus the `STRICT_*` variants) on `f32`/`f64`
   `Promote`-to-`f80` when there is no SSE/SSE2 for that type, so the DAG carries
   `fp_round(fadd f80 (fp_extend a), (fp_extend b))`.
2. Fix 4b so that `fp_round` from a wider x87 type is *not* a `COPY_TO_REGCLASS`. Replace those three
   patterns with a real narrowing sequence: `ST_FpP80m32` + `LD_Fp32m80` (equivalently
   `ST_FpP80m64` + `LD_Fp64m80`) through a stack temporary. Those instructions already exist
   (`X86InstrFPStack.td:430-444`, `694-696`). This alone is the minimal correctness fix; it is what
   makes the codegen match GCC's `fstp`/`fld` pair.
3. Optionally add a DAG combine to elide the round-trip when the consumer is a store of the same
   width (`fp_round f80->f32` feeding `store f32` becomes `ST_Fp80m32` directly) or when the value is
   immediately re-extended and fed to another x87 op *and* the source language permits it (i.e. under
   `afn`/unsafe-math, or when the frontend requested `FEM_Source` semantics). This keeps the common
   case from getting much worse.

Cost: this is essentially `-ffloat-store` for x87, which is what correctness on this target requires.
GCC pays the same cost. Step 3 recovers most of it for straight-line expression trees, since
the intermediate rounds inside a single C expression are not required by C's evaluation-method rules
(only assignments, casts, and function returns are rounding points) — but LLVM IR has no notion of
"this fadd is an interior node of a C expression", so the DAG has to be conservative unless the
frontend marks it.

Making this opt-out via a subtarget feature (e.g. `-mno-x87-excess-precision` / an
`x87-round-to-type` feature bit defaulting to on) is worth doing so the current fast-but-wrong
behaviour is still reachable, and so the change can be landed and evaluated for performance
separately.

### Option B: manipulate the FPCW precision control

Set PC to 24-bit (`f32`) or 53-bit (`f64`) around the computation with `fldcw`. There is precedent
in-tree: `X86ISD::FP80_ADD` / `X86ISD::STRICT_FP80_ADD` (`X86InstrFragments.td:906-916`,
`X86ISelLowering.cpp:21083-21103`) exist precisely to bump PC to 80 bits around one add on Windows,
where the default PC is 53-bit.

I would not build the general fix on this:

- PC controls the significand only, not the exponent range. Overflow, underflow and subnormal
  results are still computed with the 15-bit exponent and then double-round on the eventual store,
  so results near the range limits stay wrong. It is not a complete fix.
- Mixed `float`/`double` code needs an `fldcw` at every precision transition, which is slow
  (`fldcw` serialises the FPU) and awkward to place.
- It interacts badly with `#pragma STDC FENV_ACCESS`, user `fesetenv`, and inline asm that assumes
  the ABI-default control word.

It is reasonable only as a targeted optimisation on top of Option A for hot regions that are
homogeneously one type, and even then only where subnormal/overflow behaviour is known irrelevant.

### Option C: frontend-only (GCC's model), rejected

Have clang promote to `x86_fp80` and place `fptrunc`s at C's rounding points, i.e. wire
`-fexcess-precision=standard` to `-ffp-eval-method=extended` on non-SSE x86 (a one-line change in
`Clang.cpp:3232`), then teach InstCombine not to narrow `fptrunc(fadd(fpext,fpext))` on such targets.

This does not work by itself:

- The middle end would need a target-dependent (or attribute-dependent) veto on a transform that is
  perfectly valid in IR semantics, which is the wrong layering, and there are many more transforms
  than just the InstCombine narrowing that can reintroduce a narrow-typed FP op (SimplifyCFG, GVN's
  value forwarding, reassociation, vectorisation).
- Even with the `fptrunc` preserved, §4b means isel throws it away. So the backend fix is required
  regardless.

Once Option A is in place, wiring `-fexcess-precision=standard` to `FEM_Extended` on non-SSE x86
becomes worthwhile as a *separate* change — with a correct backend it buys back performance
(fewer rounding points: one per assignment rather than one per operation) rather than being needed
for correctness.

## 6. What was implemented

Option A, in two commits.

**`[X86] Round x87 f32/f64 results back to their own type`**

- `X86ISelLowering.cpp`: `FADD`/`FSUB`/`FMUL`/`FDIV`/`FSQRT` and their `STRICT_` forms on
  `f32`/`f64` are `setOperationPromotedToType(..., MVT::f80)` whenever the type lives on the x87
  stack. LegalizeDAG's `PromoteNode` already expands those to `fp_round(op(fp_extend, fp_extend))`.
- `LowerFP_ROUND` handles a narrowing `FP_ROUND` between two x87 types by storing to a stack slot of
  the destination width (`X86ISD::FST`) and reloading, instead of letting isel pick the
  `COPY_TO_REGCLASS` from §4b. Factored into `X86TargetLowering::RoundX87ToType`, which also replaces
  the open-coded copy of the same sequence in `BuildFILD`.
- `BuildFILD` builds the `FILD` as `f80` and rounds, since `FILD` also rounds to the control word's
  precision rather than to the destination type.
- Two peepholes keep the common cases free: `LowerFP_ROUND` returns the node unchanged when every
  user is a store of exactly that width (the store's `FST` *is* the rounding step), and
  `combineStore` folds `store (fp_round X)` to a truncating store. `float x = a+b; *p = x;` is
  therefore still `fadds; fstps (%eax)`, identical to before.

**`[X86][GlobalISel] Widen x87 f32/f64 arithmetic to s80`**

- `X86LegalizerInfo`: the same ops on `s32`/`s64` are widened to `s80` when they'd land on x87.
  `LegalizerHelper::widenScalar` uses `G_FPEXT`/`G_FPTRUNC` for these opcodes, and X86 already
  lowers `G_FPTRUNC` from `s80` through memory. Without this GlobalISel kept miscompiling `float`
  (it was already falling back to SelectionDAG for `double`).

**The toggle.** `-x86-x87-round-to-type` (hidden, default on). With `=false` the whole X86 codegen
test suite passes unmodified against the pre-change checked-in expectations, i.e. the opt-out is
byte-for-byte the historical behaviour. A subtarget feature would be the more idiomatic user-facing
spelling; this is the backend-internal equivalent and the thing to build one on top of.

**Verified.** All 64784 `llvm/test` tests pass. Executing the program from the top of this report on
the generated code returns 0 with the flag on and 127 with it off. 35 X86 codegen tests changed and
were regenerated; the sole hand-edit is `GlobalISel/sqrt.mir`, whose hand-written pre-legalized MIR
only reaches `SQRT_Fp32`/`SQRT_Fp64` under the opt-out and now passes the flag.

**Not done.** `clang` was not built here, so clang's own tests were not run. Wiring
`-fexcess-precision=standard` to `FEM_Extended` on non-SSE x86 (§5, Option C) is now worth doing as a
follow-up, as a performance improvement rather than a correctness fix — it moves the rounding points
from every operation to every assignment.

## 7. Suggested test coverage

`llvm/test/CodeGen/X86/x87-round-to-type.ll` covers, under both settings of the flag: the original
compare-against-reload shape, `fadd`/`fmul`/`fdiv`/`fsqrt` results left in a register, results only
stored (must not gain a round-trip), results both stored and used, `fptrunc` from `x86_fp80` to
`float`/`double`, `fptrunc double -> float`, `sitofp`, and a value live across a call (so it is
spilled to a 4-byte slot).

Still missing: an execution test (`clang/test/CodeGen` or a `test-suite` entry) of the program at
the top of this report.
