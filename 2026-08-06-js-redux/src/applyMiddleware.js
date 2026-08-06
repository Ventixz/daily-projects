import { compose } from "./compose.js";

export function applyMiddleware(...middlewares) {
  return (createStore) =>
    (reducer, preloadedState) => {
      const store = createStore(reducer, preloadedState);
      let dispatch = () => {
        throw new Error(
          "Dispatching while constructing your middleware is not allowed. " +
            "Other middleware would not be applied to this dispatch."
        );
      };

      const middlewareAPI = {
        getState: store.getState,
        dispatch: (action, ...args) => dispatch(action, ...args),
      };
      const chain = middlewares.map((middleware) => middleware(middlewareAPI));
      // Each middleware wraps the next one's `next`, innermost being the
      // store's real dispatch — so composing left-to-right through `chain`
      // has to happen via compose (right-to-left call order) to keep
      // middleware[0] as the outermost layer callers actually hit first.
      dispatch = compose(...chain)(store.dispatch);

      return { ...store, dispatch };
    };
}
