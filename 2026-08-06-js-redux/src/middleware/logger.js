// A sink function is injected (defaults to console.log) purely so tests can
// assert on what would have been logged instead of scraping stdout.
export function createLogger(sink = console.log) {
  return ({ getState }) =>
    (next) =>
    (action) => {
      const prevState = getState();
      sink({ type: "prev", state: prevState, action });
      const result = next(action);
      sink({ type: "next", state: getState(), action });
      return result;
    };
}
