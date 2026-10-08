// Synthetic acceptance geometry, not a validated fit recommendation.
// The sample and full part derive from the same parameter and source solid.
export const clearance = 0.1;
const pinRadius = 3;
export const socket = difference(
  cube({size:[24,18,10]}),
  translate(cylinder({height:12,radius:pinRadius+clearance}),[12,9,-1])
);
export const pin = cylinder({height:10,radius:pinRadius});
export const coupon = intersection(socket,cube({size:[24,18,3]}));
export const wall = cube({size:[40,3,20]});
