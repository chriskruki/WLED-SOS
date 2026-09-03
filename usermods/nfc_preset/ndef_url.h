#pragma once

#include <stdint.h>
#include <stddef.h>

// Decodes NFC tag memory (read from page 4 onwards) holding an NDEF URI record that points
// at the site, and yields the ?c=N colour. Returns false for a truncated read, a different
// host, or anything that isn't a URI record -- callers treat all three the same way.
//
// Pure: no Arduino dependency, so it is tested natively. See test/site_url_test.cpp.

#define NFC_COLORS 5

bool decodeTagUrl(const uint8_t* tag, size_t len, uint8_t& colorOut);
