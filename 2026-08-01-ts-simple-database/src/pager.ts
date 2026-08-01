import * as fs from "node:fs";

export const PAGE_SIZE = 4096;
export const TABLE_MAX_PAGES = 100;

export class PagerError extends Error {}

/**
 * Owns the single open file descriptor and a page-sized in-memory cache.
 * Nothing above this layer is allowed to know the file exists — Table asks
 * for a page number and gets a Buffer back, cached or freshly read.
 */
export class Pager {
  private fd: number;
  private fileLength: number;
  private pages: (Buffer | null)[] = new Array(TABLE_MAX_PAGES).fill(null);

  private constructor(fd: number, fileLength: number) {
    this.fd = fd;
    this.fileLength = fileLength;
  }

  static open(filename: string): Pager {
    const fd = fs.openSync(filename, "a+");
    const fileLength = fs.fstatSync(fd).size;
    return new Pager(fd, fileLength);
  }

  get lengthInRows(): number {
    return this.fileLength;
  }

  getPage(pageNum: number): Buffer {
    if (pageNum >= TABLE_MAX_PAGES) {
      throw new PagerError(
        `Tried to fetch page number out of bounds. ${pageNum} >= ${TABLE_MAX_PAGES}`
      );
    }

    let page = this.pages[pageNum];
    if (page === null) {
      page = Buffer.alloc(PAGE_SIZE);
      // The last page on disk is usually partial (only as many rows as were
      // flushed), so read whatever bytes actually exist there instead of
      // requiring a full aligned PAGE_SIZE chunk.
      const bytesAvailable = Math.max(0, this.fileLength - pageNum * PAGE_SIZE);
      if (bytesAvailable > 0) {
        fs.readSync(this.fd, page, 0, Math.min(PAGE_SIZE, bytesAvailable), pageNum * PAGE_SIZE);
      }
      this.pages[pageNum] = page;
    }
    return page;
  }

  // Writes exactly `size` bytes of a page back to its slot in the file —
  // full pages flush whole, the last (possibly partial) page flushes only
  // the bytes actually holding rows, mirroring the C tutorial's pager_flush.
  flushPage(pageNum: number, size: number): void {
    const page = this.pages[pageNum];
    if (page === null) {
      throw new PagerError(`Tried to flush null page ${pageNum}.`);
    }
    fs.writeSync(this.fd, page, 0, size, pageNum * PAGE_SIZE);
  }

  close(): void {
    fs.closeSync(this.fd);
  }
}
