import "./order.test.js";
import "./todos.test.js";
import "./history.test.js";
import "./migrations.test.js";
import { runAll } from "./harness.js";

await runAll();
