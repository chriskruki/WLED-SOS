#include "ndef_url.h"

#include <strings.h>

static const char SITE_HOST[] = "sacredimagination.co";
static constexpr size_t SITE_HOST_LEN = sizeof(SITE_HOST) - 1;

static bool startsWithCI(const uint8_t* s, size_t n, const char* lit, size_t litLen) {
  return n >= litLen && strncasecmp((const char*)s, lit, litLen) == 0;
}
#define STARTS_WITH(s, n, lit) startsWithCI((s), (n), (lit), sizeof(lit) - 1)

// Walk the tag's TLV blocks to the body of the NDEF message (TLV tag 0x03).
static bool findNdefMessage(const uint8_t* buf, size_t bufLen,
                            const uint8_t** msg, size_t* msgLen) {
  size_t i = 0;
  while (i < bufLen) {
    const uint8_t tag = buf[i];
    if (tag == 0xFE) return false;             // terminator before any NDEF
    if (tag == 0x00) { i++; continue; }        // null padding
    if (i + 1 >= bufLen) return false;

    size_t len, headerLen;
    if (buf[i + 1] != 0xFF) {
      len = buf[i + 1];
      headerLen = 2;
    } else {
      if (i + 3 >= bufLen) return false;
      len = ((size_t)buf[i + 2] << 8) | buf[i + 3];
      headerLen = 4;
    }
    if (i + headerLen + len > bufLen) return false;   // truncated: caller reads more

    if (tag == 0x03) { *msg = buf + i + headerLen; *msgLen = len; return true; }
    i += headerLen + len;                      // 0x01 lock-control, 0x02 mem-control, ...
  }
  return false;
}

// Payload of the first well-known URI record ("U", TNF 1) in the message.
static bool findUriPayload(const uint8_t* msg, size_t msgLen,
                           const uint8_t** payload, size_t* payloadLen) {
  size_t i = 0;
  while (i + 3 <= msgLen) {
    const uint8_t header = msg[i++];
    if (header & 0x20) return false;           // chunked records unsupported
    const uint8_t typeLen = msg[i++];

    size_t plen;
    if (header & 0x10) {                       // SR: single-byte payload length
      plen = msg[i++];
    } else {
      if (i + 4 > msgLen) return false;
      plen = ((size_t)msg[i] << 24) | ((size_t)msg[i+1] << 16)
           | ((size_t)msg[i+2] << 8) | (size_t)msg[i+3];
      i += 4;
    }

    uint8_t idLen = 0;
    if (header & 0x08) {                       // IL: an ID field follows the type
      if (i + 1 > msgLen) return false;
      idLen = msg[i++];
    }

    // subtraction rather than addition: plen is attacker-controlled and would overflow
    const size_t remaining = msgLen - i;
    if (typeLen > remaining) return false;
    if (idLen > remaining - typeLen) return false;
    if (plen > remaining - typeLen - idLen) return false;

    const bool isUri = (header & 0x07) == 0x01 && typeLen == 1 && msg[i] == 'U';
    i += typeLen + idLen;
    if (isUri) { *payload = msg + i; *payloadLen = plen; return true; }
    i += plen;
  }
  return false;
}

// URI record payload -> colour. Matches our host whatever the scheme, "www.", sub-path or
// extra query parameters; ?c= out of range or absent falls back to 1.
static bool matchSiteUrl(const uint8_t* payload, size_t len, uint8_t& colorOut) {
  if (len < 2) return false;

  const uint8_t* p = payload + 1;
  size_t n = len - 1;

  switch (payload[0]) {                        // URI identifier code, RTD_URI 1.0 3.2.2
    case 0x00:                                 // no abbreviation, full URI inline
      if      (STARTS_WITH(p, n, "https://")) { p += 8; n -= 8; }
      else if (STARTS_WITH(p, n, "http://"))  { p += 7; n -= 7; }
      else return false;
      break;
    case 0x01: case 0x02:                      // http(s)://www. -- prefix ate the "www."
    case 0x03: case 0x04:                      // http(s)://
      break;
    default: return false;                     // tel:, mailto:, ftp: -- never us
  }

  if (STARTS_WITH(p, n, "www.")) { p += 4; n -= 4; }

  if (!startsWithCI(p, n, SITE_HOST, SITE_HOST_LEN)) return false;
  p += SITE_HOST_LEN; n -= SITE_HOST_LEN;
  if (n && *p != '/' && *p != '?' && *p != '#') return false;  // not "sacredimagination.company"

  colorOut = 1;
  for (size_t i = 0; i + 2 < n; i++) {
    if ((p[i] != '?' && p[i] != '&') || (p[i+1] | 32) != 'c' || p[i+2] != '=') continue;
    uint16_t v = 0;
    for (size_t j = i + 3; j < n && p[j] >= '0' && p[j] <= '9' && v < 100; j++) v = v * 10 + (p[j] - '0');
    if (v >= 1 && v <= NFC_COLORS) colorOut = (uint8_t)v;
    break;
  }
  return true;
}

bool decodeTagUrl(const uint8_t* tag, size_t len, uint8_t& colorOut) {
  const uint8_t* msg;
  size_t msgLen;
  if (!findNdefMessage(tag, len, &msg, &msgLen)) return false;

  const uint8_t* payload;
  size_t payloadLen;
  if (!findUriPayload(msg, msgLen, &payload, &payloadLen)) return false;

  return matchSiteUrl(payload, payloadLen, colorOut);
}
