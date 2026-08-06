import { test } from "node:test";
import assert from "node:assert/strict";
import { createStore } from "../src/createStore.js";

function counter(state = 0, action) {
  switch (action.type) {
    case "INCREMENT":
      return state + 1;
    case "DECREMENT":
      return state - 1;
    default:
      return state;
  }
}

test("initializes state by dispatching an init action through the reducer", () => {
  const store = createStore(counter);
  assert.equal(store.getState(), 0);
});

test("preloadedState overrides the reducer's default", () => {
  const store = createStore(counter, 42);
  assert.equal(store.getState(), 42);
});

test("dispatch runs the reducer and updates state", () => {
  const store = createStore(counter);
  store.dispatch({ type: "INCREMENT" });
  store.dispatch({ type: "INCREMENT" });
  assert.equal(store.getState(), 2);
});

test("dispatch rejects actions without a type", () => {
  const store = createStore(counter);
  assert.throws(() => store.dispatch({}), /undefined "type"/);
});

test("dispatch rejects non-plain-object actions", () => {
  const store = createStore(counter);
  assert.throws(() => store.dispatch("INCREMENT"), /plain objects/);
  assert.throws(() => store.dispatch(() => {}), /plain objects/);
});

test("a reducer may not dispatch from inside itself", () => {
  const store = createStore((state = 0, action) => {
    if (action.type === "NESTED") {
      // eslint-disable-next-line no-use-before-define
      store.dispatch({ type: "INCREMENT" });
    }
    return state;
  });
  assert.throws(() => store.dispatch({ type: "NESTED" }), /Reducers may not dispatch/);
});

test("getState throws while a reducer is executing", () => {
  let sawError = false;
  const store = createStore((state = 0, action) => {
    if (action.type === "PROBE") {
      try {
        store.getState();
      } catch {
        sawError = true;
      }
    }
    return state;
  });
  store.dispatch({ type: "PROBE" });
  assert.equal(sawError, true);
});

test("subscribe registers a listener that fires on every dispatch", () => {
  const store = createStore(counter);
  let calls = 0;
  store.subscribe(() => calls++);
  store.dispatch({ type: "INCREMENT" });
  store.dispatch({ type: "INCREMENT" });
  assert.equal(calls, 2);
});

test("unsubscribe stops future notifications", () => {
  const store = createStore(counter);
  let calls = 0;
  const unsubscribe = store.subscribe(() => calls++);
  store.dispatch({ type: "INCREMENT" });
  unsubscribe();
  store.dispatch({ type: "INCREMENT" });
  assert.equal(calls, 1);
});

test("a listener that subscribes during dispatch is not called until the next dispatch", () => {
  const store = createStore(counter);
  const calls = [];
  store.subscribe(() => {
    calls.push("first");
    // Subscribing mid-dispatch must not retroactively join this round.
    store.subscribe(() => calls.push("late"));
  });
  store.dispatch({ type: "INCREMENT" });
  assert.deepEqual(calls, ["first"]);
  store.dispatch({ type: "INCREMENT" });
  assert.deepEqual(calls, ["first", "first", "late"]);
});

test("a listener that unsubscribes itself mid-dispatch still lets the rest of that round run", () => {
  const store = createStore(counter);
  const calls = [];
  let unsubscribeSelf;
  unsubscribeSelf = store.subscribe(() => {
    calls.push("self");
    unsubscribeSelf();
  });
  store.subscribe(() => calls.push("other"));
  store.dispatch({ type: "INCREMENT" });
  assert.deepEqual(calls, ["self", "other"]);
  calls.length = 0;
  store.dispatch({ type: "INCREMENT" });
  assert.deepEqual(calls, ["other"]);
});

test("replaceReducer hot-swaps the reducer but does NOT reset state", () => {
  // The new reducer's first call receives the store's *current* state as
  // its previous-state argument, not undefined — so a `= "unchanged"`
  // default parameter never kicks in here. This is what makes
  // replaceReducer a genuine hot-swap (e.g. for code-splitting reducers)
  // rather than a reset: whatever shape the old and new reducer share
  // survives the swap untouched.
  const store = createStore(counter);
  store.dispatch({ type: "INCREMENT" });
  assert.equal(store.getState(), 1);
  store.replaceReducer((state = "unchanged") => state);
  assert.equal(store.getState(), 1);
});
