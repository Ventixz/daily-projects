import { Row, ROW_SIZE, COLUMN_USERNAME_SIZE, COLUMN_EMAIL_SIZE } from "./row";
import { Table, TABLE_MAX_ROWS } from "./table";

export class PrepareError extends Error {}

export type Statement =
  | { type: "insert"; row: Row }
  | { type: "select" };

// Mirrors prepare_statement's hand-rolled tokenizer: no SQL grammar, just a
// keyword dispatch and (for insert) a fixed positional arg count.
export function prepareStatement(input: string): Statement {
  const trimmed = input.trim();

  if (trimmed.toLowerCase().startsWith("insert")) {
    const parts = trimmed.split(/\s+/);
    // "insert", id, username, email
    if (parts.length !== 4) {
      throw new PrepareError("Syntax error. Could not parse statement.");
    }
    const [, idStr, username, email] = parts;
    if (!/^-?\d+$/.test(idStr)) {
      throw new PrepareError("Syntax error. Could not parse statement.");
    }
    const id = Number(idStr);
    if (id < 0) {
      throw new PrepareError("ID must be positive.");
    }
    if (Buffer.byteLength(username, "utf8") > COLUMN_USERNAME_SIZE) {
      throw new PrepareError("String is too long.");
    }
    if (Buffer.byteLength(email, "utf8") > COLUMN_EMAIL_SIZE) {
      throw new PrepareError("String is too long.");
    }
    return { type: "insert", row: { id, username, email } };
  }

  if (trimmed.toLowerCase() === "select") {
    return { type: "select" };
  }

  throw new PrepareError(`Unrecognized keyword at start of '${trimmed}'.`);
}

export function executeStatement(statement: Statement, table: Table): string {
  if (statement.type === "insert") {
    if (table.numRows >= TABLE_MAX_ROWS) {
      return "Error: Table full.";
    }
    table.insert(statement.row);
    return "Executed.";
  }

  const rows = table.selectAll();
  const lines = rows.map((r) => `(${r.id}, ${r.username}, ${r.email})`);
  lines.push("Executed.");
  return lines.join("\n");
}

export const CONSTANTS_REPORT = [
  `ROW_SIZE: ${ROW_SIZE}`,
  `COLUMN_USERNAME_SIZE: ${COLUMN_USERNAME_SIZE}`,
  `COLUMN_EMAIL_SIZE: ${COLUMN_EMAIL_SIZE}`,
  `TABLE_MAX_ROWS: ${TABLE_MAX_ROWS}`,
].join("\n");
