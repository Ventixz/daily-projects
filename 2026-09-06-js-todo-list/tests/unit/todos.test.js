import { test, assert, assertEqual, assertThrows } from "./harness.js";
import {
  createTodo,
  addTodo,
  removeTodo,
  toggleTodo,
  editTodo,
  setOrder,
  sortByOrder,
  filterTodos,
  orderForPosition,
} from "../../src/core/todos.js";

test("createTodo trims text and defaults done to false", () => {
  const t = createTodo({ id: "1", text: "  buy milk  ", order: "5", createdAt: 1 });
  assertEqual(t, { id: "1", text: "buy milk", done: false, order: "5", createdAt: 1 });
});

test("createTodo rejects empty text", () => {
  assertThrows(() => createTodo({ id: "1", text: "   ", order: "5", createdAt: 1 }));
});

test("addTodo appends without mutating the input array", () => {
  const before = [];
  const after = addTodo(before, { id: "1" });
  assert(before.length === 0, "input array must not be mutated");
  assertEqual(after, [{ id: "1" }]);
});

test("removeTodo removes only the matching id", () => {
  const todos = [{ id: "1" }, { id: "2" }];
  assertEqual(removeTodo(todos, "1"), [{ id: "2" }]);
});

test("toggleTodo flips done and leaves other todos alone", () => {
  const todos = [
    { id: "1", done: false },
    { id: "2", done: false },
  ];
  assertEqual(toggleTodo(todos, "1"), [
    { id: "1", done: true },
    { id: "2", done: false },
  ]);
});

test("editTodo updates text and trims it", () => {
  const todos = [{ id: "1", text: "old" }];
  assertEqual(editTodo(todos, "1", "  new text  "), [{ id: "1", text: "new text" }]);
});

test("editTodo rejects empty text", () => {
  assertThrows(() => editTodo([{ id: "1", text: "old" }], "1", "   "));
});

test("setOrder updates only the order field", () => {
  const todos = [{ id: "1", order: "5" }];
  assertEqual(setOrder(todos, "1", "3"), [{ id: "1", order: "3" }]);
});

test("sortByOrder sorts ascending without mutating input", () => {
  const todos = [
    { id: "b", order: "9" },
    { id: "a", order: "1" },
  ];
  const sorted = sortByOrder(todos);
  assertEqual(
    sorted.map((t) => t.id),
    ["a", "b"],
  );
  assertEqual(
    todos.map((t) => t.id),
    ["b", "a"],
    "original array order must be untouched",
  );
});

test("filterTodos: all/active/completed", () => {
  const todos = [
    { id: "1", done: false },
    { id: "2", done: true },
  ];
  assertEqual(
    filterTodos(todos, "all").map((t) => t.id),
    ["1", "2"],
  );
  assertEqual(
    filterTodos(todos, "active").map((t) => t.id),
    ["1"],
  );
  assertEqual(
    filterTodos(todos, "completed").map((t) => t.id),
    ["2"],
  );
});

test("orderForPosition at the start, middle, and end of a list", () => {
  const todos = [
    { id: "a", order: "1" },
    { id: "b", order: "3" },
    { id: "c", order: "5" },
  ];
  const start = orderForPosition(todos, "__new__", 0);
  assert(start < "1", `expected key before "1", got ${start}`);

  const middle = orderForPosition(todos, "__new__", 1);
  assert(middle > "1" && middle < "3", `expected key between "1" and "3", got ${middle}`);

  const end = orderForPosition(todos, "__new__", 3);
  assert(end > "5", `expected key after "5", got ${end}`);
});

test("orderForPosition excludes the item being moved from its own neighbor calculation", () => {
  const todos = [
    { id: "a", order: "1" },
    { id: "b", order: "3" },
    { id: "c", order: "5" },
  ];
  // Moving "b" to the end must compare against "a" and "c" only -- if it
  // didn't exclude "b" itself, "b"'s own order ("3") could leak in as a
  // neighbor and produce a key that isn't actually past "c".
  const moved = orderForPosition(todos, "b", 2);
  assert(moved > "5", `expected key after "5" when moving "b" to the end, got ${moved}`);
});

test("orderForPosition on a single-item list places it unambiguously", () => {
  const todos = [{ id: "a", order: "5" }];
  const onlySlot = orderForPosition(todos, "__new__", 0);
  assert(onlySlot < "5", `expected key before the only existing item, got ${onlySlot}`);
});
