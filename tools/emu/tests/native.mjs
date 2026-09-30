import { cp, mkdir, mkdtemp, readFile, readdir, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import assert from 'node:assert/strict';
import { runCommand } from './bounded.mjs';
import { testPolicy } from './backends.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '../../..');

async function main() {
  assert(process.argv.length >= 3,
    'usage: node tools/emu/tests/native.mjs /path/to/esp-emu [--all | --negative-check | SUITE ...]');
  const emulator = resolve(process.argv[2]);
  const policy = await testPolicy();
  const negative = process.argv.length === 4 && process.argv[3] === '--negative-check';
  const suites = negative ? ['crc32_test'] :
    process.argv.length === 4 && process.argv[3] === '--all' ? policy.native : process.argv.slice(3);
  for (const suite of suites) assert(policy.native.includes(suite), `unported native suite: ${suite}`);
  assert(new Set(suites).size === suites.length, 'duplicate native suites');
  const logRoot = process.env.TAMA_TEST_LOG_DIR || tmpdir();
  await mkdir(logRoot, { recursive: true });
  const directory = await mkdtemp(join(logRoot, 'tamapoke-native-'));
  console.log(`Native evidence and disposable flash: ${directory}`);
  async function checked(name, command, options = {}) {
    console.log(`CHECK native ${name}`);
    const progress = setInterval(() => console.log(`CHECK native ${name}: running`), 15000);
    let result;
    try { result = await runCommand(command, options); } finally { clearInterval(progress); }
    await writeFile(join(directory, `${name}.log`), result.output);
    await writeFile(join(directory, `${name}.result.json`), JSON.stringify({ ...result, output: undefined }));
    assert(result.ok, `${name}: ${result.failure}\n${result.output.slice(-6000)}`);
    return result.output;
  }
  await checked('version', [emulator, '--version'], { required: ['0.44.0'], timeoutMs: 10000 });
  const sketch = join(directory, 'native');
  await mkdir(sketch);
  for (const file of await readdir(root))
    if (file.endsWith('.h') || ['pet.cpp', 'party.cpp', 'inventory.cpp', 'i18n.cpp', 'save.cpp',
      ...(suites.length ? ['gbsynth.cpp', 'battle.cpp', 'link.cpp', 'wild.cpp'] : [])].includes(file))
      await cp(join(root, file), join(sketch, file));
  await cp(join(here, suites.length ? 'native/suite.ino' : 'native/native.ino'), join(sketch, 'native.ino'));
  if (suites.length) {
    await cp(join(here, 'test_nvs.h'), join(sketch, 'test_nvs.h'));
    const declarations = [], entries = [];
    for (const suite of suites) {
      const source = await readFile(join(here, `${suite}.cpp`), 'utf8');
      const seed = Number(source.match(/uint32_t\s+g_seed\s*=\s*([^;]+)/)?.[1] || 12345);
      assert(Number.isInteger(seed), `unsupported native seed: ${suite}`);
      const symbol = `tamaNative_${suite}`;
      await cp(join(here, `${suite}.cpp`), join(sketch, `${suite}.h`));
      await writeFile(join(sketch, `${suite}.cpp`), `#define main ${symbol}\n#include "${suite}.h"\n`);
      declarations.push(`int ${symbol}();`);
      entries.push(`  {${JSON.stringify(suite)}, ${symbol}, ${seed}u}`);
    }
    await writeFile(join(sketch, 'suite_config.h'),
      declarations.join('\n') + '\nconst NativeSuite NATIVE_SUITES[] = {\n' + entries.join(',\n') + '\n};\n');
  }
  const build = join(directory, 'build');
  const fqbn = 'esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB';
  const compile = (name = 'compile') => checked(name, ['arduino-cli', 'compile', '--fqbn', fqbn, '--build-path', build,
    ...(suites.length ? ['--build-property', 'compiler.cpp.extra_flags=-DTAMA_NATIVE_TEST -UNDEBUG'] : []), sketch],
    { timeoutMs: 600000 });
  await compile();
  const flash = join(directory, 'flash.bin');
  await cp(join(build, 'native.ino.merged.bin'), flash);
  const command = [emulator, '--chip', 'esp32s3', '--firmware', flash,
    '--elf', join(build, 'native.ino.elf'), '--psram-size', '8M', '--save-state', '--timeout', suites.length ? '300s' : '90s',
    '--net', 'user,restrict=yes', '--log-color', 'never'];
  if (suites.length) {
    const select = (index) => ['--inject-on', 'MILESTONE native-suite-ready', '--inject', `${index}\\n`];
    async function runSuites(suffix = '') {
      for (const [index, suite] of suites.entries()) {
        await cp(join(build, 'native.ino.merged.bin'), flash);
        const complete = `PASS native-suite-complete ${suite} result=0`;
        const output = await checked(`${suite}${suffix}`, [...command, ...select(index), '--exit-on', complete], {
          timeoutMs: 310000, required: ['MILESTONE native-suite-ready', `MILESTONE native-suite-started ${suite}`, complete],
          rejected: ['FAIL', 'Timeout reached', 'Firmware abort', 'Task watchdog got triggered', 'Restarting emulator'],
        });
        const started = [...output.matchAll(/^MILESTONE native-suite-started (\w+)\r?$/gm)].map((match) => match[1]);
        const finished = [...output.matchAll(/^PASS native-suite-complete (\w+) result=0\r?$/gm)].map((match) => match[1]);
        assert.deepEqual(started, [suite], 'native suite missed its start or ran an unselected suite');
        assert.deepEqual(finished, [suite], 'native suite missed or repeated its completion');
        console.log(`PASS native suite: ${suite}`);
      }
      console.log(`PASS native-suite-matrix complete: ${suites.length}`);
    }
    await runSuites();
    if (negative) {
      const file = join(sketch, 'crc32.h');
      const original = await readFile(file, 'utf8');
      assert(original.split('0xEDB88320u').length === 2, 'native CRC mutation anchor must be unique');
      try {
        await writeFile(file, original.replace('0xEDB88320u', '0xEDB88321u'));
        await compile('compile-negative');
        await cp(join(build, 'native.ino.merged.bin'), flash);
        await checked('negative', [...command, ...select(0), '--exit-on', 'FAIL native-suite-complete crc32_test result=1'], {
          timeoutMs: 100000,
          required: ['MILESTONE native-suite-started crc32_test', 'FAIL  CRC-32 matches the published check value',
            'FAIL native-suite-complete crc32_test result=1'],
          rejected: ['PASS native-suite-matrix', 'Firmware abort', 'Task watchdog got triggered', 'Timeout reached'],
        });
      } finally { await writeFile(file, original); }
      await compile('compile-restored');
      await runSuites('-restored');
      console.log('PASS native failure control: corrupted firmware rejected; restored assertions pass');
    }
    console.log(`PASS migrated native suites: ${suites.length}`);
    return;
  }
  const before = await readFile(flash);
  await checked('warm-reboot', [...command, '--exit-on', 'PASS native warm-reboot complete'], {
    timeoutMs: 100000,
    required: ['MILESTONE native-psram-allocation-verified', 'MILESTONE native-real-nvs-legacy-migration',
      'MILESTONE native-checkpoints-written-rebooting',
      'MILESTONE native-reboot-reloaded-checkpoints-and-rewards', 'PASS native warm-reboot complete'],
    rejected: ['FAIL native:', 'Guru Meditation', 'Timeout reached', 'Task watchdog got triggered'],
  });
  const after = await readFile(flash);
  assert(before.length === after.length && !before.subarray(0x9000, 0xe000).equals(after.subarray(0x9000, 0xe000)),
    'native flash must contain actual NVS writes');
  assert(before.subarray(0x10000, 0x310000).equals(after.subarray(0x10000, 0x310000)),
    'storage probe must not modify its application partition');
  console.log('PASS native ROM boot, OPI PSRAM, real NVS migration and software reboot');
  const watchdog = await runCommand(command, {
    timeoutMs: 100000,
    required: ['MILESTONE native-cold-process-flash-persistence', 'MILESTONE native-watchdog-starvation-armed',
      'Task watchdog got triggered', 'loopTask (CPU 1)', 'Firmware abort'],
    rejected: ['FAIL native:', 'Timeout reached'],
  });
  await writeFile(join(directory, 'cold-watchdog.log'), watchdog.output);
  assert(watchdog.code === 1 && watchdog.failure === 'exit=1 signal=none',
    `expected real task watchdog panic, not an unrelated failure: ${watchdog.failure}`);
  console.log('PASS native cold-process persistence and intentional real task watchdog panic');
  await checked('post-panic', [...command, '--exit-on', 'PASS native storage-and-watchdog complete'], {
    timeoutMs: 100000,
    required: ['MILESTONE native-post-panic-process-persistence', 'PASS native storage-and-watchdog complete'],
    rejected: ['FAIL native:', 'Timeout reached', 'Task watchdog got triggered', 'Firmware abort'],
  });
  console.log('PASS native post-panic fresh-process persistence; automatic watchdog reboot is not emulated');
}

main().catch((error) => {
  console.error(`FAIL native gate: ${error.message}`);
  process.exitCode = 1;
});