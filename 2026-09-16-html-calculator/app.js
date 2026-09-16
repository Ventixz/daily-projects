// Wires the DOM to the pure session reducer: every input (click or key)
// becomes one `pressX` call, then a single `render(state, els)`. There is
// exactly one state object and one render call site, so the screen can
// never drift out of sync with `state` the way it easily could if handlers
// mutated the DOM directly.
import * as session from "./src/core/session.js";
import { render } from "./src/ui/render.js";
import { actionForKey } from "./src/ui/keyboard.js";

let state = session.initialState();

const els = {
  screen: document.querySelector('[data-testid="screen"]'),
  memoryIndicator: document.querySelector('[data-testid="memory-indicator"]'),
  historyList: document.querySelector('[data-testid="history-list"]'),
};

function dispatch(action) {
  switch (action.type) {
    case "digit":
      state = session.pressDigit(state, action.value);
      break;
    case "decimal":
      state = session.pressDecimal(state);
      break;
    case "operator":
      state = session.pressOperator(state, action.value);
      break;
    case "percent":
      state = session.pressPercent(state);
      break;
    case "toggle-sign":
      state = session.pressToggleSign(state);
      break;
    case "equals":
      state = session.pressEquals(state);
      break;
    case "clear":
      state = session.pressClear(state);
      break;
    case "clear-entry":
      state = session.pressClearEntry(state);
      break;
    case "backspace":
      state = session.pressBackspace(state);
      break;
    case "memory":
      state = session.pressMemory(state, action.value);
      break;
    default:
      return;
  }
  render(state, els);
}

document.querySelector(".keypad").addEventListener("click", (event) => {
  const button = event.target.closest("button[data-action]");
  if (!button) return;
  dispatch({ type: button.dataset.action, value: button.dataset.value });
});

document.querySelector(".history-list").addEventListener("click", (event) => {
  const entry = event.target.closest(".history-entry");
  if (!entry) return;
  // Clicking a past result recalls it the same way MR does -- ready to
  // extend with an operator, replaced outright by a fresh digit.
  state = { ...state, expression: entry.dataset.result, justEvaluated: true, lastOp: null };
  render(state, els);
});

window.addEventListener("keydown", (event) => {
  const action = actionForKey(event);
  if (!action) return;
  event.preventDefault();
  dispatch(action);
});

render(state, els);

// Exposed for the e2e suite: a test can read/reset state without scraping
// the DOM for numbers it just watched render.js format.
window.__calculator = {
  getState: () => state,
};
