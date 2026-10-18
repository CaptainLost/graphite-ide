# 0608. Make a new file only once its name is entered, and offer a dialog with templates beside it

Status: accepted
Date: 2026-10-18

## Context

**New File** in the file tree made `new file.txt` on disk at once and opened the rename field on it. The name was a guess made before the user had said anything.

- **Escape left the file behind.** Cancelling the rename ended the editing, not the file, so the folder kept a `new file.txt` nobody asked for.
- **The extension was fixed.** `.txt` is wrong in a C++ or C# project, and the stem was the only part selected, so every new file began with a rename of the extension too.
- **The file opened after the rename.** `m_OpenWhenNamed` carried the path across the rename so that opening the tab did not take the keyboard from the field. It was state that existed only to patch the order of events.
- **A class is more than one file.** A C++ class is a header and a source, and neither starts empty.

## Decision

Offer two commands, in the context menu of a folder, a file (for its folder) and the empty space of the tree.

- **New File** adds a row at the end of the folder's children with a name field in it. Nothing is made until the name is entered. Enter makes the file and opens it in a permanent tab. Escape, or an emptied name, ends the field and leaves the folder as it was. Leaving the field with a name in it enters the name, as a rename does.
- **New File from Template…** opens a modal question, through `ModalQuestion`, with a template and a name. The templates are `Empty file`, `C++ class` (a header with `#pragma once` and a class, and a source including it) and `C# class` (a file-scoped namespace and a class). Create is enabled only for a name that fits the template and names nothing that exists.
- **A typed name may carry folders.** `ParseNewFilePath` splits it on `/` and `\`, and every piece must pass `Core::IsValidFileName`. `util/text/Strings.cpp` makes `util` and `text` if they are missing. A piece that is empty, `.` or `..` refuses the whole name, so a name cannot climb out of the folder.
- **A refused name keeps the field.** A name that is invalid or taken is reported in a toast, and the field opens again with what was typed.
- **The C# namespace** is the folders from the workspace root down, each cleaned into an identifier and joined by dots; a file at the root takes the root folder's name.
- **The name is claimed before it is filled.** `CreateFiles` makes each file with `Core::CreateEmptyFile`, which fails on an existing path, and only then writes the content with `Core::WriteFileAtomically`, which would replace one.
- **New Folder is unchanged.** It still makes the folder and renames it.

## Rationale

**The name is what the user has to say.** A field that appears where the file will be answers the question the user is already asking, and cancelling it costs nothing because nothing was made. The dialog is a separate command because choosing a template is a different intent from naming a file, and putting it in the field would make every plain new file pay for it.

## Consequences

### Positive

- Escape leaves nothing on disk, and no file starts with a guessed extension.
- `m_OpenWhenNamed` and `OpenIfJustCreated` are gone: the file opens when it is made.
- A class in C++ or C# starts with its boilerplate in place.
- `a/b/c.txt` makes the folders, so a nested file needs no New Folder first.

### Negative

- The row of the field is not a node of the tree, so it is not part of what the tree reports about its rows, and it is drawn only while its folder is open.
- A folder with a filter active shows the field only if the filtered tree holds that folder.
- The template list is fixed in code. A user cannot add one.

### Neutral

- `New Folder` and `Duplicate` still create first and rename after.
