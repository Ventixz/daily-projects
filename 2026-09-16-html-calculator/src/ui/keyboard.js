// Maps a KeyboardEvent to the same action names app.js's button click
// handler uses, so both input paths share one dispatch function instead of
// keyboard shortcuts silently drifting out of sync with the buttons.

function actionForKey(event) {
  const { key } = event;
  if (/^[0-9]$/.test(key)) return { type: "digit", value: key };
  if (key === ".") return { type: "decimal" };
  if (key === "+" || key === "-" || key === "*" || key === "/") return { type: "operator", value: key };
  if (key === "Enter" || key === "=") return { type: "equals" };
  if (key === "Escape") return { type: "clear" };
  if (key === "Delete") return { type: "clear-entry" };
  if (key === "Backspace") return { type: "backspace" };
  if (key === "%") return { type: "percent" };
  return null;
}

export { actionForKey };
