// Regression: sequential JS callback + blended implicit field, including a clipped base.
const smoothMin=(a,b,k)=>{const h=Math.max(k-Math.abs(a-b),0)/k;return Math.min(a,b)-h*h*k/4;};
function ellipsoidField(v,c,r,seed,roughness=1) {
  const q=v.map((a,i)=>a-c[i]);
  const ell=(Math.hypot(q[0]/r[0],q[1]/r[1],q[2]/r[2])-1)*Math.min(...r);
  const wave=0.55*Math.sin(q[0]*0.22+seed)*Math.cos(q[2]*0.18-seed)
    +0.48*Math.sin(q[1]*0.24+q[2]*0.16+seed)
    +0.38*Math.cos(q[0]*0.11-q[1]*0.19+q[2]*0.12+seed)
    +0.16*Math.sin(q[0]*0.51+q[1]*0.32-q[2]*0.36);
  return ell-wave*roughness;
}
function blendedField(v) {
  const [x,y,z]=v;
  const lower=ellipsoidField(v,[-1,1,24],[31,29.5,29],0.4,1.55);
  const upper=ellipsoidField(v,[2,-0.5,60],[24,22.5,25],2.7,1.35);
  // Merge a gently irregular footing into the snow, without a separate plinth.
  const a=Math.atan2(y,x);
  const foot=Math.max(Math.hypot(x,y)-(24+0.32*z+0.55*Math.sin(a*3+0.5)),-z,z-10);
  return smoothMin(smoothMin(lower,upper,3.5),foot,2.8);
}
export const scene=levelSet({sdf:blendedField,bounds:{min:[-36,-35,-8],max:[36,35,87]},edgeLength:1.25,tolerance:0.02});
