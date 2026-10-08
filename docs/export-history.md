# Generated export history

The viewer keeps generated export receipts outside `synthcad.json`. After a
3MF or STL destination has been committed, the viewer may save the receipt as
an immutable JSON file under:

```text
<project-parent>/.synthcad/exports/<project-identity-hash>/<receipt-id>.json
```

The project identity is derived from the canonical project path. Standalone
`.js` scenes use their own identity even when they share a directory with a
project manifest. Moving or copying a project does not move this sidecar
history automatically.

Each schema-version 1 receipt records its unique ID and creation time, output
path and format, SHA-256 of the exported bytes, selected part IDs and source
quantities, dependency digests, and the view/model/source/layout/profile basis
used for the export. Available checks, printer context, warnings and additional
service fields remain in the receipt. Receipts are created exclusively; a
duplicate ID never replaces an older entry. The exporter calls history storage
only after the artifact has been committed. If history storage fails, the
exported artifact remains available and the UI reports the persistence error.

The Project overview shows generated receipts separately from authored
`exports` metadata. It derives these statuses when history is loaded:

| Field | Values | Meaning |
| --- | --- | --- |
| Artifact | `current`, `changed`, `missing`, `unknown` | Whether the destination bytes match the recorded SHA-256, whether the file is absent, or whether the bounded check could not determine the result. |
| Source files | `current`, `stale`, `unknown` | Whether the recorded dependency paths still match their recorded digests. |
| Freshness | `current`, `stale`, `unknown` | Whether the receipt's view and revision basis matches the current project context. A known mismatch is stale; missing or failed-load context is unknown. Selecting a different view leaves an unchanged receipt unknown until that view's layout is compared, rather than marking it stale solely because another view is active. |

These statuses answer different questions. A receipt can be current for its
project basis while its output file has since been replaced; a file can still
match its recorded digest while its source has changed. Loading history does
not regenerate an export or update an old receipt.

The store accepts receipts up to 1 MiB and retains up to 1000 records for each
project identity. Loading reads at most 10000 directory entries and 64 MiB of
receipt JSON. Artifact and dependency hashing is bounded to 256 MiB per file
and 256 MiB total for a load. Entries beyond a limit are reported as
diagnostics or explicit unknown status; the store does not silently truncate a
record or claim a hash it did not compute. Invalid and unreadable JSON files
are skipped independently so valid sibling receipts remain visible.

Published GUI/CLI history is further capped at 4 MiB, retaining the newest
records first and at most 128 KiB of diagnostics. `omittedRecords` and
`omittedDiagnostics` counts and an explicit diagnostic describe anything that
does not fit. Full receipts remain in the local history directory. History is
loaded on startup, scene reload and export; artifact status is a cached check,
not continuous monitoring of destination files.

This history is local application data. It may contain absolute output and
dependency paths, authored part IDs, printer context and check details. It is
not embedded into STL or 3MF, does not alter authored project metadata, and is
not a backup of the project or exported geometry.
