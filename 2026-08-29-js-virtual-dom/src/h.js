// Hyperscript: builds a plain-object virtual tree. No real node is
// touched here — h() is pure and just describes what the tree should
// look like.
function h(tag, props = {}, ...children) {
  return {
    tag,
    props: props || {},
    // flatten so callers can pass arrays (e.g. from .map()) inline
    children: children.flat(Infinity),
  };
}

module.exports = { h };
