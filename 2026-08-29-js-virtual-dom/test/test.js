// Hand-rolled test runner — no framework, same reasoning as the other
// days in this repo: fewer moving parts to install/trust in a sandboxed
// environment with restricted network access.
const assert = require('assert');
const { h } = require('../src/h');
const dom = require('../src/dom');
const { createElementFromVNode } = require('../src/render');
const { updateElement, changed } = require('../src/diff');

const tests = [];
function test(name, fn) {
  tests.push({ name, fn });
}

function render(vtree) {
  return createElementFromVNode(vtree);
}

// Applies newTree to a real node built from oldTree, in place, the way
// the demo does: patch the child slot of a throwaway root.
function patch(root, newTree, oldTree) {
  updateElement(root, newTree, oldTree, 0);
  return root.children[0];
}

test('createElementFromVNode builds nested elements with attrs and text', () => {
  const vtree = h('div', { id: 'app' }, h('span', { class: 'label' }, 'hi'), 'plain text');
  const el = render(vtree);
  assert.strictEqual(dom.toHTML(el), '<div id="app"><span class="label">hi</span>plain text</div>');
});

test('createElementFromVNode renders a bare string leaf as a text node', () => {
  const el = render('just text');
  assert.strictEqual(el.nodeType, 'text');
  assert.strictEqual(el.textContent, 'just text');
});

test('changed: same tag, same-shape text -> false', () => {
  assert.strictEqual(changed(h('div'), h('div')), false);
  assert.strictEqual(changed('a', 'a'), false);
});

test('changed: different tag -> true', () => {
  assert.strictEqual(changed(h('div'), h('span')), true);
});

test('changed: different text content -> true', () => {
  assert.strictEqual(changed('a', 'b'), true);
});

test('changed: text vs element -> true', () => {
  assert.strictEqual(changed('a', h('div')), true);
});

test('updateElement: appends when there is no old node', () => {
  const root = dom.createElement('root');
  updateElement(root, h('p', {}, 'new'), undefined, 0);
  assert.strictEqual(root.children.length, 1);
  assert.strictEqual(dom.toHTML(root.children[0]), '<p>new</p>');
});

test('updateElement: removes when there is no new node', () => {
  const root = dom.createElement('root');
  root.appendChild(render(h('p', {}, 'gone')));
  updateElement(root, undefined, h('p', {}, 'gone'), 0);
  assert.strictEqual(root.children.length, 0);
});

test('updateElement: replaces on tag change, in place', () => {
  const root = dom.createElement('root');
  root.appendChild(render(h('span', {}, 'x')));
  const patched = patch(root, h('div', {}, 'x'), h('span', {}, 'x'));
  assert.strictEqual(patched.tagName, 'div');
});

test('updateElement: updates text content by replacing the text node', () => {
  const root = dom.createElement('root');
  root.appendChild(render('old'));
  const patched = patch(root, 'new', 'old');
  assert.strictEqual(patched.textContent, 'new');
});

test('updateElement: adds, changes, and removes attributes', () => {
  const root = dom.createElement('root');
  const oldTree = h('div', { id: 'keep', class: 'red', drop: 'me' });
  root.appendChild(render(oldTree));
  const newTree = h('div', { id: 'keep', class: 'blue', added: 'yes' });
  const patched = patch(root, newTree, oldTree);
  assert.deepStrictEqual(patched.attrs, { id: 'keep', class: 'blue', added: 'yes' });
});

test('updateElement: recurses into unchanged-tag children and patches only what differs', () => {
  const root = dom.createElement('root');
  const oldTree = h('ul', {}, h('li', {}, 'a'), h('li', {}, 'b'));
  const el = render(oldTree);
  root.appendChild(el);
  const firstLi = el.children[0];

  const newTree = h('ul', {}, h('li', {}, 'a'), h('li', {}, 'c'));
  updateElement(root, newTree, oldTree, 0);

  assert.strictEqual(el.children[0], firstLi, 'unchanged child must be reused, not rebuilt');
  assert.strictEqual(dom.toHTML(el), '<ul><li>a</li><li>c</li></ul>');
});

test('updateElement: shrinking child list removes trailing real nodes', () => {
  const root = dom.createElement('root');
  const oldTree = h('ul', {}, h('li', {}, 'a'), h('li', {}, 'b'), h('li', {}, 'c'));
  root.appendChild(render(oldTree));
  const newTree = h('ul', {}, h('li', {}, 'a'));
  const patched = patch(root, newTree, oldTree);
  assert.strictEqual(dom.toHTML(patched), '<ul><li>a</li></ul>');
});

test('updateElement: growing child list appends new real nodes', () => {
  const root = dom.createElement('root');
  const oldTree = h('ul', {}, h('li', {}, 'a'));
  root.appendChild(render(oldTree));
  const newTree = h('ul', {}, h('li', {}, 'a'), h('li', {}, 'b'), h('li', {}, 'c'));
  const patched = patch(root, newTree, oldTree);
  assert.strictEqual(dom.toHTML(patched), '<ul><li>a</li><li>b</li><li>c</li></ul>');
});

test('integration: a full counter-app patch sequence matches expected HTML at each step', () => {
  const view = (count) => h('div', { id: 'app' }, h('span', {}, `count: ${count}`), h('button', {}, 'increment'));

  const root = dom.createElement('root');
  root.appendChild(render(view(0)));
  assert.strictEqual(dom.toHTML(root.children[0]), '<div id="app"><span>count: 0</span><button>increment</button></div>');

  let prev = view(0);
  for (let count = 1; count <= 3; count++) {
    const next = view(count);
    updateElement(root, next, prev, 0);
    prev = next;
  }
  assert.strictEqual(dom.toHTML(root.children[0]), '<div id="app"><span>count: 3</span><button>increment</button></div>');
});

let failures = 0;
for (const { name, fn } of tests) {
  try {
    fn();
    console.log(`ok - ${name}`);
  } catch (err) {
    failures++;
    console.log(`FAIL - ${name}`);
    console.log(`  ${err.message}`);
  }
}

console.log(`\n${tests.length - failures}/${tests.length} passed`);
process.exit(failures === 0 ? 0 : 1);
