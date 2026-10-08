// Generic fixture: external context and two separately printable parts.
const wall = translate(cube({size:[100,4,75]}),[-50,-4,-5]);
const left = translate(cube({size:[30,30,6]}),[-35,0,10]);
const right = translate(cube({size:[30,30,6]}),[5,0,10]);
export const scene = compose(wall,left,right);
export const displayParts = [
  {id:'wall',name:'Muro',group:['Riferimenti esterni'],solid:wall,color:'#ddd8cd',exportable:false},
  {id:'left',name:'Piastra sinistra',group:['Oggetto progettato','Piastre'],solid:left,color:'#628bb5'},
  {id:'right',name:'Piastra destra',group:['Oggetto progettato','Piastre'],solid:right,color:'#78a5c9'},
];
export const dimensions = [{type:'linear',label:'Width',value:30,start:[-35,33,16],end:[-5,33,16]}];
