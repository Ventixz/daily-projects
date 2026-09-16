// Evaluates an RPN token stream (from parser.js) and applies single binary
// operations directly (used by session.js for repeat-`=`, which reapplies
// the last operator/operand pair without re-running the whole parser).

function applyBinary(op, a, b) {
  switch (op) {
    case "+":
      return a + b;
    case "-":
      return a - b;
    case "*":
      return a * b;
    case "/":
      if (b === 0) throw new Error("division by zero");
      return a / b;
    default:
      throw new Error(`unknown operator "${op}"`);
  }
}

function evalRPN(rpn) {
  const stack = [];
  for (const t of rpn) {
    if (t.type === "number") {
      stack.push(t.value);
      continue;
    }
    if (t.value === "u-") {
      if (stack.length < 1) throw new Error("malformed expression");
      stack.push(-stack.pop());
      continue;
    }
    if (stack.length < 2) throw new Error("malformed expression");
    const b = stack.pop();
    const a = stack.pop();
    stack.push(applyBinary(t.value, a, b));
  }
  if (stack.length !== 1) throw new Error("malformed expression");
  return stack[0];
}

export { applyBinary, evalRPN };
