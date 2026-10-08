// Small public acceptance geometry. This module is edited by the harness only
// in its temporary copy, so revision tests never modify repository fixtures.
export function buildAssembly() {
  const wall = translate(cube({size:[4,36,28]}),[-18,-18,-14]);
  const plate = translate(cube({size:[24,24,5]}),[-12,-12,0]);
  const cap = translate(cube({size:[12,12,9]}),[-6,-6,5]);
  return {
    scene: compose(wall,plate,cap),
    displayParts: [
      {id:'wall',name:'Piastra di riferimento',group:['Riferimenti esterni'],solid:wall,color:'#ddd8cd',exportable:false},
      {id:'base',name:'Base',group:['Oggetto progettato','Supporto'],solid:plate,color:'#628bb5'},
      {id:'cap',name:'Coperchio',group:['Oggetto progettato','Supporto'],solid:cap,color:'#78a5c9'},
    ],
    dimensions: [{type:'linear',label:'Larghezza base',value:24,start:[-12,-15,0],end:[12,-15,0]}],
  };
}

export function buildInspection() {
  const body = translate(cylinder({height:20,radius:9,center:true}),[0,0,10]);
  const pin = translate(cylinder({height:5,radius:2,center:true}),[0,0,22.5]);
  return {
    scene: compose(body,pin),
    displayParts: [
      {id:'body',name:'Corpo cilindrico',group:['Oggetto progettato','Verifica'],solid:body,color:'#628bb5'},
      {id:'pin',name:'Perno',group:['Oggetto progettato','Verifica'],solid:pin,color:'#d09a63'},
    ],
    dimensions: [{type:'diameter',label:'Diametro corpo',value:18,start:[-9,0,10],end:[9,0,10]}],
  };
}
