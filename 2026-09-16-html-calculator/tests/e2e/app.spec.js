import { test, assert, assertEqual } from "./harness.js";

function key(id) {
  return `[data-testid="key-${id}"]`;
}

async function press(page, ...ids) {
  for (const id of ids) {
    await page.click(key(id));
  }
}

async function screenText(page) {
  return page.textContent('[data-testid="screen"]');
}

test("clicking digit and operator buttons builds and evaluates an expression", async (page) => {
  await press(page, "3", "plus", "4", "multiply", "2", "equals");
  assertEqual(await screenText(page), "11");
});

test("the operator glyphs shown on screen are display symbols, not the raw characters", async (page) => {
  await press(page, "5", "minus", "2");
  const text = await screenText(page);
  assert(text.includes("−"), `expected a minus sign glyph, got "${text}"`);
});

test("division by zero shows Error and the screen gets the error style", async (page) => {
  await press(page, "5", "divide", "0", "equals");
  assertEqual(await screenText(page), "Error");
  const hasErrorClass = await page.evaluate(() => document.querySelector('[data-testid="screen"]').classList.contains("error"));
  assert(hasErrorClass, "expected .error class on the screen after a division by zero");
});

test("keyboard input drives the calculator the same as clicking", async (page) => {
  await page.keyboard.type("12+8");
  await page.keyboard.press("Enter");
  assertEqual(await screenText(page), "20");
});

test("Escape acts as AC and Backspace edits the current entry", async (page) => {
  await page.keyboard.type("123");
  await page.keyboard.press("Backspace");
  assertEqual(await screenText(page), "12");
  await page.keyboard.press("Escape");
  assertEqual(await screenText(page), "0");
});

test("a completed calculation is appended to the visible history list", async (page) => {
  await press(page, "6", "plus", "1", "equals");
  const entries = await page.$$eval(".history-entry", (nodes) => nodes.map((n) => n.textContent));
  assert(
    entries.some((t) => t.includes("7")),
    `expected a history entry containing the result 7, got ${JSON.stringify(entries)}`,
  );
});

test("clicking a history entry recalls it, ready to chain with an operator", async (page) => {
  await press(page, "6", "plus", "1", "equals"); // -> 7, logged to history
  await press(page, "9"); // start an unrelated fresh calculation
  await page.click(".history-entry");
  await press(page, "plus", "3", "equals");
  assertEqual(await screenText(page), "10");
});

test("M+ then MR round-trips a value through the memory register", async (page) => {
  await press(page, "9", "Mplus", "AC", "MR");
  assertEqual(await screenText(page), "9");
});

test("the memory indicator only shows once the register is non-zero", async (page) => {
  const before = await page.evaluate(() =>
    document.querySelector('[data-testid="memory-indicator"]').classList.contains("visible"),
  );
  assert(!before, "expected memory indicator hidden before any M+ press");

  await press(page, "5", "Mplus");
  const after = await page.evaluate(() =>
    document.querySelector('[data-testid="memory-indicator"]').classList.contains("visible"),
  );
  assert(after, "expected memory indicator visible after M+");
});

test("percent behaves contextually: 100 + 10% is 110, not 100 * 0.1", async (page) => {
  await press(page, "1", "0", "0", "plus", "1", "0", "percent", "equals");
  assertEqual(await screenText(page), "110");
});
