import { test, assert, assertEqual } from "./harness.js";
import { migrateV1ToV2 } from "../../src/storage/migrations.js";

test("migrateV1ToV2 on an empty store returns an empty array", () => {
  assertEqual(migrateV1ToV2([]), []);
});

test("migrateV1ToV2 backfills order from createdAt, oldest first", () => {
  const v1Records = [
    { id: "c", text: "third", done: false, createdAt: 300 },
    { id: "a", text: "first", done: false, createdAt: 100 },
    { id: "b", text: "second", done: true, createdAt: 200 },
  ];

  const migrated = migrateV1ToV2(v1Records);

  assertEqual(
    migrated.map((r) => r.id),
    ["a", "b", "c"],
    "migrated records should come back sorted oldest-created first",
  );
  for (let i = 1; i < migrated.length; i++) {
    assert(
      migrated[i].order > migrated[i - 1].order,
      `order keys should be strictly increasing: ${migrated[i - 1].order} then ${migrated[i].order}`,
    );
  }
});

test("migrateV1ToV2 preserves every other field unchanged", () => {
  const v1Records = [{ id: "a", text: "keep me", done: true, createdAt: 42 }];
  const [migrated] = migrateV1ToV2(v1Records);
  assertEqual(migrated.id, "a");
  assertEqual(migrated.text, "keep me");
  assertEqual(migrated.done, true);
  assertEqual(migrated.createdAt, 42);
  assert(typeof migrated.order === "string" && migrated.order.length > 0);
});
