import { openDb, getAll, put, remove } from "./src/storage/db.js";
import {
  History,
  addCommand,
  removeCommand,
  toggleCommand,
  editCommand,
  reorderCommand,
} from "./src/core/history.js";
import { createTodo, sortByOrder, filterTodos, orderForPosition } from "./src/core/todos.js";
import { renderList } from "./src/ui/render.js";

const listEl = document.getElementById("todo-list");
const formEl = document.getElementById("add-form");
const inputEl = document.getElementById("add-input");
const filterButtons = Array.from(document.querySelectorAll("[data-filter]"));
const undoBtn = document.getElementById("undo-btn");
const redoBtn = document.getElementById("redo-btn");

let history = new History([]);
let filter = "all";
let db;

// Generic before/after diff so every mutation path -- do, undo, and redo --
// persists through the same code, instead of each command needing to know
// its own inverse database write on top of its own inverse *state* change.
async function syncDb(before, after) {
  const beforeIds = new Set(before.map((t) => t.id));
  const afterById = new Map(after.map((t) => [t.id, t]));

  for (const id of beforeIds) {
    if (!afterById.has(id)) await remove(db, id);
  }
  for (const todo of after) {
    const prev = before.find((t) => t.id === todo.id);
    if (!prev || prev.text !== todo.text || prev.done !== todo.done || prev.order !== todo.order) {
      await put(db, todo);
    }
  }
}

function render() {
  const visible = filterTodos(sortByOrder(history.todos), filter);
  renderList(listEl, visible, {
    onToggle: (id) => mutate(toggleCommand(id)),
    onRemove: (id) => mutate(removeCommand(history.todos, id)),
    onEditCommit: (id, text) => {
      if (!text.trim()) {
        mutate(removeCommand(history.todos, id));
      } else {
        mutate(editCommand(history.todos, id, text));
      }
    },
    onReorder: (id, targetIndex) => {
      const newOrder = orderForPosition(history.todos, id, targetIndex);
      mutate(reorderCommand(history.todos, id, newOrder));
    },
  });

  undoBtn.disabled = !history.canUndo();
  redoBtn.disabled = !history.canRedo();
  filterButtons.forEach((btn) => {
    btn.classList.toggle("active", btn.dataset.filter === filter);
  });
}

// Event handlers below fire-and-forget these (checkbox `change`, drag `drop`,
// etc. don't await their handler's return value), so the write to IndexedDB
// happens strictly after the render that makes it *look* done. Tracking the
// in-flight write here lets the e2e harness ask "has the last mutation
// actually reached storage yet?" before it reloads the page to check.
let pendingSync = Promise.resolve();

function mutate(command) {
  const before = history.todos;
  const after = history.run(command);
  render();
  pendingSync = syncDb(before, after);
  return pendingSync;
}

function undo() {
  const before = history.todos;
  const after = history.undo();
  render();
  pendingSync = syncDb(before, after);
  return pendingSync;
}

function redo() {
  const before = history.todos;
  const after = history.redo();
  render();
  pendingSync = syncDb(before, after);
  return pendingSync;
}

formEl.addEventListener("submit", (event) => {
  event.preventDefault();
  const text = inputEl.value;
  if (!text.trim()) return;
  const todo = createTodo({
    id: crypto.randomUUID(),
    text,
    order: orderForPosition(history.todos, "__new__", history.todos.length),
    createdAt: Date.now(),
  });
  mutate(addCommand(todo));
  inputEl.value = "";
});

filterButtons.forEach((btn) => {
  btn.addEventListener("click", () => {
    filter = btn.dataset.filter;
    render();
  });
});

undoBtn.addEventListener("click", undo);
redoBtn.addEventListener("click", redo);

document.addEventListener("keydown", (event) => {
  const meta = event.ctrlKey || event.metaKey;
  if (!meta || event.key.toLowerCase() !== "z") return;
  event.preventDefault();
  if (event.shiftKey) redo();
  else undo();
});

async function main() {
  db = await openDb();
  const stored = await getAll(db);
  history = new History(sortByOrder(stored));
  render();
  // Exposed for the end-to-end test harness only: it needs to seed legacy
  // (pre-migration) records directly and to wait on in-flight db writes.
  window.__app = { history, db };
  window.__waitForSync = () => pendingSync;
}

// The e2e harness loads the page with ?manual=1 when it needs to seed
// IndexedDB with pre-migration data *before* openDb() runs the upgrade that
// would migrate it -- so it calls window.__initApp() itself once seeding is
// done, instead of racing this auto-init.
window.__initApp = main;
if (!new URLSearchParams(location.search).has("manual")) {
  main();
}
