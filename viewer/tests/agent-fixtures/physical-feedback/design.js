import {socket,pin,coupon,wall} from './parts.js';

export const design={
  schemaVersion:1,defaultView:'assembly',
  parts:[
    {id:'socket',name:'Boccola più stretta',solid:socket,color:'#628bb5'},
    {id:'pin',name:'Perno',solid:pin,color:'#76a385'},
    {id:'coupon',name:'Campione di accoppiamento',solid:coupon,color:'#cfaa6a'},
    {id:'wall',name:'Muro',solid:wall,color:'#ddd8cd',exportable:false}
  ],
  instances:[
    {id:'socket-1',part:'socket'},
    {id:'pin-1',part:'pin',transform:{translate:[12,9,0]}},
    {id:'coupon-1',part:'coupon'},
    {id:'wall-ref',part:'wall',transform:{translate:[-8,-6,0]}}
  ],
  groups:[
    {id:'external',name:'Riferimenti esterni',members:[{instance:'wall-ref'}]},
    {id:'object',name:'Oggetto progettato',members:[{instance:'socket-1'},{instance:'pin-1'}]}
  ],
  views:[
    {id:'assembly',name:'Montaggio',kind:'assembly',members:[{group:'external'},{group:'object'}]},
    {id:'plate',name:'Piatto completo',kind:'plate',members:[{group:'object'}],
      placements:{'socket-1':{translate:[10,10,0]},'pin-1':{translate:[50,20,0]}}},
    {id:'fit-sample',name:'Campione facoltativo',kind:'plate',members:[{instance:'coupon-1'},{instance:'pin-1'}],
      placements:{'coupon-1':{translate:[10,10,0]},'pin-1':{translate:[50,20,0]}}}
  ]
};
