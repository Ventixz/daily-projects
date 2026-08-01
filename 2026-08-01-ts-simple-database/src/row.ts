export const COLUMN_USERNAME_SIZE = 32;
export const COLUMN_EMAIL_SIZE = 255;

const ID_SIZE = 4; // uint32
export const ROW_SIZE = ID_SIZE + COLUMN_USERNAME_SIZE + COLUMN_EMAIL_SIZE;

const ID_OFFSET = 0;
const USERNAME_OFFSET = ID_OFFSET + ID_SIZE;
const EMAIL_OFFSET = USERNAME_OFFSET + COLUMN_USERNAME_SIZE;

export interface Row {
  id: number;
  username: string;
  email: string;
}

export class RowError extends Error {}

export function validateRow(row: Row): void {
  if (!Number.isInteger(row.id) || row.id < 0) {
    throw new RowError("ID must be positive.");
  }
  if (Buffer.byteLength(row.username, "utf8") > COLUMN_USERNAME_SIZE) {
    throw new RowError("String is too long.");
  }
  if (Buffer.byteLength(row.email, "utf8") > COLUMN_EMAIL_SIZE) {
    throw new RowError("String is too long.");
  }
}

// Packs a Row into a fixed-width slice of `page` at `offset`, matching the
// tutorial's serialize_row: a raw memcpy into a struct-shaped byte range,
// not a length-prefixed or delimited encoding.
export function serializeRow(row: Row, page: Buffer, offset: number): void {
  page.writeUInt32LE(row.id, offset + ID_OFFSET);

  page.fill(0, offset + USERNAME_OFFSET, offset + USERNAME_OFFSET + COLUMN_USERNAME_SIZE);
  page.write(row.username, offset + USERNAME_OFFSET, COLUMN_USERNAME_SIZE, "utf8");

  page.fill(0, offset + EMAIL_OFFSET, offset + EMAIL_OFFSET + COLUMN_EMAIL_SIZE);
  page.write(row.email, offset + EMAIL_OFFSET, COLUMN_EMAIL_SIZE, "utf8");
}

function readCString(page: Buffer, start: number, maxLen: number): string {
  let end = page.indexOf(0, start);
  if (end === -1 || end > start + maxLen) {
    end = start + maxLen;
  }
  return page.toString("utf8", start, end);
}

export function deserializeRow(page: Buffer, offset: number): Row {
  const id = page.readUInt32LE(offset + ID_OFFSET);
  const username = readCString(page, offset + USERNAME_OFFSET, COLUMN_USERNAME_SIZE);
  const email = readCString(page, offset + EMAIL_OFFSET, COLUMN_EMAIL_SIZE);
  return { id, username, email };
}
