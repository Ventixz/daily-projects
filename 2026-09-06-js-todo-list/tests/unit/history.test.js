import { test, assert, assertEqual } from "./harness.js";
import {
  History,
  addCommand,
  removeCommand,
  toggleCommand,
  editCommand,
  reorderCommand,
} from "../../src/core/history.js";

test("addCommand: do adds the todo, undo removes it", () => {
  const todo = { id: "1", text: "a", done: false, order: "5" };
  const cmd = addCommand(todo);
  assertEqual(cmd.do([]), [todo]);
  assertEqual(cmd.undo([todo]), []);
});

test("removeCommand: undo restores the exact removed record, order included", () => {
  const todos = [{ id: "1", text: "a", done: true, order: "3" }];
  const cmd = removeCommand(todos, "1");
  const after = cmd.do(todos);
  assertEqual(after, []);
  assertEqual(cmd.undo(after), todos);
});

test("toggleCommand is its own inverse", () => {
  const cmd = toggleCommand("1");
  const todos = [{ id: "1", done: false }];
  const toggled = cmd.do(todos);
  assertEqual(toggled, [{ id: "1", done: true }]);
  assertEqual(cmd.undo(toggled), todos);
});

test("editCommand: undo restores the previous text, not empty text", () => {
  const todos = [{ id: "1", text: "old" }];
  const cmd = editCommand(todos, "1", "new");
  const after = cmd.do(todos);
  assertEqual(after, [{ id: "1", text: "new" }]);
  assertEqual(cmd.undo(after), todos);
});

test("reorderCommand: undo restores the previous order key", () => {
  const todos = [{ id: "1", order: "5" }];
  const cmd = reorderCommand(todos, "1", "1");
  const after = cmd.do(todos);
  assertEqual(after, [{ id: "1", order: "1" }]);
  assertEqual(cmd.undo(after), todos);
});

test("History: run/undo/redo replays a sequence correctly", () => {
  const h = new History([]);
  const todo = { id: "1", text: "a", done: false, order: "5" };

  h.run(addCommand(todo));
  h.run(toggleCommand("1"));
  assertEqual(h.todos, [{ id: "1", text: "a", done: true, order: "5" }]);

  h.undo(); // undo toggle
  assertEqual(h.todos, [{ id: "1", text: "a", done: false, order: "5" }]);

  h.undo(); // undo add
  assertEqual(h.todos, []);
  assert(!h.canUndo());

  h.redo(); // redo add
  h.redo(); // redo toggle
  assertEqual(h.todos, [{ id: "1", text: "a", done: true, order: "5" }]);
  assert(!h.canRedo());
});

test("History: undo() and redo() on an empty stack are no-ops", () => {
  const h = new History([{ id: "1" }]);
  assert(!h.canUndo());
  assert(!h.canRedo());
  h.undo();
  h.redo();
  assertEqual(h.todos, [{ id: "1" }]);
});

test("History: running a new command after undo discards the redo branch", () => {
  const h = new History([]);
  const a = { id: "1", text: "a", done: false, order: "1" };
  const b = { id: "2", text: "b", done: false, order: "2" };

  h.run(addCommand(a));
  h.undo();
  assert(h.canRedo());

  h.run(addCommand(b));
  assert(!h.canRedo(), "starting a new branch should clear the old redo history");
  assertEqual(h.todos, [b]);
});
