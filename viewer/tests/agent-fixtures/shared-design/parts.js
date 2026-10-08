// Source geometry is shared by the design graph. The acceptance harness edits
// sourceWidth only in its temporary fixture copy.
const sourceWidth = 20;
export const sharedPanel = cube({size:[sourceWidth,12,6],center:true});
export const externalWall = cube({size:[4,48,24],center:true});
