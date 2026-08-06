// Lets dispatch() accept a function instead of a plain action object.
// The function is called with (dispatch, getState) instead of being
// forwarded down the chain, which is how async action creators work
// without the reducer or store ever knowing about promises/timers.
export const thunk = ({ dispatch, getState }) => (next) => (action) => {
  if (typeof action === "function") {
    return action(dispatch, getState);
  }
  return next(action);
};
