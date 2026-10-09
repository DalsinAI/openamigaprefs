<!-- OpenTypes' design, written in AmigaChrome's development tree on 4 October 2026. Copyright (c) 2026 Dalsin Limited, MIT licence (../LICENSE). -->

# OpenTypes

A preferences tool for AmigaOS 3.x that says which program opens each kind of
file, and applies that to the icons, so default applications are as easy to
set as on a modern desktop.

We, 4 October 2026: "we also want to create a new preferences tool that
lets us update the filetype in icons to make default applications easier,
the current solution is a bit... pants".

**OpenTypes** (a working name, our to change) is the first of OpenPrefs,
our preferences editors, in the repository `DalsinAI/openamigaprefs`. MIT,
Copyright (c) 2026 Dalsin Limited, credit kept. GadTools, as OS 3.x
applications are.

## 1. What OS 3.2 has, and what is missing

- **Types come from datatypes:** `DEVS:DataTypes` describes each kind of file
  (ILBM, PNG, JPEG, ASCII, AmigaGuide, 8SVX and so on).
- **Default icons:** a file without an icon gets `ENVARC:Sys/def_<type>.info`
  (def_ascii, def_iff, def_archive and others), whose default tool opens it.
  The DefaultIcons editor changes those icons' looks.
- **Each icon carries its own default tool,** set by hand through Icon
  Information, one icon at a time.
- **Missing:** one place to say "pictures open with OpenView" and have it
  apply: to the type's default icon, and to the icons files already have that
  still name the old program. There is no "Open with…", and no list of the
  programs that can open a type.

## 2. What OpenTypes does

- **The types:** every datatype and every default icon, with the program that
  opens it now, how many icons on the chosen drives use which program, and
  which programs can open it.
- **Choose a program per type,** with an ASL requester or from the programs
  known to open that type. Known programs come from a small catalogue
  (OpenView for pictures and documents, MultiView, OpenPrint, OpenMail,
  OpenBrowser, players and editors) and from what the scan finds in use.
- **Apply:**
  - the type's default icon (`ENVARC:Sys/def_<type>.info` and `ENV:`) gets
    the new default tool, so files without icons open with it;
  - existing icons of that type that name the *old* default program are
    listed, and updated on Apply. An icon someone set to a different program
    on purpose is left alone unless ticked. Every icon changed is backed up
    first, and Undo puts them back.
- **"Open with…":** a small commodity adds Open with… to Workbench's Tools
  menu (an AppMenuItem). For the selected icons it offers the type's programs;
  "Always use this" makes it the type's default.
- **More than datatypes:** types by file name pattern too (`#?.lha`, `#?.adf`,
  `#?.html`), for files datatypes do not recognise. So OpenCompressor opens
  archives and disk images, and OpenBrowser opens HTML.
- **Use, Save, Test and Cancel**, as the OS's own editors have; settings in
  `ENVARC:OpenTypes/`, and the OS's own files (the default icons) for what
  the OS already has.

## 3. Rules

- **No patches.** OpenTypes edits icons with icon.library (`GetDiskObject`,
  `PutDiskObject`, `GetIconTagList` and `PutIconTagList`), never by hand.
- **Never a hard delete or a silent change:** every changed icon is listed
  first and backed up.
- **The OS's DefaultIcons editor keeps the looks;** OpenTypes deals with the
  default tools.
- **AROS:** the same, on AROS's default icons.

## 4. Built: OpenTypes 0.1 (5 October 2026)

Phases 1 and 2, in `src/opentypes.c` (`build.sh` builds it with the os32
stove):

- **The window** (`docs/window.png`): the kinds of file (the OS's project
  default icons) with the program for each; Opens with, Choose... (ASL) and
  the programs known to open the kind; Find icons that still name the old
  program..., with a list to tick; Save, Use, Undo all, Cancel.
- **The Shell**, for scripts and tests: `OpenTypes LIST`,
  `SET <type> TO <program> [SAVE]`, `SCAN <type> IN <drawer> [TO <program>
  APPLY]`, `TYPE <file>`, `UNDO`.
- **Tested on OS 3.2.3** (a scratch instance, two PNGs whose icons named
  MultiView):
  - it found both as pictures;
  - it changed their icons and the picture default icon to OpenView;
  - Undo put the three icons back, leaving the drive identical, file for file.
- **Found:** commands run from a script have 4 KB of stack. Undo overflowed
  it, so large buffers are static.

## 4b. OpenTypes 0.2 (6 October 2026): SWITCH

We, 6 October 2026: replace MultiView with OpenView, keeping MultiView.

- `OpenTypes SWITCH <program> TO <program> [SAVE] [IN <drawer>]` moves every
  kind of file whose default icon names the first program to the second.
  Sounds, music, video and fonts keep their program, since OpenView doesn't
  play them. A kind someone set to another program on purpose is left alone.
- With `IN`, icons under that drawer that still name the first program, and
  whose kind was switched, change too. They're found first and changed after
  the walk.
- Every change is backed up and listed like SET's, so `UNDO` puts it all back.
- OpenUp runs `SWITCH MultiView TO SYS:Utilities/OpenPrint/OpenView SAVE`
  when it installs OpenPrint (the kinds of file only, no `IN`: icons that are
  already there keep their own default tool), and `UNDO` when it is
  uninstalled.

### 4c. OpenTypes 0.2.1 (9 October 2026): the program compare

0.2 compared two tool names as `Stricmp(base(a), base(b))`. GCC 6.5 called
utility.library's Stricmp with A6 still holding dos.library (the second
`base()`, which calls dos.library's FilePart, had set it), so the call went to
a dos.library vector, returned 0, and every icon and every kind of file
"named" the first program. OpenUp 0.6.14 to 0.7.0 (`SWITCH MultiView TO
OpenView SAVE IN SYS:`) so changed the default tool of about 40 of AmigaOS
3.2.3's own icons (datatype descriptors, pointer presets, DOSDrivers, the
Picasso96 guides, Help) and of 16 default icons to OpenView.

- `same_program()` compares two names with a plain loop, no library call
  (`opentypes.c`, used by SCAN, SWITCH IN and SWITCH).
- `tests/check_stricmp_a6.py` builds OpenTypes and fails when any call of
  Stricmp in the machine code is made with another A6 than UtilityBase.

## 5. Phases

| Phase | Delivers | Done when |
| --- | --- | --- |
| 1 | The types list, choosing a program, applying it to default icons; GadTools window | Pictures without icons open in OpenView after one change |
| 2 | Scanning drives, updating existing icons with a list, backup and Undo | A drive of PNGs with MultiView as default tool switches to OpenView and back |
| 3 | Open with…, as a commodity; types by file name pattern | Open with… on a selected icon, and "Always use this" |
| 4 | The catalogue of programs per type; an Installer; Aminet (util/wb) | |
