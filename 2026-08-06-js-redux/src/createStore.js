export const INIT_ACTION = { type: "@@redux-clone/INIT" };

function isPlainObject(obj) {
  if (typeof obj !== "object" || obj === null) return false;
  let proto = obj;
  while (Object.getPrototypeOf(proto) !== null) {
    proto = Object.getPrototypeOf(proto);
  }
  return Object.getPrototypeOf(obj) === proto;
}

export function createStore(reducer, preloadedState, enhancer) {
  if (typeof preloadedState === "function" && typeof enhancer === "undefined") {
    enhancer = preloadedState;
    preloadedState = undefined;
  }

  if (typeof enhancer !== "undefined") {
    if (typeof enhancer !== "function") {
      throw new Error("Expected the enhancer to be a function.");
    }
    return enhancer(createStore)(reducer, preloadedState);
  }

  if (typeof reducer !== "function") {
    throw new Error("Expected the reducer to be a function.");
  }

  let currentReducer = reducer;
  let currentState = preloadedState;
  let currentListeners = [];
  let nextListeners = currentListeners;
  let isDispatching = false;

  // Copy-on-write: mutating `nextListeners` while a dispatch is iterating
  // `currentListeners` must never affect the listeners that dispatch already
  // committed to calling this round.
  function ensureCanMutateNextListeners() {
    if (nextListeners === currentListeners) {
      nextListeners = currentListeners.slice();
    }
  }

  function getState() {
    if (isDispatching) {
      throw new Error(
        "You may not call store.getState() while the reducer is executing. " +
          "The reducer has already received the state as an argument. " +
          "Pass it down from the top reducer instead of reading it from the store."
      );
    }
    return currentState;
  }

  function subscribe(listener) {
    if (typeof listener !== "function") {
      throw new Error("Expected the listener to be a function.");
    }
    if (isDispatching) {
      throw new Error(
        "You may not call store.subscribe() while the reducer is executing."
      );
    }

    let isSubscribed = true;
    ensureCanMutateNextListeners();
    nextListeners.push(listener);

    return function unsubscribe() {
      if (!isSubscribed) return;
      if (isDispatching) {
        throw new Error(
          "You may not unsubscribe from a store listener while the reducer is executing."
        );
      }
      isSubscribed = false;
      ensureCanMutateNextListeners();
      const index = nextListeners.indexOf(listener);
      nextListeners.splice(index, 1);
      currentListeners = null;
    };
  }

  function dispatch(action) {
    if (!isPlainObject(action)) {
      throw new Error(
        "Actions must be plain objects. Use custom middleware for async actions."
      );
    }
    if (typeof action.type === "undefined") {
      throw new Error('Actions may not have an undefined "type" property.');
    }
    if (isDispatching) {
      throw new Error("Reducers may not dispatch actions.");
    }

    try {
      isDispatching = true;
      currentState = currentReducer(currentState, action);
    } finally {
      isDispatching = false;
    }

    // Snapshot the listener list at the moment dispatch runs. A listener that
    // subscribes or unsubscribes *during* this call must not change which
    // listeners get notified this round — only the next one.
    const listeners = (currentListeners = nextListeners);
    for (const listener of listeners) {
      listener();
    }

    return action;
  }

  function replaceReducer(nextReducer) {
    if (typeof nextReducer !== "function") {
      throw new Error("Expected the nextReducer to be a function.");
    }
    currentReducer = nextReducer;
    dispatch({ type: "@@redux-clone/REPLACE" });
  }

  // Populate the initial state tree so every reducer's default case runs once.
  dispatch(INIT_ACTION);

  return { getState, dispatch, subscribe, replaceReducer };
}
