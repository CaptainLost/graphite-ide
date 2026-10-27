# 0649. Open images as views inside the document tabs

Status: proposed
Date: 2026-10-26

## Context

- A png, jpeg, gif, bmp or ico file opened from the Explorer showed only
  `Binary file — N KB`: `Core::DocumentSet` opens every file as text and fails
  one that probes as binary.
- There is no shared tab interface. Each owner docks its own ImGui windows into
  the centre node ([0494](0494-correct-adr-0493-s-premise-about-document-tabs.md)),
  and `DocumentWindows` already gives its tabs preview tabs, closing, the
  Explorer's open marks, the git dot and restoring after a restart.
- A picture is decoded off the UI thread and drawn through textures that must be
  released on it ([0646](0646-draw-pictures-through-a-texture-facade-over-imgui-s-user-textures.md)).
  A landed load or a gif's next frame frees the texture shown before it.
- The layer routes every key as `Global`. Only the view holding the keyboard
  routes the frame's queued strokes as `FocusedEditor`, and only the text editor
  did so, for a tab with a document. Close, Save and the editing commands are
  `FocusedEditor` commands.
- The zoom chords Ctrl+=, Ctrl+− and Ctrl+0 are global and change the editor's
  font. Their Ctrl+wheel alternates fire only when a text editor routes the
  wheel it is under. A menu takes the keyboard focus before its item runs.

## Decision

- **An image item in `Core::DocumentSet`.** `OpenItem` carries a `View`, an
  `ItemView` of `Text` or `Image`. An item whose file probes as binary and whose
  extension names an image format (`Core::ImageFormatOfPath`: png, jpg, jpeg,
  gif, bmp, ico) loads as `View == Image`, with no document and no failure; any
  other binary file still fails as `Binary`. Only a NUL byte makes a file
  binary, so a text file under an image name, such as a Git LFS pointer, opens
  as text.
- **Kept in `DocumentWindows`.** An image item is drawn by the same window loop
  as a text item. Where the loop would draw the path row and the editor, an
  item with `View == Image` and no failure calls `DrawImageTab`
  (`Graphite/src/Image/ImageTabView.h`). An image item whose file went missing
  draws the failure message, as a text tab does.
- **One owner for the pictures.** `ImageTabs` (`Graphite/src/Image/ImageTabs.h`)
  holds one `ImageTab` per open image path, spelt as the item spells it, and the
  shared `PictureBudget`. `GraphiteLayer` makes it with the shared job queue and
  `Core::ImGuiRenderer::GetTextures()`, and `DocumentWindows` holds it by
  reference. A tab's picture is made when the tab first draws and dropped when
  the tab closes.
- **Updated before anything draws.** At the start of each frame every picture is
  updated, as shown when it was drawn last frame and as hidden otherwise, and
  the budget then releases what it names, never a picture on screen. A tab that
  comes on screen asks for one more frame, on which its load starts.
- **Disk changes.** `HandleFilesChanged` and the check on regaining focus ask the
  picture to compare its file's stamp, as they ask a document.
- **Restart.** `CaptureState` keeps an image tab as `TabState{Path, Preview}` and
  may name it the active tab; `RestoreState` reopens it through the ordinary
  open path.
- **Close.** The close command closes the active tab whatever it shows, a
  picture or a file that could not be read included. An image tab whose window
  holds the keyboard routes the frame's strokes as `FocusedEditor` through
  `RouteFocusedEditorStrokes` (`Graphite/src/Editor/TextEditorWidget.h`), the
  routing the text editor uses, so Ctrl+W and Ctrl+F4 close it. The other
  `FocusedEditor` commands are disabled there, since the tab holds no document.
  A popup open over the tab, such as its zoom menu, does not count as the tab
  holding the keyboard.
- **Status bar.** The items a picture's view drew last frame are what
  `GetViewStatusItems` hands the bar when that tab is active
  ([0647](0647-let-the-view-on-screen-add-its-own-items-to-the-status-bar.md)).
