import { createStore, combineReducers, applyMiddleware, thunk, createLogger } from "../src/index.js";

function todos(state = [], action) {
  switch (action.type) {
    case "ADD_TODO":
      return [...state, { text: action.text, done: false }];
    case "COMPLETE_TODO":
      return state.map((todo, i) =>
        i === action.index ? { ...todo, done: true } : todo
      );
    default:
      return state;
  }
}

function visibilityFilter(state = "ALL", action) {
  return action.type === "SET_FILTER" ? action.filter : state;
}

const store = createStore(
  combineReducers({ todos, visibilityFilter }),
  applyMiddleware(thunk, createLogger())
);

store.subscribe(() => {
  console.log("  -> state is now:", JSON.stringify(store.getState()));
});

// A thunk: dispatches a plain action immediately, then another after a
// fake network delay. The store never sees "async" — thunk middleware
// intercepts the function and calls it with (dispatch, getState) instead
// of forwarding it to the reducer.
function addTodoRemotely(text) {
  return (dispatch) => {
    dispatch({ type: "ADD_TODO", text: `${text} (saving...)` });
    return new Promise((resolve) => {
      setTimeout(() => {
        dispatch({ type: "COMPLETE_TODO", index: 0 });
        resolve();
      }, 10);
    });
  };
}

console.log("dispatching a plain action:");
store.dispatch({ type: "SET_FILTER", filter: "ACTIVE" });

console.log("\ndispatching a thunk:");
await store.dispatch(addTodoRemotely("learn redux internals"));

console.log("\nfinal state:", JSON.stringify(store.getState(), null, 2));
