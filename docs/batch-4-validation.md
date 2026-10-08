# Batch 4: geometric selection and shared references

SC08 implementation is available; the issue remains open until the outstanding
Windows desktop clipboard check below is verified. SC09 guided questions and
event retrieval are subsequent work.

## Behavior

- Click picks a part, planar face, curved patch, sharp edge chain or corner.
  The bottom-right card chooses the mode and shows owner and model coordinates.
  Surface selection never presents tessellation triangles as analytic CAD faces.
- Picks respect visible parts, cross-part occlusion and instance ownership.
  Edges/vertices snap within eight logical pixels, including silhouettes.
  Distance ties use stable instance identity rather than tree ordering.
- Moving more than four logical pixels changes a viewport press into an orbit
  gesture; it cannot then become a selection click. Panel gestures, search,
  export dialogs and right-button drags do not select geometry.
- Amber overlays show selected geometry; blue agent highlights remain separate.
  Selection does not change export flags or the underlying model files.
- `selection --json` includes `data.selection.geometry`: owner/source/instance,
  kind, hit position, applicable normal, whole-feature bounds and a copyable token.
  `reference TOKEN` resolves identity against current viewer geometry without
  changing selection. A resolved position is labeled representative.
- Source/view changes reject old geometric tokens. Failed loads retain the last
  displayed selection and geometry, disable export, and reject reference lookup
  until recovery. Malformed tokens and topology mismatches fail explicitly.
- Degenerate topology falls back to Part mode with diagnostics. Oversized
  authored IDs retain context but cannot produce a bounded reference token.
- Copy ref and Ctrl+C use the native clipboard; unavailable clipboard access
  produces feedback directing the agent to `synthcad selection`.

Native scaling now uses Windows window DPI or X11 `Xft.dpi`, preserving the
existing physical-pixel canvas and converting input once. Font rasterization,
UI hit boxes, scissor rectangles and orbit thresholds use the same scale.
Notifications sit above the selection card; errors/help temporarily replace it.

## Verification

Windows MSVC Release and Ubuntu 24.04/GCC 13.3 Release builds pass all sixteen
C++ suites: the four existing viewer suites; project, CLI, transport, bridge,
knowledge and scene; topology and shared design; picker, reference codec,
selection UI and display scale. The Windows icon/reload smoke, standalone
eleven-topic guidance, and both existing live session/shared-design harnesses
also pass. Linux runs use raylib 6.0 with Xvfb/Mesa and an isolated container.

The new `scripts/test-geometric-selection.py` sends native messages directly to
its own viewer window, without moving the user's pointer or adding an input
command to the product CLI. It verifies all four pick modes plus curved patches,
source/instance ownership, hidden-part exclusion, camera drag separation,
independent highlights/export flags, token resolution, malformed input, failed
import retention/recovery, and reference invalidation after source/view edits.
It retains selected-vertex and curved-patch screenshots with `--keep-temp`.

Actual scaling checks (not forced-scale preview modes):

| Environment | Observed scale and client size | Result |
| --- | --- | --- |
| Windows native window | 125%, 1600 × 900 | Native input and reference checks pass |
| Linux X11, `Xft.dpi:144` | 150%, 1920 × 1080 | Native input and exact clipboard token pass |
| Linux X11, `Xft.dpi:192` | 200%, 2560 × 1440 | Native input and exact clipboard token pass |

Default 100% Linux and Windows runs passed before the scale adapter; current
Linux default-scale regression harnesses also pass. The 200% selected-patch
screenshot was visually checked for overlays, text, controls and notification
placement. Native Wayland and multi-monitor physical display transitions remain
outside this evidence and belong to SC19's platform matrix.

**Outstanding Windows check:** this execution environment rejects clipboard
access with error 5. The harness reports a skip, not a passing copy assertion;
the viewer reports clipboard unavailability. A normal Windows desktop must
confirm that Copy ref pastes the exact `scsel1.` token. The equivalent native
X11 ownership/readback check passes using a separate `xclip` client.

## Reproduction

```text
synthcad open path/to/synthcad.json --session review
synthcad selection -s review --json
synthcad reference COPIED_TOKEN -s review --json
python scripts/test-geometric-selection.py --cli PATH/TO/synthcad --viewer PATH/TO/dingcad_viewer --keep-temp
```

Linux native input testing requires `DISPLAY` and `xdotool`; `xclip` enables the
separate clipboard assertion. Use `--expected-scale 1.5` or `2` after configuring
Xft DPI in an isolated X server; the test fails if that scale was not applied.

CPU-only Linux observations: a 32,768-triangle sphere surface pick averages
0.74 ms, a 256-segment circular rim 0.043 ms, and 100 coincident boxes 1.45 ms
per edge pick. These are local measurements, not latency guarantees.

No personal models, screenshots, generated binaries or released STL exports are
included in this batch. No physical fit or printing performance is asserted.
