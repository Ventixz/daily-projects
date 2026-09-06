// Command-pattern undo/redo. Each command captures only what it needs to
// invert itself (the removed record, the previous text, the previous order
// key) rather than snapshotting the whole list -- a full-state snapshot
// stack would also work for a list this small, but it doesn't generalize
// (it gets memory-expensive fast on a bigger document, and it can't merge
// with e.g. a server that's also mutating the list), so this repo builds
// the version that scales instead of the one that's easiest to type.

import { addTodo, removeTodo, toggleTodo, editTodo, setOrder } from "./todos.js";

function addCommand(todo) {
  return {
    label: "add",
    do: (todos) => addTodo(todos, todo),
    undo: (todos) => removeTodo(todos, todo.id),
  };
}

function removeCommand(todos, id) {
  const removed = todos.find((t) => t.id === id);
  if (!removed) throw new Error(`removeCommand: no todo with id ${id}`);
  return {
    label: "remove",
    do: (list) => removeTodo(list, id),
    // Re-insertion doesn't try to restore list position (that's `order`'s
    // job, and it's part of the captured record) -- addTodo just appends,
    // and sortByOrder puts it back where it belongs.
    undo: (list) => addTodo(list, removed),
  };
}

function toggleCommand(id) {
  // Toggling is its own inverse, so do and undo are the same function.
  return {
    label: "toggle",
    do: (todos) => toggleTodo(todos, id),
    undo: (todos) => toggleTodo(todos, id),
  };
}

function editCommand(todos, id, newText) {
  const prev = todos.find((t) => t.id === id);
  if (!prev) throw new Error(`editCommand: no todo with id ${id}`);
  const prevText = prev.text;
  return {
    label: "edit",
    do: (list) => editTodo(list, id, newText),
    undo: (list) => editTodo(list, id, prevText),
  };
}

function reorderCommand(todos, id, newOrder) {
  const prev = todos.find((t) => t.id === id);
  if (!prev) throw new Error(`reorderCommand: no todo with id ${id}`);
  const prevOrder = prev.order;
  return {
    label: "reorder",
    do: (list) => setOrder(list, id, newOrder),
    undo: (list) => setOrder(list, id, prevOrder),
  };
}

class History {
  constructor(initialTodos = []) {
    this.todos = initialTodos;
    this._undoStack = [];
    this._redoStack = [];
  }

  run(command) {
    this.todos = command.do(this.todos);
    this._undoStack.push(command);
    this._redoStack = [];
    return this.todos;
  }

  canUndo() {
    return this._undoStack.length > 0;
  }

  canRedo() {
    return this._redoStack.length > 0;
  }

  undo() {
    const command = this._undoStack.pop();
    if (!command) return this.todos;
    this.todos = command.undo(this.todos);
    this._redoStack.push(command);
    return this.todos;
  }

  redo() {
    const command = this._redoStack.pop();
    if (!command) return this.todos;
    this.todos = command.do(this.todos);
    this._undoStack.push(command);
    return this.todos;
  }
}

export {
  History,
  addCommand,
  removeCommand,
  toggleCommand,
  editCommand,
  reorderCommand,
};
