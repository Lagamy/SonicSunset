# Editor keybinds

All keys of the in-game editor. "Mesh subtabs" means the HubWorld **Geometry**, **Texturing** and **Collision** subtabs. Keys do nothing while a text field has the keyboard.

## Camera (editor view)

| Input | Action |
|---|---|
| W / A / S / D | Fly forward / left / back / right |
| Space | Fly up |
| Alt | Fly down |
| Shift (held) | Fast fly (full speed, no slowdown near things) |
| Right mouse (hold) + move | Look around (in the mesh subtabs: Alt + right mouse, since right click picks there) |
| Middle mouse (hold) + move | Pan; in the mesh subtabs it turns the camera about the middle of the open mesh |
| Scroll | Zoom / dolly (orthographic views: change the view's height) |

## Editor (all tabs)

| Input | Action |
|---|---|
| Left click | Select the entity under the cursor (a miss keeps the selection) |
| Shift + left click | Add to / remove from the selection (in the HubWorld view: the level) |
| Ctrl + Z | Undo (not in the mesh subtabs) |
| Ctrl + Y | Redo (not in the mesh subtabs) |
| Ctrl + C / Ctrl + V | Copy the selected entities / paste at the cursor |
| Ctrl + S | Save all levels (not in the mesh subtabs) |
| Delete | Delete the selected entities (not in the HubWorld view or the mesh subtabs) |
| 1 / 2 / 3 | Gizmo mode: Move / Rotate / Scale (not in the mesh subtabs) |
| Gizmo handle (drag) | Move / rotate / scale the selection |

## Always (editor and playing)

| Input | Action |
|---|---|
| Home | Show / hide the collision lines view |
| V | Collision lines: colour by attribute / by slope |
| Left / Right | Collision lines: move the highlighted attribute along the legend (the Entities legend while the debug UI shows its Entities tab) |
| Up / Down | Switch the labels over entities (entity / mesh / collision names) |
| P | Print what the view ray through the screen centre first hits (in the Geometry and Collision subtabs P switches Create / Transform instead) |

## Playing

| Input | Action |
|---|---|
| Tab | Open / close the debug menu (frees the cursor) |
| Esc | Stop playing, back to the editor |

## Asset panel

| Input | Action |
|---|---|
| Ctrl + C / Ctrl + V | Copy the selected script tile / paste it into the open folder |
| Double click a texture | Open it |

## Mesh subtabs (Geometry, Texturing, Collision)

Ctrl is the merge key here, so undo/redo/save use **Shift**.

| Input | Action |
|---|---|
| Shift + Z | Undo one mesh edit (a move, created vertex or face, delete, texture or UV edit) |
| Shift + Y | Redo |
| Shift + S | Save |
| 1 / 2 / 3 / 4 / 5 / 6 | Orthographic view along -Z / +Z / -X / +X / down / up (any camera look or fly key leaves it) |
| Middle mouse (hold) | Turn the camera about the middle of the open mesh |
| Alt + right mouse (hold) | Look around |
| Alt | Fly down |
| I | Isolate the open mesh (hide everything else); again to leave |
| P | Geometry and Collision: switch between the Create and Transform tabs |
| Q | Deselect (also drops an unfinished triangle) |
| Esc | Discard what is half done (the triangle being drawn, pending extrusions) |
| Delete | Delete the selection |

### Picking (Transform and Faces tabs, and Texturing)

| Input | Action |
|---|---|
| Left click | Pick (replaces the selection) |
| Shift + left click | Toggle in the selection |
| Ctrl + left click | Add to the selection (not while extruding, not in Create's Vertex mode) |
| Left drag | Box pick (Shift or Ctrl adds) |
| Right click | Take one out of the selection |
| Right drag | Take everything in the box out |
| Move gizmo (drag) | Move the selection; let go near a vertex or edge and it merges (green) |
| Ctrl (held) | No merging: the preview, clicks, Enter and the end of a move leave vertices apart |

### Create tab

| Input | Action |
|---|---|
| Left click on a vertex | Start a triangle; further clicks add corners (empty space makes a new vertex; Shift on the third goes on from it) |
| Right click | Step back one corner; cancel the pending extrusion or the picked vertex / edge |
| E | Extrude the picked edge (or two picked vertices) as a Triangle or a Quad |
| \ | Switch Triangle / Quad (pending extrusions are made again in the new kind) |
| Enter | Make the faces of the pending extrusions (with none: a triangle from three picked vertices) |
| Q / Esc | Deselect / discard (see above) |

### Texturing window

Drag in the texture view to move the picked faces' UVs; drag a corner handle to scale; drop a texture from the Asset panel on the window to assign it. Undo with Shift + Z.
