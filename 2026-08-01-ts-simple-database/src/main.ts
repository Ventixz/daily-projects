import * as readline from "node:readline";
import { Table } from "./table";
import { prepareStatement, executeStatement, PrepareError, CONSTANTS_REPORT } from "./statement";

function main(): void {
  const filename = process.argv[2];
  if (!filename) {
    console.error("Must supply a database filename.");
    process.exit(1);
  }

  const table = Table.open(filename);
  const rl = readline.createInterface({ input: process.stdin, output: process.stdout, prompt: "db > " });

  rl.prompt();
  rl.on("line", (line) => {
    const input = line.trim();

    if (input.startsWith(".")) {
      switch (input) {
        case ".exit":
          table.close();
          rl.close();
          return;
        case ".constants":
          console.log(CONSTANTS_REPORT);
          break;
        default:
          console.log(`Unrecognized command '${input}'.`);
      }
      rl.prompt();
      return;
    }

    try {
      const statement = prepareStatement(input);
      console.log(executeStatement(statement, table));
    } catch (err) {
      if (err instanceof PrepareError) {
        console.log(err.message);
      } else {
        throw err;
      }
    }
    rl.prompt();
  });

  rl.on("close", () => {
    process.exit(0);
  });
}

main();