- **Zoom chords and the zoom menu entries.** `EditorSettings::SetPictureZoom`
  takes a redirect that the three zoom commands ask first; they change the font
  only when it answers false. `DocumentWindows::ZoomFocusedPicture` answers true
  for the tab whose window held the keyboard focus last frame when it shows a
  picture. When no document tab held the focus, because it sat in a menu, a
  popup or a panel, it answers true for the active tab if that tab shows a
  picture on screen. Zoom In and Zoom Out step the picture around its area's
  centre and Reset Zoom fits it, from a chord and from the View menu alike. It
  answers false while the wheel turns, because a zoom command then came from
  Ctrl+wheel over a text editor; the picture takes the wheel itself.

## Rationale

Keeping the picture as one more kind of item keeps every tab behaviour the
editor's tabs already have, where a window owner of its own would have copied
them. Updating every picture before any is drawn is the one order in which no
draw command can point at a texture freed later in the same frame. Routing an
image tab's strokes through the text editor's routing, rather than a copy of it,
keeps one place deciding which strokes a focused view's commands take. A menu
holds the focus when its item runs, so the focused tab cannot say which picture
the user means there; the active tab is the one they last worked in.

## Consequences

### Positive

- An image tab previews, closes, restores and carries its git dot exactly as a
  text tab does.
- A hidden gif costs nothing and a picture off screen can be given up under
  memory pressure, since one place sees them all.
- View → Zoom In acts on the picture the user is looking at, as the chord does.

### Negative

- `DocumentWindows`, already past 2,200 lines, grows another branch, another
  injected dependency and three more pieces of per-frame state.
- A tab that comes on screen starts loading one frame late, because which tabs
  are on screen is known only as their windows begin.
- The wheel test in `ZoomFocusedPicture` reads ImGui's input: a zoom chord pressed
  in the same frame as a wheel turn zooms the font.
- A tab that shows a failure, a missing image's included, routes no strokes, so
  Ctrl+W does not close it, as for a text tab that could not be read; File →
  Close Tab does.

### Neutral

- An image tab that holds the focus takes the zoom chords even while its picture
  is loading or after it failed to load: nothing zooms and the font does not
  change.
- An image tab whose file is missing draws the failure message and shows no
  picture, so the zoom chords and the zoom menu entries change the font while it
  has the focus or is active.
- With the focus in a panel, such as the Explorer right after it opened an image,
  the zoom chords zoom the active tab's picture rather than the font.

## Alternatives Considered

### A `PictureWindows` owner of its own beside `DocumentWindows`

Rejected: preview tabs, closing, the Explorer's open marks, the git dot and
restoring would each be built a second time for the same files.

### Update each picture just before its tab draws

Rejected: tabs that are not drawn still need their update, so a hidden gif
stops asking for frames, and the budget has to weigh every picture at once. One
update for all, before any draw, serves both and leaves no order between tabs
in which a texture drawn earlier in the frame could be freed.

### Route the zoom chords through a scope of their own

Rejected: the keymap has no scope for "the focused tab shows a picture", and
adding one would change the keymap's model for three commands; a redirect asked
by the commands keeps the bindings and the settings page as they are.

### Let the zoom menu entries change the font, as before

Rejected: with an image tab in front, the entry would change text the user is
not looking at and leave the picture as it was.

## Related

- [0476](0476-load-a-tab-whose-file-appears-rather-than-only-clearing-its-missing-mark.md) —
  a tab whose file disappears and comes back.
- [0551](0551-open-a-result-with-one-click-model-shared-by-every-list-that-opens-files.md) —
  preview tabs, which image tabs share.
- [0645](0645-add-stb-as-a-dependency-for-decoding-and-scaling-images.md) —
  the decoder behind the pictures.
- [0648](0648-let-a-tab-s-view-put-its-tools-on-the-right-of-the-path-row-and-give-the-row-to-image-tabs.md) —
  the path row's tools, which an image tab fills.
