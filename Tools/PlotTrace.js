const fs = require('fs');

const input = process.argv[2] || 'ECOLOGY_PHASE2_TRACE.csv';
const output = process.argv[3] || 'ECOLOGY_PHASE2_TRACE.svg';
const lines = fs.readFileSync(input, 'utf8').trim().split(/\r?\n/);
const headers = lines.shift().split(',');
const index = Object.fromEntries(headers.map((h, i) => [h, i]));
const rows = lines.map(line => line.split(',').map(Number));

const width = 1200;
const height = 620;
const left = 74;
const right = 30;
const top = 72;
const bottom = 66;
const plotWidth = width - left - right;
const plotHeight = height - top - bottom;
const maxTime = rows.at(-1)[index.time_s];
const x = t => left + (t / maxTime) * plotWidth;
const y = v => top + (1 - v) * plotHeight;

function pathFor(field, stride = 3) {
  const parts = [];
  for (let i = 0; i < rows.length; i += stride) {
    parts.push(`${parts.length ? 'L' : 'M'}${x(rows[i][index.time_s]).toFixed(2)},${y(rows[i][index[field]]).toFixed(2)}`);
  }
  return parts.join(' ');
}

let grid = '';
for (let i = 0; i <= 10; ++i) {
  const value = i / 10;
  grid += `<line x1="${left}" y1="${y(value)}" x2="${width-right}" y2="${y(value)}" class="grid"/>`;
  grid += `<text x="${left-12}" y="${y(value)+5}" text-anchor="end" class="axis">${value.toFixed(1)}</text>`;
}
for (let t = 0; t <= maxTime + 0.001; t += 30) {
  grid += `<line x1="${x(t)}" y1="${top}" x2="${x(t)}" y2="${height-bottom}" class="grid"/>`;
  grid += `<text x="${x(t)}" y="${height-bottom+28}" text-anchor="middle" class="axis">${t}</text>`;
}

const svg = `<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${height}" viewBox="0 0 ${width} ${height}">
<style>
  .bg { fill:#0c1211 } .grid { stroke:#263a32; stroke-width:1 } .axis { fill:#8ea997; font:13px Segoe UI,sans-serif }
  .title { fill:#e0ecd9; font:bold 24px Segoe UI,sans-serif } .sub { fill:#8ea997; font:14px Segoe UI,sans-serif }
  .prey { fill:none; stroke:#4fa07a; stroke-width:2.2 } .pred { fill:none; stroke:#cd5a47; stroke-width:2.2 }
  .counter { fill:none; stroke:#d7bf68; stroke-width:1.6; stroke-dasharray:7 6 }
</style>
<rect class="bg" width="100%" height="100%"/>
<text x="${left}" y="34" class="title">Phase 2: autonomous gain-only Predator / Prey ecology</text>
<text x="${left}" y="56" class="sub">Constant input and settings • no LFO • no scripted timeline • 50 Hz ecology tick</text>
${grid}
<path d="${pathFor('prey')}" class="prey"/>
<path d="${pathFor('predator')}" class="pred"/>
<path d="${pathFor('prey_no_attack')}" class="counter"/>
<text x="${left}" y="${height-18}" class="axis">Time (seconds)</text>
<text x="${width-380}" y="34" class="axis" fill="#4fa07a">— Prey</text>
<text x="${width-290}" y="34" class="axis" fill="#cd5a47">— Predator</text>
<text x="${width-180}" y="34" class="axis" fill="#d7bf68">- - Prey, attack disabled</text>
</svg>`;

fs.writeFileSync(output, svg);
console.log(`Wrote ${output}`);
