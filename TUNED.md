# tuned/dynarec-r2

Test branch: 86Box master with two open recompiler pull requests stacked on top, tested together on an ARM64
host (Raspberry Pi 5) and an x86-64 host. It is not an official 86Box build and not meant for merging as it is.

This is the second version of `tuned/dynarec`, rebuilt on the PR heads their author rebased onto master after
PR 9102. `tuned/dynarec` stays as it was (tag `tuned/dynarec-2026-11-25`).

## Contents

| | head |
|---|---|
| master | `66e7cc5c8` |
| PR 8829 "New dynarec: aarch64 performance work" | `8d36721f9`, all 12 commits |
| PR 8917 "CPU/dynarec: performance fixes for SMC, links, code memory, segment loads and the interpreter" | `ef3c22650`, 11 of its 35 commits |

The same choices as in `tuned/dynarec`: where the two PRs reimplement the same code (block lookup, code memory,
link routine), this branch keeps PR 8829's version and takes PR 8917's commits that stand on their own: #AC and
PSE-36, interpreter overhead, segment descriptor reads, small x86-64 code generation changes. Three of those were
adapted to PR 8829's code; their messages say how. Every commit keeps its author and names its original commit.
PR 8829 no longer has its own segment-load commit, so PR 8917's is taken as it is.

The commits are ordered by topic. Tags `tuned/topic-r2/NN-name` mark the last commit of each topic, and every tag
builds and boots.

## Tested (2026-11-25/26)

- Same speed as `tuned/dynarec` on both hosts, 3 interleaved rounds each: WinBench 96 CPUmark16 on a 486 DX4-100
  and a Pentium 66 (Windows for Workgroups 3.11) and WinBench 99 CPUmark32 on a Pentium 100 (Windows 98 SE) on the
  Pi; 3DBench, Landmark 6, PCPBench and Doom on the DX4-100 and the Pentium 66 on the Pi; WinBench 99 CPUmark32 and
  FPU WinMark on a Pentium MMX 233 and a Pentium II 350, Quake II, GLQuake (Voodoo 2) and the 3DMark99 race scene on
  x86-64. The differences stay within a few percent and go both ways. Scores are the same.
- DOS on a Pentium 66 (3DBench, Landmark 6, PCPBench, Doom) and the intros of Wing Commander III, Wing Commander IV
  and Star Trek: The Next Generation - A Final Unity on a 486 DX2-66, a DX4-100 and a Pentium 66, Pi: real time in
  every run, at about a third of a host core.
- Stability: 100+ WinBench 96 runs on the Pi without a guest fault; 160 boots of the WfW VM, 100 Quake II/GLQuake
  runs, REP string checksums, protected-mode page fault tests 20/20 and the PR 8829 boot repro set 8/8 on x86-64.
- Every topic tag builds and boots the WfW VM.
