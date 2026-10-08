# Build volume and optional context

Printer context lives in the current project's `synthcad.json`. No global defaults
or previously opened project's profile are inherited. `profiles` is an object
keyed by stable, nonempty IDs; `activeProfile` explicitly selects one of those IDs.
Every declared profile is validated, including profiles that are not selected.
An incomplete profile can be reviewed and geometry can still be evaluated.
For plate placement, only the build volume is required. `profiles` is the
existing project metadata container, not a catalog of known printers or verified
presets. Printer, nozzle, material and provenance can be added later; they are
not prerequisites for bounds, height or bed display.

Start with this fragment in the existing manifest:

```json
{
  "activeProfile": "custom",
  "profiles": {
    "custom": { "buildVolume": [220, 220, 250] }
  }
}
```

These dimensions are illustrative. Supply the intended bed width, depth and
maximum height in millimetres. Missing exclusions remain unknown and their
check stays `not-checked`; add `"exclusions": []` only after confirming none.

When further process context is needed, ask:

1. Which printer, or what custom bed width, depth, and maximum height in mm?
2. Are there bed clips or other rectangular excluded regions? Confirm `[]` for none.
3. What nozzle diameter in mm and which material?
4. Where did these values come from: user, manufacturer, or slicer? Which values
   are provisional and need confirmation?

For example, merge this fragment into the project's existing manifest:

```json
{
  "activeProfile": "custom-pla",
  "profiles": {
    "custom-pla": {
      "name": "Custom printer / PLA",
      "printer": { "id": "custom-printer", "name": "Custom printer" },
      "buildVolume": [220, 200, 250],
      "exclusions": [
        { "name": "Front clip", "min": [0, 0], "max": [12, 8] }
      ],
      "nozzleDiameter": 0.4,
      "material": { "id": "pla", "name": "PLA" },
      "provenance": { "type": "user", "description": "Measured bed; material pending confirmation" },
      "provisional": ["material"]
    }
  }
}
```

Optional profile fields may be omitted or `null` while setup is incomplete. Bed
coordinates start at `[0,0]`; `buildVolume` is three positive finite dimensions.
Each exclusion is an axis-aligned rectangle in mm, with nonnegative `min`, `max`
strictly greater than `min` on both axes, and bounds inside the bed when its size
is available. An omitted exclusion list means unknown obstacles; an empty list
explicitly confirms none. Nozzle diameter must be positive and finite.

`printer` and `material` accept optional string `id` and `name`. `provenance`
accepts `type` (`user`, `manufacturer`, or `slicer`), optional string `description`,
and optional string `source`. `provisional` is a unique list drawn from `printer`,
`buildVolume`, `exclusions`, `nozzleDiameter`, `material`, and `provenance`.
`slicer` may record optional strings `id`, `version`, and `presetId`. These IDs do
not verify a slicer preset. Dingcad never reports them as verified presets.
Unknown fields are rejected; custom metadata belongs in a profile-level
`extensions` object.

All authored strings and object keys, including extensions, must be valid UTF-8
without NUL and at most 4096 bytes. Profile IDs and present `id`/`presetId` values
must contain 1..128 UTF-8 bytes. Null or omitted identifiers remain incomplete;
an explicitly empty identifier is invalid. Optional names may be empty. Limits
are 256 profiles/project, 4096 exclusions/profile, 65536 values/profile, and 32
nesting levels. Validation reports at most 64 scoped errors per response.

The pure C++ `PrinterProfileContext(project)` API returns `status` (`complete`,
`incomplete`, or `invalid`), normalized selected `profile`, `activeProfile`,
`bedOrigin`, `missing`, `missingIdentifiers`, `provisional`, scoped `errors`,
`profileRevision`, `checkReadiness`, and `slicerPresetVerified` (always false).
Errors contain `path` and `message`; they are reported as context and do not throw
into geometry evaluation. Without an active profile, `profile` and
`profileRevision` are null. `PrinterProfileTemplate()` returns an incomplete
authoring fragment.

Setup completeness requires printer, volume, exclusions, nozzle, material, and
provenance including its type. Missing printer/material IDs are reported
separately in `missingIdentifiers`; names are optional. Provisional values keep
setup incomplete. That overall status does not block placement checks when
dimensions are available. `checkReadiness` reports `buildVolume`, `exclusions`, `nozzle`, and `material`
as `ready`, `missing`, `provisional`, or `invalid`, each with a reason. Volume
readiness requires dimensions only; exclusions have their own readiness.
Only provisional dimensions make volume readiness provisional. A provisional
printer, material or exclusion list does not downgrade authored dimensions.
Material readiness needs
a nonempty material ID or name. Missing IDs remain visible independently of
readiness. Any validation error makes these readiness states invalid.

Readiness describes available metadata. It does not run geometry checks, prove
bed fit, guarantee printability, or validate slicer settings. The profile revision
is a deterministic SHA-256 of the selected ID and normalized authored profile;
object key order and null versus omitted optional fields do not change it.
Provisional fields are sorted; numeric values use a consistent representation.
Changing selected IDs, values, or provisional flags changes the revision;
changing an unselected profile does not. Extensions are preserved as authored.
