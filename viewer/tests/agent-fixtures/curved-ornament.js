// Compact second project for simultaneous-session and standalone-scene checks.
const foot = cube({size:[16,16,3]});
const stem = translate(cylinder({height:22,radius:2.5,center:true}),[8,8,14]);
const bead = translate(sphere({radius:7}),[8,8,28]);
export const scene = compose(foot,stem,bead);
export const displayParts = [
  {id:'foot',name:'Base decorativa',group:['Oggetto progettato'],solid:foot,color:'#6686a8'},
  {id:'stem',name:'Stelo',group:['Oggetto progettato'],solid:stem,color:'#d09a63'},
  {id:'bead',name:'Sfera superiore',group:['Oggetto progettato'],solid:bead,color:'#b96e73'},
];
