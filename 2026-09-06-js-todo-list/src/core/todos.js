// Pure, storage-free todo list logic. Every function takes a todos array and
// returns a new one; nothing here touches IndexedDB, the DOM, or Date.now()
// directly (callers pass ids/timestamps in), so it's testable with plain
// arrays and no browser.

import { keyBetween } from "./order.js";

function createTodo({ id, text, order, createdAt }) {
  if (!text || !text.trim()) throw new Error("todo text must not be empty");
  return { id, text: text.trim(), done: false, order, createdAt };
}

function addTodo(todos, todo) {
  return [...todos, todo];
}

function removeTodo(todos, id) {
  return todos.filter((t) => t.id !== id);
}

function toggleTodo(todos, id) {
  return todos.map((t) => (t.id === id ? { ...t, done: !t.done } : t));
}

function editTodo(todos, id, text) {
  if (!text || !text.trim()) throw new Error("todo text must not be empty");
  return todos.map((t) => (t.id === id ? { ...t, text: text.trim() } : t));
}

function setOrder(todos, id, order) {
  return todos.map((t) => (t.id === id ? { ...t, order } : t));
}

function sortByOrder(todos) {
  return [...todos].sort((a, b) => (a.order < b.order ? -1 : a.order > b.order ? 1 : 0));
}

function filterTodos(todos, filter) {
  switch (filter) {
    case "active":
      return todos.filter((t) => !t.done);
    case "completed":
      return todos.filter((t) => t.done);
    case "all":
    default:
      return todos;
  }
}

// Computes the order key that would place `movingId` at `targetIndex` in the
// sorted, non-moving list -- i.e. "as if the item you're dragging weren't
// there yet, what key sits between its new neighbors."
function orderForPosition(todos, movingId, targetIndex) {
  const rest = sortByOrder(todos.filter((t) => t.id !== movingId));
  const clamped = Math.max(0, Math.min(targetIndex, rest.length));
  const lo = clamped > 0 ? rest[clamped - 1].order : null;
  const hi = clamped < rest.length ? rest[clamped].order : null;
  return keyBetween(lo, hi);
}

export {
  createTodo,
  addTodo,
  removeTodo,
  toggleTodo,
  editTodo,
  setOrder,
  sortByOrder,
  filterTodos,
  orderForPosition,
};
