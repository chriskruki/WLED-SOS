// Native test for the URL catalog: real NDEF tag bytes -> catalog -> "c" field.
// Build & run:  make -C usermods/nfc_preset/test
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "ndef_url.h"

static int failures = 0;

// Build the page bytes an NTAG holds for a single URI record: NDEF TLV + SR record.
static std::vector<uint8_t> tagBytes(uint8_t prefixCode, const std::string& rest,
                                     uint8_t recHeader = 0xD1) {
    std::vector<uint8_t> payload;
    payload.push_back(prefixCode);
    payload.insert(payload.end(), rest.begin(), rest.end());

    std::vector<uint8_t> rec;
    rec.push_back(recHeader);                   // MB|ME|SR, TNF 1 (well known)
    rec.push_back(0x01);                        // type length
    rec.push_back((uint8_t)payload.size());     // payload length
    rec.push_back('U');                         // type
    rec.insert(rec.end(), payload.begin(), payload.end());

    std::vector<uint8_t> pages;
    pages.push_back(0x03);                      // NDEF message TLV
    pages.push_back((uint8_t)rec.size());
    pages.insert(pages.end(), rec.begin(), rec.end());
    pages.push_back(0xFE);                      // terminator
    pages.resize(64, 0x00);                     // pad to the read cap
    return pages;
}

static void expect(const char* label, uint8_t prefixCode, const std::string& rest,
                   bool wantDecoded, uint8_t wantC) {
    auto pages = tagBytes(prefixCode, rest);
    uint8_t c = 0;
    bool decoded = decodeTagUrl(pages.data(), pages.size(), c);
    if (!decoded) c = 0;

    if (decoded != wantDecoded || (wantDecoded && c != wantC)) {
        failures++;
        printf("FAIL %-42s decoded=%d (want %d) c=%u (want %u)\n",
               label, decoded, wantDecoded, c, wantC);
    } else {
        printf("ok   %-42s decoded=%d c=%u\n", label, decoded, c);
    }
}

int main() {
    // 0x04 == "https://", 0x02 == "https://www.", 0x03 == "http://", 0x00 == no prefix
    expect("bare host",              0x04, "sacredimagination.co",                 true, 1);
    expect("trailing slash",         0x04, "sacredimagination.co/",                true, 1);
    expect("c=3",                    0x04, "sacredimagination.co/?c=3",            true, 3);
    expect("c=5 upper C",            0x04, "sacredimagination.co/?C=5",            true, 5);
    expect("www prefix code",        0x02, "sacredimagination.co/?c=2",            true, 2);
    expect("literal www",            0x04, "www.sacredimagination.co/?c=4",        true, 4);
    expect("http",                   0x03, "sacredimagination.co/shop",            true, 1);
    expect("sub-route",              0x04, "sacredimagination.co/a/b/c",           true, 1);
    expect("sub-route + c",          0x04, "sacredimagination.co/collections/x?c=2", true, 2);
    expect("c not first param",      0x04, "sacredimagination.co/?utm=x&c=5",      true, 5);
    expect("c before other params",  0x04, "sacredimagination.co/?c=2&utm=x",      true, 2);
    expect("mixed case host",        0x04, "SacredImagination.CO/?c=3",            true, 3);
    expect("no prefix code",         0x00, "https://sacredimagination.co/?c=4",    true, 4);
    expect("fragment",               0x04, "sacredimagination.co/#top",            true, 1);

    expect("c out of range -> default", 0x04, "sacredimagination.co/?c=9",         true, 1);
    expect("c=0 -> default",         0x04, "sacredimagination.co/?c=0",            true, 1);
    expect("c empty -> default",     0x04, "sacredimagination.co/?c=",             true, 1);
    expect("cid= is not c=",         0x04, "sacredimagination.co/?cid=3",          true, 1);

    expect("other host",             0x04, "example.com/?c=3",                     false, 0);
    expect("suffix host",            0x04, "sacredimagination.company/?c=3",       false, 0);
    expect("host as sub-path",       0x04, "evil.com/sacredimagination.co",        false, 0);
    expect("tel: prefix",            0x05, "sacredimagination.co",                 false, 0);
    expect("no prefix, no scheme",   0x00, "sacredimagination.co",                 false, 0);

    // TNF must be 1 (well known); a MIME record that happens to be typed "U" is not a URI
    {
        auto pages = tagBytes(0x04, "sacredimagination.co/?c=3", 0xD2);  // TNF 2 == MIME
        uint8_t c = 0;
        bool decoded = decodeTagUrl(pages.data(), pages.size(), c);
        if (decoded) { failures++; printf("FAIL %-42s decoded a TNF 2 record\n", "wrong TNF rejected"); }
        else printf("ok   %-42s decoded=0\n", "wrong TNF rejected");
    }

    // The reader grows the buffer page by page and retries until this returns true, so every
    // truncated prefix must fail and the first complete one must succeed.
    {
        auto pages = tagBytes(0x04, "sacredimagination.co/?c=4");
        const size_t complete = 2 + 4 + 1 + 25;   // TLV header + record header + payload
        bool ok = true;
        for (size_t n = 4; n < complete; n += 4) {
            uint8_t c = 0;
            if (decodeTagUrl(pages.data(), n, c)) { ok = false; printf("FAIL truncated at %zu decoded\n", n); }
        }
        uint8_t c = 0;
        size_t rounded = ((complete + 3) / 4) * 4;
        if (!decodeTagUrl(pages.data(), rounded, c) || c != 4) {
            ok = false;
            printf("FAIL %-42s at %zu bytes c=%u\n", "first complete page-aligned read", rounded, c);
        }
        if (!ok) failures++;
        else printf("ok   %-42s fails below %zu, succeeds at %zu\n", "incremental read", complete, rounded);
    }

    printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
