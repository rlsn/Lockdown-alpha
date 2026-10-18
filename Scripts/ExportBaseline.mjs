import { readFile, writeFile } from 'node:fs/promises';
import { resolve, join } from 'node:path';
import { pathToFileURL } from 'node:url';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';

const [baselineRoot, outputFile] = process.argv.slice(2);
if (!baselineRoot || !outputFile) throw new Error('Usage: node --import <baseline tsx loader> ExportBaseline.mjs <baselineRoot> <outputFile>');
const root = resolve(baselineRoot);
const revision = execFileSync('git', ['-C', root, 'rev-parse', 'HEAD'], { encoding: 'utf8' }).trim();
const { C } = await import(pathToFileURL(join(root, 'packages/shared/config.ts')).href);
const { MAP } = await import(pathToFileURL(join(root, 'packages/shared/map.ts')).href);
const sourceHashes = {};
for (const file of ['packages/shared/config.ts', 'packages/shared/map.ts', 'packages/simulation/life.ts', 'packages/simulation/vision.ts', 'packages/simulation/objectives.ts', 'packages/simulation/match.ts']) {
  sourceHashes[file] = createHash('sha256').update(await readFile(join(root, file))).digest('hex');
}
const snapshot = {
  schemaVersion: 1,
  baselineRevision: revision,
  constants: C,
  map: MAP,
  conversion: { centimetresPerPixel: 2, originPixel: [1200, 800], invertY: true, visionLevelHeightCm: 200 },
  sourceHashes,
};
await writeFile(resolve(outputFile), JSON.stringify(snapshot, null, 2) + '\n', 'utf8');
console.log(`Exported ${revision} -> ${resolve(outputFile)}`);
