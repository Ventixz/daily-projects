// Pure-ish rendering: takes a session state and a set of DOM elements and
// writes to them. No event listeners live here (that's app.js/keyboard.js)
// -- render.js only ever goes state -> DOM, never the other way.

const OPERATOR_SYMBOLS = { "+": "+", "-": "−", "*": "×", "/": "÷" };

// Swaps internal ASCII operators for the display glyphs a calculator
// screen actually uses. Applied only here, at render time -- the state
// itself (and history entries) always store the tokenizer's plain +-*/ so
// they stay directly re-tokenizable.
function toDisplaySymbols(text) {
  return text.replace(/[+\-*/]/g, (c) => OPERATOR_SYMBOLS[c]);
}

function render(state, els) {
  const screenText = state.error ? "Error" : state.expression === "" ? "0" : toDisplaySymbols(state.expression);
  els.screen.textContent = screenText;
  els.screen.classList.toggle("error", state.error);

  els.memoryIndicator.classList.toggle("visible", state.memory !== 0);

  els.historyList.innerHTML = "";
  for (const entry of state.history) {
    const li = document.createElement("li");
    li.className = "history-entry";
    li.dataset.result = entry.result;
    const exprSpan = document.createElement("span");
    exprSpan.className = "history-expr";
    exprSpan.textContent = `${toDisplaySymbols(entry.expr)} =`;
    const resultSpan = document.createElement("span");
    resultSpan.className = "history-result";
    resultSpan.textContent = toDisplaySymbols(entry.result);
    li.append(exprSpan, resultSpan);
    els.historyList.append(li);
  }
  els.historyList.scrollTop = els.historyList.scrollHeight;
}

export { render, toDisplaySymbols, OPERATOR_SYMBOLS };
