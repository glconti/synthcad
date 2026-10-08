import {sharedPanel, externalWall} from './parts.js';

export const design = {
  schemaVersion: 1,
  defaultView: 'assembly',
  parts: [
    {id:'panel-blank', name:'Shared panel blank', solid:sharedPanel, color:'#628bb5'},
    {id:'room-wall', name:'External wall reference', solid:externalWall,
      color:'#ddd8cd', exportable:false},
  ],
  instances: [
    {id:'panel-left', part:'panel-blank', name:'Left panel',
      transform:{rotate:[0,0,0], translate:[-24,0,3]}},
    {id:'panel-right', part:'panel-blank', name:'Right panel',
      transform:{rotate:[0,0,90], translate:[24,0,3]}},
    {id:'wall-reference', part:'room-wall', name:'Room wall', exportable:false,
      transform:{rotate:[0,0,0], translate:[0,-20,12]}},
  ],
  groups: [
    {id:'designed-object', name:'Designed object', members:[
      {group:'mounting-alias'}, {group:'printable-panels'},
    ]},
    {id:'mounting-alias', name:'Mounting interface', members:[
      {instance:'panel-left'},
    ]},
    {id:'printable-panels', name:'Printable panels', members:[
      {instance:'panel-left'}, {instance:'panel-right'},
    ]},
    {id:'external-context', name:'External references', members:[
      {instance:'wall-reference'},
    ]},
  ],
  views: [
    {id:'assembly', name:'Assembly', kind:'assembly', members:[
      {group:'designed-object'}, {group:'external-context'},
    ]},
    {id:'inspection', name:'Inspection', kind:'inspection', members:[
      {instance:'panel-left'}, {instance:'panel-right'}, {instance:'wall-reference'},
    ], placements:{
      'panel-left':{rotate:[0,0,0], translate:[-18,18,3]},
      'panel-right':{rotate:[0,0,0], translate:[18,-18,3]},
      'wall-reference':{rotate:[0,0,0], translate:[0,0,12]},
    }},
    {id:'plate-1', name:'Plate 1', kind:'plate', members:[
      {instance:'panel-left'},
    ], placements:{
      'panel-left':{rotate:[0,0,0], translate:[0,0,3]},
    }},
    {id:'plate-2', name:'Plate 2', kind:'plate', members:[
      {instance:'panel-right'},
    ], placements:{
      'panel-right':{rotate:[0,0,0], translate:[48,0,3]},
    }},
  ],
};
