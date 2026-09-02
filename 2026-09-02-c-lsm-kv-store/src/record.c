#include <stdlib.h>
#include <string.h>

#include "record.h"

int record_write(FILE *fp, char op, const char *key, const char *val, size_t vlen) {
    uint32_t klen = (uint32_t)strlen(key);

    if (fwrite(&op, 1, 1, fp) != 1)
        return -1;
    if (fwrite(&klen, sizeof(klen), 1, fp) != 1)
        return -1;
    if (klen && fwrite(key, 1, klen, fp) != klen)
        return -1;

    if (op == REC_PUT) {
        uint32_t vl = (uint32_t)vlen;
        if (fwrite(&vl, sizeof(vl), 1, fp) != 1)
            return -1;
        if (vl && fwrite(val, 1, vl, fp) != vl)
            return -1;
    }
    return 0;
}

int record_read(FILE *fp, char *op, char **key, char **val, size_t *vlen) {
    uint8_t op_byte;
    size_t n = fread(&op_byte, 1, 1, fp);
    if (n == 0)
        return feof(fp) ? 0 : -1;

    uint32_t klen;
    if (fread(&klen, sizeof(klen), 1, fp) != 1)
        return -1;

    char *k = malloc(klen + 1);
    if (klen && fread(k, 1, klen, fp) != klen) {
        free(k);
        return -1;
    }
    k[klen] = '\0';

    *op = (char)op_byte;
    *key = k;

    if (op_byte == REC_PUT) {
        uint32_t vl;
        if (fread(&vl, sizeof(vl), 1, fp) != 1) {
            free(k);
            return -1;
        }
        char *v = malloc(vl ? vl : 1);
        if (vl && fread(v, 1, vl, fp) != vl) {
            free(k);
            free(v);
            return -1;
        }
        *val = v;
        *vlen = vl;
    } else {
        *val = NULL;
        *vlen = 0;
    }
    return 1;
}
