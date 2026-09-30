import { cp, mkdtemp, readFile, readdir, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import assert from 'node:assert/strict';
import { runCommand } from './bounded.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '../../..');
const core = ['gbsynth', 'pet', 'i18n', 'party', 'battle', 'link', 'save', 'inventory', 'wild'];

function replaceOnce(source, before, after) {
  assert(source.split(before).length === 2, 'mutation anchor must occur exactly once');
  return source.replace(before, after);
}

async function checked(command, options, stage) {
  const result = await runCommand(command, { echo: false, ...options });
  assert(result.ok, `${stage}: ${result.failure}\n${result.output.slice(-6000)}`);
  return result;
}

async function build(directory) {
  const emu = join(directory, 'tools/emu');
  await checked(['python3', join(emu, 'genproto.py'), join(directory, 'TamaPoke.ino')],
    { cwd: emu, timeoutMs: 30000 }, 'generate real sketch prototypes');
  await writeFile(join(emu, 'sketch.cpp'), '#include "proto.h"\n' + await readFile(join(directory, 'TamaPoke.ino'), 'utf8'));
  const command = ['g++', '-std=c++17', '-O1', '-w', '-pthread', `-I${emu}`, `-I${directory}`,
    `-I${emu}/tests/audio_stubs`, '-DTAMA_REAL_AUDIO', '-DTAMA_DETERMINISTIC_CLOCK',
    `-DSPRITE_DIR="${root}/tools/sdcard/mons"`, '-o', join(directory, 'workflow'),
    join(emu, 'tests/workflow_test.cpp'), ...core.map((name) => join(directory, `${name}.cpp`)),
    ...['host_impl', 'font', 'clock'].map((name) => join(emu, `${name}.cpp`))];
  await checked(command, { timeoutMs: 120000 }, 'compile real firmware workflow');
}

async function positive(directory, name) {
  await checked([join(directory, 'workflow'), '--case', name, 'enabled'], {
    timeoutMs: 60000,
    required: ['MILESTONE battle-started', 'MILESTONE idle-30s',
      'MILESTONE reboot-reloaded rewards-and-collection-preserved', 'PASS mandatory workflows complete: 1'],
  }, `fixed ${name}`);
}

const mutations = [
  {
    name: 'copied Preferences ownership', case: 'gym-win',
    reason: 'battle result left saving broken or pending',
    change(files) {
      files['pet.h'] = replaceOnce(files['pet.h'],
        '  Pet(const Pet &) = delete;\n  Pet &operator=(const Pet &) = delete;',
        '  Pet(const Pet &) = default;\n  Pet &operator=(const Pet &) = default;');
      files['TamaPoke.ino'] = replaceOnce(files['TamaPoke.ino'],
        '    combatantFromPet(btlSquad[btlSquadN++], pet, maxLvl);',
        '    Pet capped = pet;\n' +
        '    if (maxLvl && capped.level() > maxLvl)\n' +
        '      capped.ageMinutes = (uint32_t)(maxLvl - 1) * MINUTES_PER_LEVEL;\n' +
        '    combatantFromPet(btlSquad[btlSquadN++], capped);');
    },
  },
  {
    name: 'stopped music leaves active voices', case: 'wild-win',
    reason: 'audio watchdog starvation: 100 nonblocking polls without DMA or a wait',
    change(files) {
      files['audio.cpp'] = replaceOnce(files['audio.cpp'],
        '    if (m == MUS_NONE) {\n      gSyn.allOff();', '    if (m == MUS_NONE) {');
    },
  },
];

async function main() {
  assert(process.argv.length === 2, 'usage: node tools/emu/tests/mutations.mjs');
  const directory = await mkdtemp(join(tmpdir(), 'tamapoke-mutations-'));
  try {
    for (const file of await readdir(root))
      if (/\.(cpp|h)$/.test(file) || file === 'TamaPoke.ino') await cp(join(root, file), join(directory, file));
    await cp(join(root, 'tools/emu'), join(directory, 'tools/emu'), {
      recursive: true,
      filter: (file) => !/\.(nvs|bin|ppm|png|o)$/.test(file) && !file.endsWith('/tamapoke-emu'),
    });
    const originals = {};
    for (const file of ['pet.h', 'TamaPoke.ino', 'audio.cpp']) originals[file] = await readFile(join(directory, file), 'utf8');
    await build(directory);
    for (const mutation of mutations) {
      console.log(`CHECK fixed control: ${mutation.case}`);
      await positive(directory, mutation.case);
      const changed = { ...originals };
      mutation.change(changed);
      for (const [file, content] of Object.entries(changed)) await writeFile(join(directory, file), content);
      console.log(`CHECK restored original defect: ${mutation.name}`);
      await build(directory);
      const result = await runCommand([join(directory, 'workflow'), '--case', mutation.case, 'enabled'], {
        timeoutMs: 60000, echo: false,
      });
      assert(!result.ok && !result.timedOut && result.code === 1 &&
        result.output.includes('MILESTONE battle-started') && result.output.includes(mutation.reason) &&
        !result.output.includes('PASS mandatory workflows complete'),
      `mutation did not fail for its expected reason: ${result.failure}\n${result.output.slice(-6000)}`);
      console.log(`PASS mutation ${mutation.name}: ${mutation.reason}`);
      for (const [file, content] of Object.entries(originals)) await writeFile(join(directory, file), content);
      await build(directory);
      await positive(directory, mutation.case);
      console.log(`PASS restored fix: ${mutation.case}`);
    }
    const partyFile = join(directory, 'party.cpp');
    const partySource = await readFile(partyFile, 'utf8');
    const anchor = '      memcpy(&slots[i], old + i * oldStride, oldStride);';
    const boxTest = join(directory, 'box-regression');
    for (const broken of [false, true, false]) {
      await writeFile(partyFile, broken ? replaceOnce(partySource, anchor, `${anchor}\n    save();`) : partySource);
      await checked(['g++', '-std=c++17', '-O1', '-w', `-I${directory}/tools/emu`, `-I${directory}`,
        '-o', boxTest, join(directory, 'tools/emu/tests/box_test.cpp'),
        ...['pet', 'party', 'i18n'].map((name) => join(directory, `${name}.cpp`))],
      { timeoutMs: 120000 }, 'compile combined legacy migration control');
      const result = await runCommand([boxTest], { timeoutMs: 30000, echo: false });
      if (broken) {
        assert(!result.ok && result.code === 1 && !result.timedOut &&
          result.output.includes('FAIL  short legacy party migration waits for the 18-slot box before checkpointing') &&
          result.output.includes('FAIL  combined legacy migration survives checkpoint reload'),
        `premature migration checkpoint was not detected: ${result.failure}\n${result.output}`);
        console.log('PASS mutation premature legacy party checkpoint loses box');
      } else assert(result.ok, `fixed combined legacy migration failed: ${result.failure}\n${result.output}`);
    }
    console.log('PASS restored combined legacy party/box migration');
    console.log('PASS mandatory original-defect mutations complete: 2');
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
}

main().catch((error) => {
  console.error(`FAIL mutation gate: ${error.message}`);
  process.exitCode = 1;
});