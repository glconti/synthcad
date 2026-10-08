// Public manufacturing-review fixture. Dimensions are test data, not printer
// recommendations. The harness edits this shared source in a temporary copy.
const panelWidth = 20;
export const panel = cube({size:[panelWidth,12,4],center:false});
export const pin = cylinder({height:6,radius:3,center:false});
export const spacer = cube({size:[6,6,3],center:false});
export const wall = cube({size:[4,70,35],center:false});
