// Run: NODE_PATH=/usr/lib/node_modules node tools/export_original.cjs
// The upstream algorithm is transpiled unchanged; outputs glyph + palette cells.
const fs = require('node:fs');
const path = require('node:path');
const ts = require('typescript');
const root = path.resolve(__dirname, '..');
const source = fs.readFileSync(path.join(root, 'reference/black-prism.ts'), 'utf8');
const js = ts.transpileModule(source, {
  compilerOptions: {module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2020}
}).outputText;
const exported = {};
new Function('exports', js)(exported);
const render = exported.default();
const frames = [];
for (let i = 0; i < 90; ++i) {
  const color = new Uint8Array(200 * 100);
  const text = render(i / 15, { color });
  const cells = Array.from(text.replaceAll('\n', ''));
  if (cells.length !== 20000) throw new Error('Unexpected upstream grid');
  frames.push({dots: cells.map(c => ' ·•●'.indexOf(c)), color: Array.from(color)});
}
fs.writeFileSync('/tmp/prism-original-frames.json', JSON.stringify({
  palette: exported.meta.palette, frames
}));
