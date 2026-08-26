# M5 — the frame loop, and the emulator that found a real bug first

M5 is the first build that keeps running. Before any of it, this session
installed DuckStation, which had been on the carried-forward list since M0 for
GPU fill rate and since M3 for the exception handler. It settled both, and
before either it found a fault in the port that three milestones of clean runs
had hidden.

---

## 1. DuckStation, and why the runs before it proved less than they looked

`F:\DuckStation`, in **portable** mode: `portable.txt` next to the executable
makes the user directory the program directory, which matters here because this
machine's `Documents` is redirected into OneDrive and a first run without it
lands the settings somewhere the emulator itself then cannot find. BIOS images
go in `F:\DuckStation\bios`; the disc has no licence string (`make_cd.py` does
not write one), so **Fast Boot is mandatory** — a retail BIOS refuses the disc
otherwise.

```sh
"F:/DuckStation/duckstation-qt-x64-ReleaseLTCG.exe" \
    -batch -fastboot d:/Homebrew6/PSX-LBA/build/lba1psx.cue
```

The settings that matter, in `F:\DuckStation\settings.ini`:

| | |
|---|---|
| `[CPU] ExecutionMode = Interpreter` | same reason as PCSX-Redux (M3 §5) |
| `[BIOS] PatchFastBoot = true` | no licence string on our disc |
| `[BIOS] TTYLogging = true` | BIOS `putchar` into the log |
| `[Logging] LogToFile = true` | **the TTY becomes a file** |

That last line is the workflow change. PCSX-Redux writes its TTY to the console
handle, which shell redirection cannot capture (M2's gotcha list); DuckStation
writes `duckstation.log`, so the port's own diagnostics can be read, grepped and
diffed without watching a terminal. Every number below came out of that file.

## 2. The bug: `mfc0` has a load delay slot

The port did not boot at all on a retail BIOS. It died inside
`PORT_ExcInstall`, and the report it printed was internally inconsistent — a
Cause of 0 (an interrupt, which the vector chains and never reports) arriving
through the path that only a genuine fault can reach.

An entry-time snapshot of the four COP0 registers (`-DPSX_EXC_ENTRYLOG=ON`,
kept) came back **shifted by one slot** against the same registers read a few
instructions later. That is the whole answer:

```
    mfc0    $k1, $13        /* Cause                     */
    andi    $k1, $k1, 0x7c  /* <- reads the OLD $k1      */
```

On the R3000A a move from coprocessor 0 has a load delay slot exactly as `lw`
does: the destination is not readable by the next instruction. So the triage at
the top of the vector was testing **whatever `k1` happened to hold**, not Cause.
When that leftover was a multiple of 4 in the low seven bits the exception
chained correctly by accident; when it was not, an ordinary interrupt fell into
the fault path and halted the machine. In the failing build the leftover was 4,
from the instrumentation's own counter — the three exceptions before it had
chained on luck and the fourth did not.

Every `mfc0` in `psx_exc.S` now has a `nop` behind it.

**Neither PCSX-Redux mode models this.** Not the recompiler, and not the
`-interpreter` that M3 switched to precisely because the recompiler was hiding
alignment faults. DuckStation models it, and named it on the first run. The M3
rule stands and gets a second half: *run under `-interpreter`, and run under
DuckStation as well, because the two emulators hide different things.*

The self-test then closed the item M3 left open — the fatal path had never been
seen to fire, because PCSX-Redux with OpenBIOS answered a real address error
from its own handler and halted before ours. On DuckStation with a retail BIOS:

```
*** EXCEPTION *** AdEL     address error on LOAD (unaligned or bad)
    epc      8001102c        badvaddr 80032979
    cause    30000010        sr 00000404
    -> odd address: a halfword or word read off a byte stream
    the four words at epc: 8c420000 ...          <- lw $v0, 0($v0)
    exceptions through the vector so far: 9148
```

Right code, right address, right instruction, and 9148 interrupts and syscalls
chained through the vector before it without a scratch. The handler is an
instrument now, not a claim.

## 3. The same scene, on a second emulator

Cube 0, retail BIOS, software renderer, interpreter:

| | PCSX-Redux + OpenBIOS | DuckStation + SCPH-1001 |
|---|---|---|
| heap at the scene | 1535 KB | **1535 KB** |
| HQM used | 141936 | **141936** |
| `ChangeCube` | 3894 ms | **17012 ms** |
| `AffGrille` | 588 ms | **461 ms** |
| `Flip` | 122 ms | **119 ms** |
| the actor, transformed and emitted | ~95 ms | **94 ms** |

Everything the CPU decides agrees. Everything the CD decides does not, and the
gap is 4.4x.

**`ChangeCube` is 17 seconds.** PCSX-Redux's drive model is optimistic;
DuckStation's models seek latency, and `LoadUsedBrick` does one seek per brick
over a 3.9 MB archive. Seventeen seconds to enter a room is not a load time, it
is a defect, and it moves the M7 drive-contention work forward: the archive
layout has to change, not just the streaming policy.

**The actor costs 94 ms and both emulators agree.** M4 declined to report this
figure because PCSX-Redux models no GPU timing — but the two now agree to within
1%, which says the cost is not in the GPU at all. It is 129 primitives against a
20 ms frame at 50 Hz: **one actor is 4.7 frames**. Whatever M5's frame loop
does, it does not start from a working budget, and the first job inside it is to
find where 94 ms goes for 129 primitives.
