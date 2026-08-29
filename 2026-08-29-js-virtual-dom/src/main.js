// Demo: a tiny counter "app" re-rendered from scratch on every tick,
// patched onto the same real root each time — the point being that the
// vdom diff only touches what actually changed.
const { h } = require('./h');
const dom = require('./dom');
const { createElementFromVNode } = require('./render');
const { updateElement } = require('./diff');

function view(count) {
  return h(
    'div',
    { id: 'app' },
    h('span', {}, `count: ${count}`),
    h('button', { disabled: count >= 5 ? 'true' : 'false' }, 'increment'),
  );
}

const root = dom.createElement('root');
let tree = view(0);
root.appendChild(createElementFromVNode(tree));
console.log(`tick 0 -> ${dom.toHTML(root.children[0])}`);

for (let count = 1; count <= 5; count++) {
  const nextTree = view(count);
  updateElement(root, nextTree, tree, 0);
  tree = nextTree;
  console.log(`tick ${count} -> ${dom.toHTML(root.children[0])}`);
}
