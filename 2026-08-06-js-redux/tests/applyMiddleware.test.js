import { test } from "node:test";
import assert from "node:assert/strict";
import { createStore } from "../src/createStore.js";
import { applyMiddleware } from "../src/applyMiddleware.js";
import { thunk } from "../src/middleware/thunk.js";
import { createLogger } from "../src/middleware/logger.js";

function counter(state = 0, action) {
  switch (action.type) {
    case "INCREMENT":
      return state + 1;
    default:
      return state;
  }
}

test("applyMiddleware runs middleware in the order given, outermost first", () => {
  const trace = [];
  const tag = (name) => () => (next) => (action) => {
    trace.push(`${name}:before`);
    const result = next(action);
    trace.push(`${name}:after`);
    return result;
  };
  const store = createStore(counter, applyMiddleware(tag("a"), tag("b")));
  trace.length = 0; // ignore the INIT dispatch
  store.dispatch({ type: "INCREMENT" });
  assert.deepEqual(trace, ["a:before", "b:before", "b:after", "a:after"]);
});

test("thunk middleware lets dispatch accept a function", () => {
  const store = createStore(counter, applyMiddleware(thunk));
  store.dispatch((dispatch, getState) => {
    assert.equal(getState(), 0);
    dispatch({ type: "INCREMENT" });
  });
  assert.equal(store.getState(), 1);
});

test("thunk supports async action creators that dispatch after a delay", async () => {
  const store = createStore(counter, applyMiddleware(thunk));
  const asyncIncrement = () => (dispatch) =>
    new Promise((resolve) => {
      setTimeout(() => {
        dispatch({ type: "INCREMENT" });
        resolve();
      }, 0);
    });
  await store.dispatch(asyncIncrement());
  assert.equal(store.getState(), 1);
});

test("logger middleware observes state before and after each action", () => {
  const events = [];
  const logger = createLogger((entry) => events.push(entry));
  const store = createStore(counter, applyMiddleware(logger));
  events.length = 0; // ignore the INIT dispatch
  store.dispatch({ type: "INCREMENT" });
  assert.equal(events.length, 2);
  assert.equal(events[0].type, "prev");
  assert.equal(events[0].state, 0);
  assert.equal(events[1].type, "next");
  assert.equal(events[1].state, 1);
});

test("dispatching before the middleware chain finishes constructing throws", () => {
  // A middleware that calls store-level dispatch synchronously, during its
  // own construction (not from within the (next) => (action) => ... layer,
  // but eagerly in the middleware(api) call itself), must hit the
  // placeholder guard rather than a real dispatch function that doesn't
  // exist yet — `dispatch` is only reassigned after every middleware(api)
  // call has returned.
  const eagerMiddleware = (api) => {
    assert.throws(
      () => api.dispatch({ type: "TOO_EARLY" }),
      /Dispatching while constructing/
    );
    return (next) => (action) => next(action);
  };
  createStore(counter, applyMiddleware(eagerMiddleware));
});
