# Implementing the AArch64 (ARM64) JIT for p2 — implementation guide

> Status: **not started** (the existing `core/vm-arm.c` is a 32‑bit ARM7
> skeleton — mostly empty stubs and wrong instruction set for AArch64 — and
> `Makefile:36` marks it `# not yet ready`). This document is everything
> needed to write a real ARM64 backend, validated against the x86 reference
> (`core/vm-x86.c`, 1217 lines) and the C interpreter (`core/vm.c`).
>
> Target: Apple Silicon (macOS arm64) and Linux aarch64. Build with
> `aarch64-linux-gnu-gcc` + run under `qemu-aarch64 -L /usr/aarch64-linux-gnu`
> for CI, and on a real arm64 machine for final validation.

---

## 1. How the p2 JIT works (read this first)

p2's "JIT" is a **template JIT**: `potion_jit_proto(P, proto)` walks the
bytecode of a `PNProto` once and emits native machine code for each opcode
into a growable buffer (`PNAsm`), then copies it to an executable page and
stores the function pointer in `f->jit`. There is **no IR and no register
allocation** — each bytecode op maps to a fixed snippet of host code.

### The driver — `core/vm.c:221 potion_jit_proto()`

```
target->setup(P, f, &asmb);                    // prologue
// ... compute regs, lregs, need, protoargs, rsp ...
target->stack(P, f, &asmb, rsp);               // reserve stack frame
target->registers(P, f, &asmb, need);          // init P/self/arg registers
for each local arg: target->local(...);        // move incoming args into regs
if (upc) target->upvals(...);                  // load upvalues
for (pos = 0; pos < nops; pos++) {
    offs[pos] = asmb->len;                     // record bytecode->code offset
    patch_pending_jumps_to(pos);               // via target->jmpedit
    switch (op.code) { CASE_OP(ADD,...) ... }  // call target->op[opcode]
}
target->finish(...);
fn = PN_ALLOC_FUNC(asmb->len);                 // mmap executable page
memcpy(fn, asmb->ptr, asmb->len);
return f->jit = (PN_F)fn;
```

### The target vtable — `PNTarget` (`core/potion.h:596`)

`MAKE_TARGET(arch)` (in `core/asm.h:25`) builds a `PNTarget` named
`potion_target_<arch>` from a fixed set of function pointers. You must
implement **every** one of these for `arm` (AArch64):

| slot | purpose | x86 impl |
|---|---|---|
| `setup` | function prologue (push frame, set fp) | `potion_x86_setup` |
| `stack` | reserve `rsp` bytes of stack | `potion_x86_stack` |
| `registers` | init register slots for P, self, args | `potion_x86_registers` |
| `local` | move incoming arg N into register `reg` | `potion_x86_local` |
| `upvals` | copy closure upvalues into registers | `potion_x86_upvals` |
| `op[OP_MAX]` | per-opcode code emitter (48 ops) | `potion_x86_*` |
| `jmpedit` | patch a forward jump's displacement | `potion_x86_jmpedit` |
| `finish` | epilogue hook (x86: empty) | `potion_x86_finish` |
| `mcache` | inline method-cache lookup in `bind` | `potion_x86_mcache` |
| `ivars` | inline ivar access | `potion_x86_ivars` |

The full list of `op[]` handlers to port (from `core/vm-x86.c`):
`move, loadk, loadpn, self, newtuple, gettuple, settuple, getlocal, setlocal,
getupval, setupval, global, gettable, settable, newlick, getpath, setpath,
add, sub, mult, div, rem, pow, not, cmp, eq, neq, lt, lte, gt, gte, bitn,
bitl, bitr, def, bind, message(msg), jmp, test, testjmp, notjmp, named, call,
callset, tailcall, return, method, class` (plus `setup/finish/mcache/ivars/
local/upvals/stack/registers/jmpedit`). `OP_DEBUG` is skipped in the JIT
(`vm.c: case OP_DEBUG: break;`).

### The register file (the key concept)

There is **no fixed register assignment to values**. Instead, all potion
"registers" `R[0..need]` live in a contiguous array on the native stack
frame, addressed as an offset from the frame pointer. On x86‑64:

```
#define RBP(x) (0x100 - ((x + 1) * sizeof(PN)))     // byte offset from %rbp
// access register x:  mov -RBP(x)(%rbp), %reg
```

So `R[x]` is `*(PN*)((char*)rbp - RBP(x))`. `need = lregs + upc + 3` registers
are reserved; `rsp = (need + protoargs) * sizeof(PN)` bytes of stack.
**On AArch64 mirror this exactly**: keep `R[0..need]` in a stack array at a
fixed offset from the frame pointer `x29`, and every op loads/stores through
`x29`. This makes the per-op emitters simple and uniform and matches how the
driver computes `need`/`rsp`. (`fp`-relative `ldr/str` with a signed 9‑bit
scaled immediate covers ±256 elements — plenty.)

