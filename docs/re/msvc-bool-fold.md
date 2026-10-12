# MSVC 19.50.35728 x86 /O2: a constant-folded inline call loses a bool member

Found while building `tooltip.h`'s pure `tooltip_step`. It is a property of the toolchain,
not of the tooltip: any inline function that returns a struct by value can meet it.
Method: a standalone repro compiled at the project's flags, compared against the same
function called with runtime values.

## The defect

Compiler: `cl.exe` version **19.50.35728**, x86, at the project's own flags
`/nologo /O2 /W3 /EHsc /MT /MP`.

Given an `inline` function that RETURNS A STRUCT BY VALUE, where that struct has a
`bool` member, the optimiser constant-folds a CHAIN of calls to it whenever every argument
at every call site is a compile-time literal. The fold is wrong: the byte-sized `bool`
member comes out of it holding the wrong value. The out-of-line copy of the same function,
compiled from the same source, is correct.

The shape that triggers it is exactly the shape a test block is written in -- a run of
calls that thread one state value through, each with literal arguments:

```
State s = begin();
s = step(s, 7, false, 1000, 0, 0.0);
s = step(s, 7, false, 1400, 0, 0.0);
...
```

It reached this project as a suite failure with no wrong code behind it: the checks read a
`bool` field back as the wrong value, while the same function called with runtime values
answered correctly.

## What it is NOT

Each of these was ruled out by measurement:

- **Not the CHECK macro.** A 55-line repro with no project headers at all -- its own
  struct, its own `inline` step function, its own literal-argument chain, and a plain
  `printf` of the field -- fails the same way.
- **Not branchiness.** The repro's step function is a handful of lines.
- **Not the DLL.** The panel calls these functions with runtime values (the wall clock, the
  hovered row, the frame delta), so nothing folds on that path and the shipped code was
  never at risk. The defect is reachable only from a fully literal call chain.

## The two patterns that are safe

Either one fixes it; both were measured to pass the same repro.

1. **Do not put a `bool` in a struct an inline function returns by value.** Widen the flags
   to `int` (0/1) and compare them against 0/1. This is why `entry.h`'s `EntryState::open`/`edited` and `EntryResult::commit` are `int`. It
   costs nothing and needs no compiler-specific attribute.
2. **`__declspec(noinline)`** on the returning function, which forces a real call and
   leaves the optimiser nothing to fold. This is what `tooltip.h`'s `tooltip_step` carries,
   because `TooltipState` already had `bool` members and widening them would have touched
   every call site in the suite. Its cost is one non-inlined call per frame.

Routing an argument through `volatile` also passes the repro, but it is a worse fix than
either of the two above: it hides the workaround at the CALL sites rather than stating it
once at the definition.

## When this can be removed

Keep whichever guard a file carries until a toolchain build is **measured** to pass the same
repro without it. 19.50.35728 is the build it was measured on.

## Open questions

- The first and last affected compiler builds are unknown.
