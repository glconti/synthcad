# Batch 9: shared export review and immutable history

SC17 now routes GUI and CLI geometry export through the running viewer's
selection, validation and writer. The CLI offers `export --dry-run`, explicit
warning/replacement acknowledgement, revision guards and `export-history`.
Standard Core 3MF and STL preserve the current assembly or plate arrangement.
Exact Bambu presets, native multi-plate projects and slicing remain deferred.

The review exposes view/kind, format, destination, source quantities, available
printer context and cached check risks. Unknown physical/sliced evidence stays
unknown after an export. Checks cover the authored exportable geometry, not a
new validation of a user-selected subset. Selected solids receive individual
validity checks, including references explicitly enabled for export.

Both formats stage their bytes before an atomic destination commit. Stale
dependencies, failed serialization, empty selection and unconfirmed replacement
preserve existing artifacts. The service checks its deadline before staging
and before committing. A transport timeout around commit can still leave a
saved file; inspect the destination and history before retrying.

Successful commits receive an immutable local receipt with byte hash, instance
IDs, quantities, dependency digests and source/model/layout/profile revisions.
History persists beside the project without modifying its manifest. An export
remains valid if receipt storage fails; the response and UI disclose that
separately. Model edits mark earlier receipts stale without rewriting files.
Switching views alone does not falsely mark unchanged exports stale. History
hashing and publication are bounded, with explicit diagnostics for omissions.

Imported primary mesh files now participate in revision tracking and missing
file recovery. The importer reads independently, with before/after captures;
auxiliary importer assets are not tracked. This limitation is documented in
the API and revision contract. A model reload closes an open export review so
a click on the old review cannot approve newly loaded geometry.

## Verification

- MSVC Release viewer/CLI and all 26 Windows C++ suites passed, including the
  four existing camera, dimensions, appearance and parts suites.
- Ubuntu 24.04/GCC Release and all 26 C++ suites passed under the existing Linux
  validation environment. This is local Linux validation, not published CI.
- Public CLI acceptance passed on both platforms from isolated Unicode paths:
  dry run/no writes, explicit warning acceptance, 3MF mesh reuse and quantities,
  STL, no accidental reference export, overwrite protection, stale guards,
  empty export, failed-source recovery and history across viewer restart.
- Native primary-mesh acceptance passed on both platforms: changing only STL
  bytes changes revision, bounds and independently parsed export volume;
  previous guards fail and receipts become stale; deletion fails the load and
  restoration recovers without a JavaScript edit.
- Export-service tests exercise early/final deadline and stale guards,
  serialization failure, concurrent destination creation and exact receipts.
  History tests cover malformed/oversized records, unchanged stored JSON,
  missing/changed artifacts and explicit 4 MiB publication bounds.
- Large-response testing exposed a Windows named-pipe issue. Bounded write
  chunks and sleeping only when reads make no progress fix it; a 1 MiB
  round trip and the full native history workflow pass on both platforms.
- Actual Linux GUI tests passed warning review, scrolling, Export anyway,
  replacement confirmation and receipt creation. Reload during the modal
  closes it without creating or replacing a file. The final Windows dialog
  was inspected for wrapping and layout; available process context is shown.
- Standalone CLI guidance passed all 14 topics without a viewer, checkout or
  installed skill. The Windows embedded/runtime icon and watched error/recovery
  smoke test passed on the final mesh-aware build.
- All nine released V5 STL hashes remain unchanged. No personal models,
  generated exports, history, screenshots or build products are committed.

Reproduce the public workflows with:

```text
python scripts/test-export-workflow.py --cli PATH_TO_SYNTHCAD --viewer PATH_TO_VIEWER
python scripts/test-mesh-reload.py --cli PATH_TO_SYNTHCAD --viewer PATH_TO_VIEWER
```

On Linux run these under an X display; Xvfb/Mesa was used for acceptance.
Local evidence is under ignored `out/export-workflow` and
`out/linux-validation/batch9-final-*`. Physical print, strength and slicer
toolpath validation are separate evidence and are not claimed here.