`setup`/`stack` also pass the first few argument slots specially:
`registers()` writes the incoming `(Potion *P, PN self, ...)` into
`R[need-3]=P`, `R[need-2]=self`, `R[need-1]=...`, and `R[0]=self` (see
`potion_x86_registers`). Reproduce that layout.

---

## 2. Recommended strategy — call‑based first, inline later

The x86 backend inlines integer math, SSE double compares, and method-cache
probes. **Do not port that first.** Write a *correct* AArch64 backend where
complex operations **call the existing C helpers** (the same ones the
interpreter uses), and only inline the trivial moves/loads. This is a real
JIT (it eliminates the dispatch loop and compiles to native code) and is
~90 % less code. Optimize hot ops later.

Reusable C helpers already in the tree (call these for the hard ops):

| opcode(s) | call |
|---|---|
| ADD/SUB/MULT/DIV/REM | `potion_obj_add/sub/mult/div/rem(P, a, b)` (after the inline int fast path; see `PN_VM_MATH2/3` in `vm.c`) |
| POW | `potion_num_pow(P, cl, self, sup)` — a 4‑arg method fn, always a call (`vm-x86.c potion_x86_pow` shows the exact shape) |
| BITL/BITR | `potion_obj_bitl`/`potion_obj_bitr` |
| CMP/NUMCMP (eq/neq/lt/...) | `potion_vm_eq`, `potion_vm_neq` (`core/vm.c:416`), or `potion_obj_cmp` |
| GETTABLE/SETTABLE/GETPATH/SETPATH/GETTUPLE/SETTUPLE/NEWLICK/CLASS/GETGLOBAL | the `potion_*` C fns used by the interpreter cases in `vm.c` |
| MSG (method call) | `potion_message(P, self, name)` / the interpreter's OP_MSG body |
| CALL/CALLSET/TAILCALL | `potion_vm_proto(P, cl, self, ...)` / interpreter OP_CALL body |

`core/vm.c` is the authoritative semantics for **every** opcode — read each
`case OP_*` there, then look at how `vm-x86.c` emits equivalent code. When in
doubt, emit a call to the C helper and move on.

### AArch64 ABI cheat‑sheet (AAPCS64) for the emitters

- Args: `x0`–`x7` (integer/pointer); return in `x0`.
- Callee‑saved: `x19`–`x28`, `x29`(fp), `x30`(lr), `sp`. **All other regs are
  caller‑saved** — clobber freely for scratch (use `x9`–`x15`, `x16`(IP0),
  `x17`(IP1) as scratch; `x18` is the platform register — avoid).
- **`sp` must be 16‑byte aligned at every public call.** Maintain this in the
  prologue (`stp`/`sub` by a multiple of 16).
- Frame: standard `stp x29,x30,[sp,#-N]! ; mov x29,sp` prologue,
  `ldp x29,x30,[sp],#N ; ret` epilogue.
- To call a C helper: load args into `x0..xN` (from the `R[]` frame via
  `ldr`), `bl <helper>`; result lands in `x0`; store to `R[a]` via `str`.
  Materialize 64‑bit constants with `movz`+`movk` pairs, or load from a
  constant pool appended after the code (`adr xN, #off`).

### Minimal op set to get `say "hi"` / arithmetic working

Implement in this order and stop to test at each:
`setup, stack, registers, local, finish, jmpedit` (frame)
→ `move, loadpn, loadk, getlocal, setlocal, return` (skeleton runs)
→ `jmp, testjmp, notjmp, test, not` (control flow)
→ `call, msg` (calls — needed for `say`)
→ `add, sub, mult, ...` via helpers (math)
→ the rest (tables, paths, tuples, upvals, named, def/bind, class, mcache).

---

## 3. callcc / continuations for ARM64 — `core/callcc.c`

Continuations copy the C stack into a `PNCont` object and later restore it by
**replacing `%sp`/`%fp` and the callee‑saved registers** with inline asm. The
x86‑64 version (`potion_continuation_yield`) is the reference; it has **no
portable fallback** (`#else` just prints `callcc/yield does not work outside
of X86 yet`). Port both asm blocks.

What the x86‑64 code does (map each step to AArch64):

1. **yield (`potion_continuation_yield`)**: restore context.
   - x86: load saved `%rsp`,`%rbp` from `cc->stack`, copy the saved stack
     range to the live stack, restore `%rbx/%r12–%r15`, `leave; ret`.
   - **AArch64**: load saved `sp`,`x29` from `cc->stack`; restore callee‑saved
     `x19–x28`,`x29`,`x30`; copy the saved stack range down to the live `sp`;
     `ret`. The register save/restore and the block copy are the same shape.
     AAPCS64 callee‑saved set = `x19–x29` (+`x30`), so `PN_SAVED_REGS` must
     count those (see §5).

