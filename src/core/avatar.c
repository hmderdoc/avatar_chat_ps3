#include "avatar.h"
#include "base64.h"
#include <string.h>

int avatar_valid(const unsigned char a[AVATAR_BYTES]) {
    int i;
    for (i = 0; i < AVATAR_BYTES; i += 2) {
        switch (a[i]) {
        case 0x00: case 0x07: case 0x08: case 0x09: case 0x0A: case 0x0C: case 0x0D: case 0x1B: case 0xFF:
            return 0;
        default: break;
        }
        if (a[i + 1] & 0x80) return 0; /* blink */
    }
    return 1;
}

int avatar_from_base64(const char *b64, size_t n, unsigned char out[AVATAR_BYTES]) {
    unsigned char buf[1024];
    int got;
    if (n > sizeof buf * 4 / 3 + 4) return 0;
    got = base64_decode(b64, n, buf, sizeof buf);
    if (got < AVATAR_BYTES) return 0; /* senders may pad; take the first 120 */
    memcpy(out, buf, AVATAR_BYTES);
    return avatar_valid(out);
}

int avatar_from_collection(const unsigned char *data, size_t n, int i, unsigned char out[AVATAR_BYTES]) {
    int count;
    /* Strip a SAUCE record (128 bytes starting "SAUCE00") and its optional
     * comment block (5 + 64*N bytes, "COMNT") and the 0x1A marker. */
    if (n >= 128 && memcmp(data + n - 128, "SAUCE00", 7) == 0) {
        unsigned comments = data[n - 128 + 104];
        n -= 128;
        if (comments > 0 && n >= 5 + 64u * comments && memcmp(data + n - (5 + 64u * comments), "COMNT", 5) == 0)
            n -= 5 + 64u * comments;
        if (n > 0 && data[n - 1] == 0x1A) n--;
    }
    count = (int)(n / AVATAR_BYTES);
    if (i >= 0 && i < count) memcpy(out, data + (size_t)i * AVATAR_BYTES, AVATAR_BYTES);
    return count;
}
