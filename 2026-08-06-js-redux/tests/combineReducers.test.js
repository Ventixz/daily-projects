import { test } from "node:test";
import assert from "node:assert/strict";
import { createStore } from "../src/createStore.js";
import { combineReducers } from "../src/combineReducers.js";

function todos(state = [], action) {
  switch (action.type) {
    case "ADD":
      return [...state, action.text];
    default:
      return state;
  }
}

function visibilityFilter(state = "ALL", action) {
  switch (action.type) {
    case "SET_FILTER":
      return action.filter;
    default:
      return state;
  }
}

test("combines independent reducers under their own state keys", () => {
  const store = createStore(combineReducers({ todos, visibilityFilter }));
  assert.deepEqual(store.getState(), { todos: [], visibilityFilter: "ALL" });
  store.dispatch({ type: "ADD", text: "learn redux" });
  store.dispatch({ type: "SET_FILTER", filter: "DONE" });
  assert.deepEqual(store.getState(), {
    todos: ["learn redux"],
    visibilityFilter: "DONE",
  });
});

test("returns the exact same state reference when no slice changed", () => {
  const store = createStore(combineReducers({ todos, visibilityFilter }));
  const before = store.getState();
  store.dispatch({ type: "NOOP" });
  assert.equal(store.getState(), before);
});

test("returns a new top-level object when only one slice changed", () => {
  const store = createStore(combineReducers({ todos, visibilityFilter }));
  const before = store.getState();
  store.dispatch({ type: "SET_FILTER", filter: "DONE" });
  const after = store.getState();
  assert.notEqual(after, before);
  // The untouched slice keeps its own reference even though the parent object is new.
  assert.equal(after.todos, before.todos);
});

test("a bad reducer shape is caught at combine time but only thrown on first use", () => {
  // combineReducers() itself never throws — assertReducerShape() runs
  // eagerly and any error is *captured*, not raised, because the
  // combination function it returns has to be an ordinary reducer (no
  // exceptions escaping just from constructing it). The stored error only
  // surfaces the first time something actually calls the combined reducer.
  const broken = (state, action) => {
    if (action.type === "@@redux-clone/INIT") return undefined;
    return state ?? 0;
  };
  const combined = combineReducers({ broken });
  assert.doesNotThrow(() => combined);
  assert.throws(
    () => combined(undefined, { type: "ANYTHING" }),
    /returned undefined during initialization/
  );
});

test("throws if a reducer returns undefined for an unknown action type", () => {
  const store = createStore(combineReducers({ todos, visibilityFilter }));
  const badReducers = combineReducers({
    todos,
    visibilityFilter: (state = "ALL", action) =>
      action.type === "EXPLODE" ? undefined : visibilityFilter(state, action),
  });
  assert.throws(() => badReducers(store.getState(), { type: "EXPLODE" }), /returned undefined handling "EXPLODE"/);
});

test("warns (does not throw) about unexpected keys already present in state", (t) => {
  const combined = combineReducers({ todos });
  const originalError = console.error;
  const messages = [];
  console.error = (msg) => messages.push(msg);
  t.after(() => {
    console.error = originalError;
  });

  combined({ todos: [], ghost: "leftover" }, { type: "ADD", text: "x" });
  assert.equal(messages.length, 1);
  assert.match(messages[0], /Unexpected key "ghost"/);
});
