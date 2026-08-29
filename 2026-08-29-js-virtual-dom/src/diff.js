const { createElementFromVNode } = require('./render');

function isText(vnode) {
  return typeof vnode === 'string' || typeof vnode === 'number';
}

// Decides whether a real node has to be thrown away and rebuilt, vs.
// patched in place. Deliberately simple: no keys, so a text/text swap
// with different content or a tag swap both count as "changed" even
// though a smarter differ could patch text-in-place.
function changed(a, b) {
  if (typeof a !== typeof b) return true;
  if (isText(a) && isText(b)) return String(a) !== String(b);
  if (isText(a) !== isText(b)) return true;
  return a.tag !== b.tag;
}

function updateProps(el, newProps = {}, oldProps = {}) {
  for (const [key, value] of Object.entries(newProps)) {
    if (oldProps[key] !== value) el.setAttribute(key, value);
  }
  for (const key of Object.keys(oldProps)) {
    if (!(key in newProps)) el.removeAttribute(key);
  }
}

// The core algorithm: walk newTree/oldTree together, and mutate `parent`
// (a real node) to match newTree. index tracks position among siblings
// since there's nothing else — no keys — to correlate old and new nodes.
function updateElement(parent, newNode, oldNode, index = 0) {
  if (oldNode === undefined) {
    parent.insertChildAt(createElementFromVNode(newNode), index);
    return;
  }

  if (newNode === undefined) {
    parent.removeChild(parent.children[index]);
    return;
  }

  if (changed(newNode, oldNode)) {
    parent.replaceChild(createElementFromVNode(newNode), parent.children[index]);
    return;
  }

  if (isText(newNode)) return; // unchanged text, nothing to do

  const el = parent.children[index];
  updateProps(el, newNode.props, oldNode.props);

  const max = Math.max(newNode.children.length, oldNode.children.length);
  // Walk back-to-front so that a removal (splice) doesn't shift the
  // index of children not yet visited in this same pass.
  for (let i = max - 1; i >= 0; i--) {
    updateElement(el, newNode.children[i], oldNode.children[i], i);
  }
}

module.exports = { changed, updateElement, updateProps };
