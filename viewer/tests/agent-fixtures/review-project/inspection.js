import {buildInspection} from './modules/shape-library.js';
const model = buildInspection();
export const scene = model.scene;
export const displayParts = model.displayParts;
export const dimensions = model.dimensions;
