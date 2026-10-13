# OpenSystem

The Team's System preferences. Its first page is **Stack**: the adaptive
stack's default cap, and the programs with a cap of their own. MIT, Copyright
(c) 2026 Dalsin Limited.

Decided 13 October 2026: a new Stack page in the Team's System prefs owns the
cap and the per-program overrides (one voice). The `STACKCAP` tooltype is
honoured as the program's own request. The adaptive stack itself is in
amigachrome `docs/design/Design-Adaptive-Stack.md`.

## Where it lives

- **A tenth editor, System.** None of the nine combined editors
  (`docs/combined-prefs/DESIGN.md`) is about how the system runs programs, so
  the stack does not fit under Appearance, Desktop or the others. System is the
  home for settings of that kind: the stack first; later, for example, the
  task guard and the CPU cores board.
- **Pages.** One page now (Stack). With a second page, the window takes tabs,
  as OpenLook does.
- **Installed** as `SYS:Prefs/OpenSystem`.

## 0.1 (13 October 2026): the Stack page

| Setting | What it does |
| --- | --- |
| Default cap (KB) | How deep any program's stack may grow. 1024 KB unless changed. Rounded up to 64 KB chunks, 64 KB to 64 MB |
| Programs with a cap of their own | A program's name, as the Shell runs it (its command's file part, `StackGrow`), case aside, and its cap. Up to 32 |
| The runtime line | What the runtime does now: grow, watch only or off; the cap; the programs on growing stacks now; the stack alerts since boot |

- **Use** writes `ENV:OpenAmiga/Stack` and hands it to the runtime at once.
  **Save** writes `ENVARC:` as well. **Cancel** changes nothing.
- **The file** is short text: `CAP 1024`, then a `PROGRAM <name> <kb>` line for
  each program.
- **To the runtime.** On AmigaChrome the editor writes the memory controller's
  stack registers (Dalsin product 12, or 14 on the AA+ board, at `$900` on the
  board: amigachrome `stackwatch.c`). Where there is no such board, the
  settings are kept for when there is.
- **At boot**, `OpenSystem APPLY` in User-startup hands `ENV:OpenAmiga/Stack`
  to the runtime. Until OpenUp puts that line in, the runtime starts with its
  own default (1 MB, or `AC_STACK_CAP`).

From the Shell:

```
OpenSystem [APPLY] [INFO] [CAP kb] [PROGRAM name] [PCAP kb] [USE] [SAVE]
```

- `APPLY`: `ENV:OpenAmiga/Stack` to the runtime.
- `INFO`: the runtime line, the stack range and the settings for Use.
- `CAP`, and `PROGRAM` with `PCAP` (0 removes it), with `USE` or `SAVE`.

Tested 13 October 2026 in a lab copied from Instance-32 (OS 3.2, runtime from
amigachrome `runtime/adaptive-stack-20261013`): `OpenSystem CAP 512 PROGRAM
StackGrow PCAP 2048 SAVE`, then a recursion 1500 KB deep completed on a growing
stack and one 3000 KB deep stopped alone with the stack alert; `INFO` showed
the runtime's cap and counts; the window opened on Workbench with the list.

## Next

- **The `STACKCAP` tooltype.** The program's own request, read when Workbench
  starts it. That needs the Team's dos, or the runtime reading the icon at
  adoption (open).
- **APPLY at boot**: OpenUp to add the line to User-startup.
- **The window's look**: OpenLook's tabs and theme when a second page comes.
- **Combined prefs**: System to be added to the editors' list.
