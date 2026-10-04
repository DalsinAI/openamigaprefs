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

## 4. Phases

| Phase | Delivers | Done when |
| --- | --- | --- |
| 1 | The types list, choosing a program, applying it to default icons; GadTools window | Pictures without icons open in OpenView after one change |
| 2 | Scanning drives, updating existing icons with a list, backup and Undo | A drive of PNGs with MultiView as default tool switches to OpenView and back |
| 3 | Open with…, as a commodity; types by file name pattern | Open with… on a selected icon, and "Always use this" |
| 4 | The catalogue of programs per type; an Installer; Aminet (util/wb) | |
