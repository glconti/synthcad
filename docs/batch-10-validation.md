# Batch 10: optional physical feedback

SC18 adds explicit sample and user-reported observation records to the project
overview. Samples reference registered project views and shared source parts;
an unvisited sample view can be opened directly from the overview. A full export
remains available without any sample, print or test records.

Observations identify their reporter, kind, result, details and optional test
conditions. Proposed, printed, tested and superseded stages change only when
authored. Export does not imply that a part was printed or physically tested.
Revision freshness is separate from those stages: changing geometry can make a
tested record stale without rewriting its historical report.

Compatibility notes preserve previous and target revision bases and distinguish
affected parts from the subset requiring reprints. Invalid records are rejected
without discarding valid siblings or blocking otherwise valid geometry export.
Missing evidence references remain visible with warnings. These are authored
decisions, not automatic compatibility or strength certification.

## Verification

- Final MSVC Release viewer/CLI and all 26 Windows C++ suites passed, including
  the four legacy camera, dimensions, appearance and parts suites.
- Final Ubuntu 24.04/GCC Release and all 26 C++ suites passed. A separate clean
  CI rehearsal built pinned raylib 6.0 and passed the same canonical inventory.
- Standalone embedded guidance passed all 15 topics on Windows and Linux,
  including the new `synthcad docs physical-feedback` topic.
- The public physical-feedback acceptance script passed on both platforms from
  an isolated path containing spaces and accented text. It verifies full export
  without samples, shared sample references, explicit stage transitions,
  synthetic fit observations, changed source geometry, stale historical reports,
  targeted reprint notes, preserved previous output bytes and invalid-record
  recovery. Its simulated observations are test data, not actual printed tests.
- The isolated Linux CI rehearsal passed all nine native acceptance scripts:
  sessions, shared design, geometric selection, guided picks, project overview,
  manufacturing review, exports, mesh reload and physical feedback. Actual X11
  DPI resources at 144 and 192 passed geometric-selection and guided-pick input
  tests at scales 1.5 and 2. This is Xvfb/Mesa evidence, not physical-monitor or
  native Wayland validation.
- The Windows embedded/runtime icon and watched-error/recovery smoke test passed.
- Actual Linux GUI review verified that an unvisited registered sample opens
  from its overview action and becomes the active, displayed view. The card
  renders Italian sample names alongside English controls and distinguishes a
  user-reported observation, its explicit stage and stale revision basis.
- All nine released V5 STL hashes remain unchanged. Personal scenes, generated
  exports, screenshots and build products are excluded from commits.

Run the new public workflow with:

```text
python scripts/test-physical-feedback.py --cli PATH_TO_SYNTHCAD --viewer PATH_TO_VIEWER
synthcad docs physical-feedback
```

On Linux the native workflow needs an X display. Local evidence is under ignored
`out/physical-feedback`, `out/linux-validation/batch10-*` and
`out/linux-validation/batch10-ci-final-logs`. Hosted CI is tracked separately in
[CI documentation](ci.md); a local rehearsal does not close SC19.
