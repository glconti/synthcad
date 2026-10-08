import {panel, pin, spacer, wall} from './parts.js';

export const design = {
  schemaVersion: 1,
  defaultView: 'assembly',
  parts: [
    {id:'panel', name:'Shared panel', solid:panel, color:'#628bb5', quantity:2},
    {id:'pin', name:'Locating pin', solid:pin, color:'#76a385', quantity:1},
    {id:'spacer', name:'Spacer', solid:spacer, color:'#cfaa6a', quantity:1},
    {id:'wall', name:'External wall', solid:wall, color:'#ddd8cd', exportable:false},
  ],
  instances: [
    {id:'panel-a', part:'panel', name:'Panel A'},
    {id:'panel-b', part:'panel', name:'Panel B', transform:{translate:[30,0,0]}},
    {id:'pin-1', part:'pin', name:'Pin', transform:{translate:[65,6,0]}},
    {id:'spacer-1', part:'spacer', name:'Spacer', transform:{translate:[75,0,0]}},
    {id:'wall-ref', part:'wall', name:'External reference', exportable:false,
      transform:{translate:[-15,-20,0]}},
  ],
  groups: [
    {id:'external', name:'External references', members:[{instance:'wall-ref'}]},
    {id:'panels', name:'Panel pair', members:[{instance:'panel-a'},{instance:'panel-b'}]},
    {id:'mounting', name:'Mounting alias', members:[{instance:'panel-a'}]},
    {id:'object', name:'Designed object', members:[{group:'panels'},{group:'mounting'},
      {instance:'pin-1'},{instance:'spacer-1'}]},
  ],
  views: [
    {id:'assembly', name:'Assembly', kind:'assembly',
      members:[{group:'external'},{group:'object'}]},
    {id:'inspection', name:'Inspection', kind:'inspection', members:[{group:'object'}]},
    {id:'plate-a', name:'Plate A', kind:'plate',
      members:[{group:'panels'},{group:'mounting'},{instance:'pin-1'}],
      placements:{
        'panel-a':{rotate:[0,0,0],translate:[20,20,0]},
        'panel-b':{rotate:[0,0,90],translate:[70,20,0]},
        'pin-1':{rotate:[0,0,0],translate:[20,55,0]},
      }},
    {id:'plate-b', name:'Plate B', kind:'plate', members:[{instance:'spacer-1'}],
      placements:{'spacer-1':{rotate:[0,0,0],translate:[20,20,0]}}},
  ],
};
