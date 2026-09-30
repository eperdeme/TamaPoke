import { readFile, readdir } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import assert from 'node:assert/strict';

const here = dirname(fileURLToPath(import.meta.url));

export async function testPolicy() {
  const policy = JSON.parse(await readFile(resolve(here, 'backends.json'), 'utf8'));
  assert(Array.isArray(policy.native) && policy.host && typeof policy.host === 'object', 'invalid backend policy');
  const assigned = [...policy.native, ...Object.keys(policy.host)].sort();
  const actual = (await readdir(here)).filter((file) => file.endsWith('_test.cpp')).map((file) => file.slice(0, -4)).sort();
  assert(new Set(assigned).size === assigned.length, 'a test cannot have two default backends');
  assert.deepEqual(assigned, actual, 'every suite must have an explicit backend; stale or unclassified suites are failures');
  for (const [name, reason] of Object.entries(policy.host))
    assert(typeof reason === 'string' && reason.length > 0, `host-only suite needs a reason: ${name}`);
  return policy;
}

export async function testRoutes(filter = '', backend = 'auto') {
  assert(['auto', 'host'].includes(backend), 'TAMA_TEST_BACKEND must be auto or host');
  const policy = await testPolicy();
  const names = [...policy.native, ...Object.keys(policy.host)].sort().filter((name) => name.includes(filter));
  assert(names.length > 0, `no mandatory suites matched '${filter}'`);
  return names.map((name) => ({ name, backend: backend === 'auto' && policy.native.includes(name) ? 'native' : 'host' }));
}

async function main() {
  assert(process.argv.length <= 4, 'usage: node backends.mjs [FILTER] [auto|host]');
  const routes = await testRoutes(process.argv[2], process.argv[3]);
  for (const { name, backend } of routes) console.log(`${backend}\t${name}`);
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url))
  main().catch((error) => { console.error(`FAIL test routing: ${error.message}`); process.exitCode = 1; });