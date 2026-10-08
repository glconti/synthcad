import {buildAssembly} from './modules/shape-library.js';
const model = buildAssembly();
export const scene = model.scene;
export const displayParts = model.displayParts;
export const dimensions = model.dimensions;
