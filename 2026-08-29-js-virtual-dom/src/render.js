const dom = require('./dom');

// Turns a virtual node (a plain object from h(), or a string/number leaf)
// into a real node on the dom stand-in. This is the "mount from scratch"
// path — diff.js calls back into it whenever a subtree is new.
function createElementFromVNode(vnode) {
  if (typeof vnode === 'string' || typeof vnode === 'number') {
    return dom.createTextNode(vnode);
  }

  const el = dom.createElement(vnode.tag);
  for (const [key, value] of Object.entries(vnode.props)) {
    el.setAttribute(key, value);
  }
  for (const child of vnode.children) {
    el.appendChild(createElementFromVNode(child));
  }
  return el;
}

module.exports = { createElementFromVNode };
