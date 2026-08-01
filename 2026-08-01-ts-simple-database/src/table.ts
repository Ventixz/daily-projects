import { Pager, PAGE_SIZE, TABLE_MAX_PAGES } from "./pager";
import { Row, ROW_SIZE, validateRow, serializeRow, deserializeRow } from "./row";

export const ROWS_PER_PAGE = Math.floor(PAGE_SIZE / ROW_SIZE);
export const TABLE_MAX_ROWS = ROWS_PER_PAGE * TABLE_MAX_PAGES;

export class TableError extends Error {}

interface Slot {
  page: Buffer;
  offset: number;
}

/**
 * The only thing above the Pager: turns a row index into a (page, offset)
 * slot, and knows when the table is full. No B-tree yet — rows are appended
 * in insertion order, so `select` is a linear scan and there's no `WHERE`.
 */
export class Table {
  private pager: Pager;
  numRows: number;

  private constructor(pager: Pager, numRows: number) {
    this.pager = pager;
    this.numRows = numRows;
  }

  static open(filename: string): Table {
    const pager = Pager.open(filename);
    const numRows = Math.floor(pager.lengthInRows / ROW_SIZE);
    return new Table(pager, numRows);
  }

  private rowSlot(rowNum: number): Slot {
    const pageNum = Math.floor(rowNum / ROWS_PER_PAGE);
    const page = this.pager.getPage(pageNum);
    const offset = (rowNum % ROWS_PER_PAGE) * ROW_SIZE;
    return { page, offset };
  }

  insert(row: Row): void {
    validateRow(row);
    if (this.numRows >= TABLE_MAX_ROWS) {
      throw new TableError("Table full.");
    }
    const { page, offset } = this.rowSlot(this.numRows);
    serializeRow(row, page, offset);
    this.numRows++;
  }

  selectAll(): Row[] {
    const rows: Row[] = [];
    for (let i = 0; i < this.numRows; i++) {
      const { page, offset } = this.rowSlot(i);
      rows.push(deserializeRow(page, offset));
    }
    return rows;
  }

  close(): void {
    const numFullPages = Math.floor(this.numRows / ROWS_PER_PAGE);
    for (let i = 0; i < numFullPages; i++) {
      this.rowSlot(i * ROWS_PER_PAGE); // ensure page is loaded before flushing
      this.pager.flushPage(i, PAGE_SIZE);
    }

    const remainingRows = this.numRows % ROWS_PER_PAGE;
    if (remainingRows > 0) {
      this.rowSlot(numFullPages * ROWS_PER_PAGE);
      this.pager.flushPage(numFullPages, remainingRows * ROW_SIZE);
    }

    this.pager.close();
  }
}
