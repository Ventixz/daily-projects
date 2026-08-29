// A minimal DOM stand-in. This environment has no browser, so the patch
// algorithm targets this instead of window.document — same node shape
// (tagName, attrs, children, appendChild/removeChild/replaceChild,
// setAttribute/removeAttribute), just enough of it to prove the vdom
// diff/patch logic actually mutates a tree correctly.

class TextNode {
  constructor(text) {
    this.nodeType = 'text';
    this.textContent = String(text);
    this.parent = null;
  }
}

class Element {
  constructor(tag) {
    this.nodeType = 'element';
    this.tagName = tag;
    this.attrs = {};
    this.children = [];
    this.parent = null;
  }

  setAttribute(name, value) {
    this.attrs[name] = value;
  }

  removeAttribute(name) {
    delete this.attrs[name];
  }

  appendChild(node) {
    node.parent = this;
    this.children.push(node);
  }

  // Inserts at an exact position rather than always at the end. diff.js
  // needs this: when it walks children back-to-front (see comment there),
  // a plain appendChild would land later-appended-but-lower-index nodes
  // after ones appended earlier in the same pass, reversing their order.
  insertChildAt(node, index) {
    node.parent = this;
    this.children.splice(index, 0, node);
  }

  removeChild(node) {
    const i = this.children.indexOf(node);
    if (i === -1) throw new Error('removeChild: node is not a child of this element');
    this.children.splice(i, 1);
    node.parent = null;
  }

  replaceChild(newNode, oldNode) {
    const i = this.children.indexOf(oldNode);
    if (i === -1) throw new Error('replaceChild: oldNode is not a child of this element');
    oldNode.parent = null;
    newNode.parent = this;
    this.children[i] = newNode;
  }
}

function createElement(tag) {
  return new Element(tag);
}

function createTextNode(text) {
  return new TextNode(text);
}

// Serializes a node to an HTML-ish string, sorting attrs for deterministic
// output — useful for asserting on tree shape in tests and for the demo.
function toHTML(node) {
  if (node.nodeType === 'text') return node.textContent;
  const attrs = Object.keys(node.attrs)
    .sort()
    .map((k) => ` ${k}="${node.attrs[k]}"`)
    .join('');
  const inner = node.children.map(toHTML).join('');
  return `<${node.tagName}${attrs}>${inner}</${node.tagName}>`;
}

module.exports = { Element, TextNode, createElement, createTextNode, toHTML };
