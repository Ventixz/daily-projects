// Pure memory-register operations (M+/M-/MR/MC). Kept separate from
// session.js's expression state because memory survives AC, unlike
// everything else -- a reducer that mixed the two would make that
// asymmetry easy to lose track of.

function memoryAdd(memory, value) {
  return memory + value;
}

function memorySubtract(memory, value) {
  return memory - value;
}

function memoryClear() {
  return 0;
}

export { memoryAdd, memorySubtract, memoryClear };