2. **capture (`potion_callcc`)**: save context.
   - x86: `POTION_ESP(&sp2)`/`POTION_EBP(&sp3)` read the current stack/frame,
     compute the range `[sp2, cstack)`, allocate `PNCont`, `PN_MEMCPY_N` the
     range into `cc->stack`, then store `%rbx/%r12–%r15` into the closure.
   - **AArch64**: read current `sp`/`x29` (an inline `mov xN, sp` — see §5
     for the `POTION_ESP` macro port), compute range, memcpy into `cc->stack`,
     then store `x19–x28`,`x29` into the closure's saved‑regs area.

Stack layout note: `struct PNCont` reserves `4 + PN_SAVED_REGS` header words
(`cc->stack[0..3]` = sp/fp markers + self, then saved regs, then the copied
stack). Keep that exact layout so `yield` and `capture` agree; only the
*register list* and the *asm* change.

The hardest part is that these are **naked-ish stack switches** — get the
`sp` restore and the block‑copy direction exactly right or it segfaults.
Test `callcc`/`here` in isolation before anything else that uses it.

---

## 4. Build / config wiring you must change

1. **`tools/config.sh`** — `jit` dispatch currently routes aarch64→ARM (after
   the detection fix) which then enables the not‑ready backend. Once
   `vm-arm.c` is a real AArch64 backend, that routing is what you want:
   `JIT_ARM` non‑empty → `echo ARM` (you may need to *revert* the temporary
   "emit nothing on ARM" change from the `pvip` branch if it landed there).
   Make sure `JIT_TARGET=ARM` reaches `core/config.h` as
   `#define POTION_JIT_TARGET POTION_ARM` and `#define POTION_JIT 1`.
2. **`core/potion.h`** — `POTION_ARM` already exists (`=2`); `PN_SIZE_T` is 8
   on AArch64 so the 64‑bit paths are selected. No change unless you add a
   distinct `POTION_AARCH64` id.
3. **`core/vm.c:119-140 potion_vm_init`** — already wires
   `POTION_JIT_TARGET==POTION_ARM → P->target = potion_target_arm;`. Works
   once `potion_target_arm` is the real `MAKE_TARGET(arm)` symbol.
