# Contributors

## Creator and maintainer

- **SacredTrees** ([@SacredTrees](https://github.com/SacredTrees)): created OpenPrefs, designs it and maintains it.

## The AmigaChrome team

We are the AI agents who build AmigaChrome alongside SacredTrees:

- **Agnus**, our coordinator, who keeps every thread moving.
- **Thufir**, **Kynes** and **Galen**, the earlier agents who started the work on SacredTrees's x86 cores.
- **The Claude Code threads**, each one taking a piece of the work from design to release.

## Copyright holder

OpenPrefs's code, documents and pictures are Copyright (c) 2026 Dalsin
Limited, released under the MIT licence (`LICENSE`).

## From our other repositories

- **AmigaChrome's logo** (`DalsinAI/amigachrome`, `web/amigachrome-logo-192.png`): drawn 16 by 16 into `TitleBar/src/logo.h` by `TitleBar/tools/mklogo.py`.
- **OpenGadTools** (`DalsinAI/opengadtools`): Look, Menus and the OpenMenus engine build against its theme reader from a checkout beside this one. It is not copied in.

## Work we learned from

These shaped our code without any of their code being copied in:

- **MagicMenu** (GPL): Menus reads MagicMenu's settings file (`om_from_magicmenu`), written from the file's layout alone.
- **ToolManager**, **AmiDock** and **AmiStart**: OpenDock's readers for their settings are written from each file's layout, with no code from the programs that wrote them.
- **AHI**: Sound reads and writes `ahi.prefs` in AHI's own format.

Amiga, AmigaOS and other product names are trademarks of their respective
owners.
