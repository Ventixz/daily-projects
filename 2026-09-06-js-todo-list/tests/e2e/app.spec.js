import { test, assert, assertEqual } from "./harness.js";

// Every test lands on ?manual=1 (see harness.js), so it has to start the app
// itself. Kept separate from page load so the migration test can seed
// IndexedDB in between the two.
async function start(page) {
  await page.evaluate(() => window.__initApp());
  await page.waitForSelector("#add-form");
}

async function addTodo(page, text) {
  await page.fill("#add-input", text);
  await page.click("#add-form button[type=submit]");
  await page.waitForSelector(`.todo-text:text("${text}")`);
}

// Mutations render synchronously but persist to IndexedDB asynchronously
// (app.js's pendingSync) -- anything that reloads the page to check
// persistence has to wait for that write first, or it's racing its own app.
async function waitForSync(page) {
  await page.evaluate(() => window.__waitForSync());
}

test("adding a todo shows it in the list", async (page) => {
  await start(page);
  await addTodo(page, "buy milk");
  const texts = await page.locator(".todo-text").allTextContents();
  assertEqual(texts, ["buy milk"]);
});

test("toggling a todo marks it done and survives a reload", async (page) => {
  await start(page);
  await addTodo(page, "walk the dog");
  await page.click(".todo-item input[type=checkbox]");
  await page.waitForSelector(".todo-item.done");
  await waitForSync(page);

  await page.reload();
  await start(page);
  const checked = await page.isChecked(".todo-item input[type=checkbox]");
  assert(checked, "checkbox should still be checked after reload");
  assert((await page.locator(".todo-item.done").count()) === 1);
});

test("removing a todo removes it and survives a reload", async (page) => {
  await start(page);
  await addTodo(page, "temporary");
  await page.click(".todo-remove");
  await page.waitForFunction(() => document.querySelectorAll(".todo-item").length === 0);
  await waitForSync(page);

  await page.reload();
  await start(page);
  assertEqual(await page.locator(".todo-item").count(), 0);
});

test("double-click edits a todo's text and survives a reload", async (page) => {
  await start(page);
  await addTodo(page, "old text");
  await page.dblclick(".todo-text");
  await page.fill(".todo-edit", "new text");
  await page.keyboard.press("Enter");
  await page.waitForSelector('.todo-text:text("new text")');
  await waitForSync(page);

  await page.reload();
  await start(page);
  assertEqual(await page.locator(".todo-text").textContent(), "new text");
});

test("drag-and-drop reorders todos and the order survives a reload", async (page) => {
  await start(page);
  await addTodo(page, "one");
  await addTodo(page, "two");
  await addTodo(page, "three");
  assertEqual(await page.locator(".todo-text").allTextContents(), ["one", "two", "three"]);

  // Drop near the top edge of "one" so the drop handler's before/after
  // midpoint check unambiguously reads "before one", not "wherever the
  // browser happens to center the synthetic pointer".
  await page.dragAndDrop('.todo-item:has-text("three")', '.todo-item:has-text("one")', {
    targetPosition: { x: 10, y: 2 },
  });
  await page.waitForFunction(
    () => document.querySelectorAll(".todo-text")[0]?.textContent === "three",
  );
  assertEqual(await page.locator(".todo-text").allTextContents(), ["three", "one", "two"]);
  await waitForSync(page);

  await page.reload();
  await start(page);
  assertEqual(await page.locator(".todo-text").allTextContents(), ["three", "one", "two"]);
});

test("Ctrl+Z / Ctrl+Shift+Z undo and redo a removal", async (page) => {
  await start(page);
  await addTodo(page, "keep");
  await addTodo(page, "doomed");
  await page.click('.todo-item:has-text("doomed") .todo-remove');
  await page.waitForFunction(() => document.querySelectorAll(".todo-item").length === 1);

  await page.keyboard.press("Control+z");
  await page.waitForFunction(() => document.querySelectorAll(".todo-item").length === 2);
  assertEqual(await page.locator(".todo-text").allTextContents(), ["keep", "doomed"]);

  await page.keyboard.press("Control+Shift+z");
  await page.waitForFunction(() => document.querySelectorAll(".todo-item").length === 1);
  assertEqual(await page.locator(".todo-text").allTextContents(), ["keep"]);
});

test("filter buttons show only active or only completed todos", async (page) => {
  await start(page);
  await addTodo(page, "active one");
  await addTodo(page, "done one");
  await page.click('.todo-item:has-text("done one") input[type=checkbox]');
  await page.waitForSelector(".todo-item.done");

  await page.click('[data-filter="active"]');
  assertEqual(await page.locator(".todo-text").allTextContents(), ["active one"]);

  await page.click('[data-filter="completed"]');
  assertEqual(await page.locator(".todo-text").allTextContents(), ["done one"]);

  await page.click('[data-filter="all"]');
  assertEqual(await page.locator(".todo-text").allTextContents(), ["active one", "done one"]);
});

test("legacy v1 records (no order field) are migrated to v2 on load", async (page) => {
  // Page is already on ?manual=1 (harness.js) and hasn't called __initApp
  // yet, so IndexedDB hasn't been opened by the app at all -- seed the v1
  // shape first, *then* let the app open it and trigger the v1->v2 upgrade.
  await page.evaluate(
    () =>
      new Promise((resolve, reject) => {
        const req = indexedDB.open("todo-app", 1);
        req.onupgradeneeded = () => {
          req.result.createObjectStore("todos", { keyPath: "id" });
        };
        req.onsuccess = () => {
          const db = req.result;
          const tx = db.transaction("todos", "readwrite");
          const store = tx.objectStore("todos");
          store.put({ id: "b", text: "second (created later)", done: true, createdAt: 200 });
          store.put({ id: "a", text: "first (created earlier)", done: false, createdAt: 100 });
          tx.oncomplete = () => {
            db.close();
            resolve();
          };
          tx.onerror = () => reject(tx.error);
        };
        req.onerror = () => reject(req.error);
      }),
  );

  await start(page);
  await page.waitForSelector(".todo-item");

  assertEqual(await page.locator(".todo-text").allTextContents(), [
    "first (created earlier)",
    "second (created later)",
  ]);

  const records = await page.evaluate(
    () =>
      new Promise((resolve, reject) => {
        const req = indexedDB.open("todo-app");
        req.onsuccess = () => {
          const db = req.result;
          const getAllReq = db.transaction("todos", "readonly").objectStore("todos").getAll();
          getAllReq.onsuccess = () => resolve(getAllReq.result);
          getAllReq.onerror = () => reject(getAllReq.error);
        };
        req.onerror = () => reject(req.error);
      }),
  );
  assert(
    records.every((r) => typeof r.order === "string" && r.order.length > 0),
    "every migrated record should have a non-empty order key",
  );
});
