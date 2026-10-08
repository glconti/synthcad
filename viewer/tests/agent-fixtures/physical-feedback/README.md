# Optional sample and physical-feedback fixture

This synthetic socket-and-pin design exercises the optional workflow. Its
clearance is test data, not a material/nozzle recommendation. No part has been
printed or tested physically. The manifest initially has no evidence records.

`assembly`, `plate` and `fit-sample` reference shared source geometry. The short
socket coupon is intersected from the full socket and uses its same clearance.
The reusable pin instance appears in both full and sample layouts. The wall is
an external reference excluded from exports.

`scripts/test-physical-feedback.py` copies this fixture before adding synthetic
user reports and revision-bound compatibility records. It verifies that a
full export works without those records, that exporting a sample never marks
it printed/tested, and that old outputs survive a source revision. Simulated
observations in that test are not claims about physical performance.
