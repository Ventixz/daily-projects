#ifndef RECORD_H
#define RECORD_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* On-disk record format shared by the WAL and SSTables:
 *   [1 byte op: 'P' or 'D'][4 byte keylen][key][4 byte vallen][val]
 * 'D' (delete/tombstone) records omit the vallen/val fields entirely. */

#define REC_PUT 'P'
#define REC_DEL 'D'

int record_write(FILE *fp, char op, const char *key, const char *val, size_t vlen);

/* Reads one record at the current file position. On success returns 1 and
 * sets op/key/val/vlen (val is NULL for REC_DEL; caller frees key and val).
 * Returns 0 cleanly at EOF, -1 on a truncated/corrupt record. */
int record_read(FILE *fp, char *op, char **key, char **val, size_t *vlen);

#endif
