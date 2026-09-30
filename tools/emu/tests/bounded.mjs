import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { resolve } from 'node:path';
import assert from 'node:assert/strict';
import { testRoutes } from './backends.mjs';

export async function runCommand(command, {
  timeoutMs = 120000, required = [], rejected = [], echo = true, cwd, env = process.env,
} = {}) {
  assert(command.length > 0 && timeoutMs > 0, 'a command and positive deadline are mandatory');
  return new Promise((done) => {
    const child = spawn(command[0], command.slice(1), {
      cwd, env, detached: true, stdio: ['ignore', 'pipe', 'pipe'],
    });
    let output = '', timedOut = false, interrupted = false, overflow = false, execError = '';
    const kill = () => {
      if (!child.pid) return;
      try { process.kill(-child.pid, 'SIGKILL'); } catch (error) {
        if (error.code !== 'ESRCH') execError = error.message;
      }
    };
    const interrupt = () => { interrupted = true; kill(); };
    process.once('SIGINT', interrupt);
    process.once('SIGTERM', interrupt);
    const timer = setTimeout(() => { timedOut = true; kill(); }, timeoutMs);
    const collect = (chunk) => {
      const text = chunk.toString();
      if (echo) process.stdout.write(text);
      output += text;
      if (output.length > 32 * 1024 * 1024) { overflow = true; kill(); }
      if (rejected.some((milestone) => output.includes(milestone))) kill();
    };
    child.stdout.on('data', collect);
    child.stderr.on('data', collect);
    child.on('error', (error) => { execError = error.message; });
    child.on('close', (code, signal) => {
      clearTimeout(timer);
      process.removeListener('SIGINT', interrupt);
      process.removeListener('SIGTERM', interrupt);
      const missing = required.filter((milestone) => !output.includes(milestone));
      const forbidden = rejected.filter((milestone) => output.includes(milestone));
      const ok = code === 0 && !signal && !timedOut && !interrupted && !overflow && !execError &&
        missing.length === 0 && forbidden.length === 0;
      const failures = [
        timedOut && `deadline exceeded (${timeoutMs} ms)`,
        interrupted && 'interrupted', overflow && 'output exceeded 32 MB', execError,
        (code !== 0 || signal) && `exit=${code} signal=${signal || 'none'}`,
        missing.length && `missing milestones: ${missing.join(', ')}`,
        forbidden.length && `unexpected output: ${forbidden.join(', ')}`,
      ].filter(Boolean);
      done({ ok, code, signal, timedOut, output, failure: failures.join('; ') });
    });
  });
}

async function selfTest() {
  const node = (source) => [process.execPath, '-e', source];
  const ready = await runCommand(node('console.log("READY")'), { required: ['READY'], echo: false });
  assert(ready.ok);
  const missing = await runCommand(node('process.exit(0)'), { required: ['READY'], echo: false });
  assert(!missing.ok && missing.failure.includes('missing milestones'));
  const timeout = await runCommand(node('setInterval(() => {}, 1000)'), { timeoutMs: 100, echo: false });
  assert(!timeout.ok && timeout.timedOut);
  const failed = await runCommand(node('process.exit(7)'), { echo: false });
  assert(!failed.ok && failed.code === 7);
  const reset = await runCommand(node('console.log("unexpected firmware reset")'), {
    rejected: ['unexpected firmware reset'], echo: false,
  });
  assert(!reset.ok && reset.failure.includes('unexpected output'));
  const unavailable = await runCommand(['/definitely/unavailable/tamapoke-tool'], { echo: false });
  assert(!unavailable.ok && unavailable.failure);
  const routed = await testRoutes();
  assert(routed.some((test) => test.backend === 'native') && routed.some((test) => test.backend === 'host'));
  assert.deepEqual(await testRoutes('battle_test'), [{ name: 'battle_test', backend: 'native' }]);
  assert((await testRoutes('', 'host')).every((test) => test.backend === 'host'));
  await assert.rejects(testRoutes('definitely_no_such_suite'));
  await assert.rejects(testRoutes('', 'silent-fallback'));
  console.log('PASS native-first routing: complete ownership, filters, explicit host mode, invalid requests');
  console.log('PASS bounded gates: milestones, timeout, nonzero exit, reset, unavailable tool');
}

async function main(args) {
  if (args.length === 1 && args[0] === '--self-test') { await selfTest(); return; }
  const separator = args.indexOf('--');
  const seconds = Number(args[0]);
  assert(Number.isFinite(seconds) && seconds > 0 && separator >= 1 && args.length > separator + 1,
    'usage: node bounded.mjs SECONDS [--require TEXT] [--reject TEXT] -- COMMAND [ARGS]');
  const required = [], rejected = [];
  for (let index = 1; index < separator; index += 2) {
    assert(index + 1 < separator, 'missing gate value');
    if (args[index] === '--require') required.push(args[index + 1]);
    else if (args[index] === '--reject') rejected.push(args[index + 1]);
    else throw new Error(`unknown gate: ${args[index]}`);
  }
  const result = await runCommand(args.slice(separator + 1), { timeoutMs: seconds * 1000, required, rejected });
  if (!result.ok) throw new Error(result.failure);
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main(process.argv.slice(2)).catch((error) => {
    console.error(`FAIL bounded command: ${error.message}`);
    process.exitCode = 1;
  });
}