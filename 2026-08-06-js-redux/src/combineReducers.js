import { INIT_ACTION } from "./createStore.js";

function assertReducerShape(reducers) {
  Object.keys(reducers).forEach((key) => {
    const reducer = reducers[key];
    const initialState = reducer(undefined, INIT_ACTION);
    if (typeof initialState === "undefined") {
      throw new Error(
        `Reducer "${key}" returned undefined during initialization. ` +
          "If the state passed to the reducer is undefined, you must " +
          "explicitly return the initial state."
      );
    }
    const probeType = `@@redux-clone/PROBE_UNKNOWN_ACTION_${Math.random()
      .toString(36)
      .slice(2)}`;
    if (typeof reducer(undefined, { type: probeType }) === "undefined") {
      throw new Error(
        `Reducer "${key}" returned undefined when probed with a random ` +
          "action type. Don't try to handle actions in \"redux/*\" " +
          "namespace. They are considered private."
      );
    }
  });
}

export function combineReducers(reducers) {
  const reducerKeys = Object.keys(reducers);
  const finalReducers = {};
  for (const key of reducerKeys) {
    if (typeof reducers[key] === "function") {
      finalReducers[key] = reducers[key];
    }
  }
  const finalReducerKeys = Object.keys(finalReducers);

  let shapeAssertionError;
  try {
    assertReducerShape(finalReducers);
  } catch (e) {
    shapeAssertionError = e;
  }

  return function combination(state = {}, action) {
    if (shapeAssertionError) {
      throw shapeAssertionError;
    }

    const unexpectedKeys = Object.keys(state).filter(
      (key) => !finalReducers.hasOwnProperty(key)
    );
    if (unexpectedKeys.length > 0) {
      console.error(
        `Unexpected ${unexpectedKeys.length > 1 ? "keys" : "key"} ` +
          `"${unexpectedKeys.join('", "')}" found in state. Expected to find ` +
          `one of the known reducer keys instead: "${finalReducerKeys.join(
            '", "'
          )}". Unexpected keys will be ignored.`
      );
    }

    let hasChanged = false;
    const nextState = {};
    for (const key of finalReducerKeys) {
      const reducer = finalReducers[key];
      const previousStateForKey = state[key];
      const nextStateForKey = reducer(previousStateForKey, action);
      if (typeof nextStateForKey === "undefined") {
        throw new Error(`Reducer "${key}" returned undefined handling "${action.type}".`);
      }
      nextState[key] = nextStateForKey;
      hasChanged = hasChanged || nextStateForKey !== previousStateForKey;
    }
    hasChanged =
      hasChanged || finalReducerKeys.length !== Object.keys(state).length;

    // Preserve the previous reference when nothing actually changed, so
    // downstream `===` checks (memoized selectors, React re-render bail-outs)
    // still work the way they would against a hand-written root reducer.
    return hasChanged ? nextState : state;
  };
}
