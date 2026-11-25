# tuned/dynarec

Test branch: 86Box master with two open recompiler pull requests stacked on top, tested together on an ARM64
host (Raspberry Pi 5) and an x86-64 host. It is not an official 86Box build and not meant for merging as it is.

## Contents

| | head |
|---|---|
| master | `de50f9adf` |
| PR 8829 "New dynarec: aarch64 performance work" | `3bff46c28`, all 13 commits |
| PR 8917 "CPU/dynarec: performance fixes for SMC, links, code memory, segment loads and the interpreter" | `225af56f7`, 11 of its 32 commits |

The two PRs reimplement some of the same code (block lookup, code memory, link routine) in different ways. Where
they overlap, this branch keeps PR 8829's version and takes PR 8917's commits that stand on their own: #AC and
PSE-36, interpreter overhead, segment descriptor reads, small x86-64 code generation changes. Four of those were
adapted to PR 8829's code; their messages say how. Every commit keeps its author and names its original commit.

The commits are ordered by topic. Tags `tuned/topic/NN-name` mark the last commit of each topic, and every tag
builds and boots.

## Tested (2026-11-24/25)

- WinBench 96 CPUmark16 on a 486 DX4-100 (Windows for Workgroups 3.11), Pi 5: as fast as PR 8829 alone (host CPU
  per emulated second 0.98 vs 1.22 on master), same score.
- WinBench 99 CPUmark32 and FPU WinMark on a Pentium MMX 233 and a Pentium II 350 (Windows 98 SE), x86-64: as
  fast as PR 8829 alone (CPUmark32 cost −37 % vs master at the PII 350), same scores.
- WinBench 99 CPUmark32 on a Pentium 100 (Windows 98 SE), Pi 5: host CPU per emulated second 1.27 vs 1.29 for
  PR 8829 alone and 1.91 on master (77 % vs 52 % speed), same score.
- DOS benchmarks on the 486 DX4-100 (3DBench, Landmark 6, PCPBench), Pi 5: real time on every build; PCPBench and
  Landmark cheaper than master, 3DBench about 5 % more expensive (as with PR 8829 alone).
- Quake II, GLQuake (Voodoo 2) and the 3DMark99 race scene on a Pentium MMX 233 and a Pentium II 350, x86-64: same
  cost, scores and end screens as PR 8829 alone.
- 150 WinBench 96 runs on the Pi without a guest fault; 410 boots of the WfW VM on both hosts, one of which stopped
  in POST after the configuration screen (not reproduced since).
- REP string checksums identical to master (dynarec and interpreter), protected-mode page fault tests 20/20, the
  unit tests that build on master pass.