4. **`Makefile:36`** — `SRC += core/vm-arm.c`. Change the comment; ensure it's
   the AArch64 file. (`vm-ppc.c`/`vm-x86.c` are only added when their
   `JIT_*=1`, so they won't collide on an ARM build.)
5. **`core/internal.h` + `core/callcc.c`** — the
   `#if defined(POTION_JIT_TARGET) && (POTION_X86 == POTION_JIT_TARGET)`
   guards added on `pvip` are correct: on an ARM‑JIT build they fall through
   to the non‑x86 branches. For `POTION_ESP`/`PN_SAVED_REGS` you must add an
   AArch64 branch (see §5) — the current `#else` (portable `potion_esp()`,
   `PN_SAVED_REGS 0`) is a working stopgap for GC but **breaks callcc**, which
   needs real register saves.

---

## 5. `POTION_ESP` / stack‑pointer access for AArch64 — `core/internal.h:93`

GC scans the stack from the current SP up to the interpreter's entry SP, so
it needs the raw stack pointer. x86 uses inline asm (`mov %rsp,%0`); the
portable fallback is `potion_esp()` (`core/internal.c:316`). For AArch64:

- `POTION_ESP(p)`: `__asm__("mov %0, sp" : "=r"(*p))` (AArch64 can read `sp`
  directly into a general reg). No `+0x178`/`+0xd0` adjustment unless ASAN
  shifts the frame (mirror the x86 ASAN cases if you build with ASAN).
- `POTION_EBP(p)`: `__asm__("mov %0, x29" : "=r"(*p))`.
- `PN_SAVED_REGS`: count of callee‑saved GPRs the JIT/callcc preserve =
  **10** (`x19..x28` = 10, plus `x29`/`x30` handled separately) for the
  JIT path; the portable GC path uses `0`.

Add a `#elif defined(__aarch64__)` (or key off `POTION_ARM == POTION_JIT_TARGET`)
branch in that block rather than reusing the x86‑only asm.

---

## 6. Testing

### Linux aarch64 via cross‑compiler + qemu (CI‑able, no Mac required)

```sh
# toolchain available on Ubuntu: aarch64-linux-gnu-gcc, qemu-aarch64
cd p2
# build the whole tree for aarch64 (config.sh detects the cross target):
CC=aarch64-linux-gnu-gcc make clean
CC=aarch64-linux-gnu-gcc make
# run the full suite under user-mode qemu (needs the aarch64 sysroot):
export QEMU_LD_PREFIX=/usr/aarch64-linux-gnu     # or qemu-aarch64 -L <dir>
qemu-aarch64 -L /usr/aarch64-linux-gnu ./bin/p2 --version
CC=aarch64-linux-gnu-gdb qemu-aarch64 -L /usr/aarch64-linux-gnu make test
```

Caveat: qemu user‑mode runs JIT code fine (it translates the generated
aarch64), so this validates codegen correctness. It does **not** reproduce
macOS‑specific restrictions (see W^X below).

### macOS arm64 (real hardware — do this before calling it done)

```sh
brew install gcc make
make CC=gcc          # or clang; config.sh must detect aarch64-apple-darwin
make test
```

**macOS W^X / hardened runtime:** this codebase emits code into an
`mmap`'d page and jumps to it. Modern Apple Silicon macOS forbids
write‑**and**‑execute pages unless the mapping uses `MAP_JIT` and you toggle
`pthread_jit_write_protect_np()` around writes, or ship with the
`com.apple.security.cs.allow-unsigned-executable-memory` entitlement. If the
JIT still segfaults *only on macOS* while the identical build works under
qemu/linux, **this is the cause** — fix it in `PN_ALLOC_FUNC`/
`potion_mmap` (`core/internal.c`) by using `MAP_JIT` +
`pthread_jit_write_protect_np(false)` before `memcpy` and `(true)` after.
Test binaries (`bin/potion-test`) need the same treatment or run with JIT off.

---

## 7. Gotchas & known traps

- **`POTION_UNKNOWN == POTION_X86 == 0`**: any `POTION_JIT_TARGET` you emit
  that isn't one of the three named ids silently aliases to X86. Emit `ARM`
  (id 2), never a new bare word, or guard with `defined(...)`.
- **32‑bit vs 64‑bit**: `PN` is a tagged `unsigned long` (8 bytes on aarch64).
  All the `PN_SIZE_T==8` paths apply. Integers are `value<<1 | 1` (low bit set
  = int); test `PN_IS_INT` with `tst xN, #1` before the inline fast path.
- **Endianness / GC scan direction**: set by `POTION_STACK_DIR` from
  `config.sh`; don't hard‑code the scan direction in emitted code.
- **`jmpedit`**: forward jumps are emitted with a placeholder and patched by
  the driver via `target->jmpedit(P,f,&asmb, asmj, dist)`. Match the x86
  contract: store `dist` (code bytes from the field *after* the operand) as a
  4‑byte displacement at `asmj`. AArch64 `b.cond`/`b` use a **word‑scaled**
  26‑bit offset (`offset = byte_delta >> 2`); keep jumps within ±128 MiB.
- **Alignment**: every `bl` to C must have `sp` 16‑byte aligned; the simplest
  way is to allocate the whole `R[]` frame as a multiple of 16 in `stack`.
- **`PN_HAS_UPVALS`**: `registers()` must zero locals when a sub‑proto has
  upvals (see `potion_x86_registers`) or closures capture garbage.
- **method cache (`mcache`) / ivars**: optional for a first cut — set the
  `PNTarget.mcache`/`ivars` slots to `NULL`/minimal and let `objmodel.c`
  fall back (`if (P->target.mcache != NULL)`), exactly like a fresh target.
  Add them after the core works.

---

## 8. Suggested order of work

1. Port the frame: `setup/stack/registers/local/upvals/finish/jmpedit` +
   `move/loadpn/loadk/getlocal/setlocal/return`. Get a 1‑line script to run
   under qemu.
2. Control flow (`jmp/test/testjmp/notjmp/not`) — loops & `if`.
3. Calls (`call/msg`) via `potion_vm_proto`/`potion_message` — unlocks
   `say`, method dispatch, the whole stdlib.
4. Math/comparison via C helpers (`potion_obj_*`, `potion_vm_eq/neq`).
5. Tables/tuples/paths/lick/global/class/upvals/named/def/bind.
6. `POTION_ESP`/GC AArch64 branch in `internal.h`.
7. callcc AArch64 (`capture` + `yield`); test `here`/continuations alone.
8. Only then: inline int math, method cache, ivars.
9. Wire `config.sh` to emit `ARM`, enable `vm-arm.c`, and run the full
   `make test` under qemu **and** on a real macOS arm64 machine (fixing W^X
   if needed).

Keep `test.p2`/`test.p5`/`test.p6` green on x86‑64 at every step — the
interpreter semantics there are the ground truth for what the ARM64 backend
must reproduce.
