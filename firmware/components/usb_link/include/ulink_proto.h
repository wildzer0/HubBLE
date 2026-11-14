/**
 * @file ulink_proto.h
 * @brief Wire protocol of the USB configuration link (pure C, host-testable).
 *
 * The web app talks to the dongle over a vendor-defined HID interface with two 64-byte reports (IN and OUT) and no
 * report IDs. On top of those reports there is a plain byte stream, and on top of the stream a sequence of frames. Both
 * directions use the same layers, so the same code is mirrored in `webapp/src/api/usbLink.ts` (same test vectors).
 *
 *  - Report: byte 0 = number of valid bytes n (0..63), bytes 1..n = stream bytes, the rest zero padding.
 *  - Frame:  [length low][length high][type][payload of `length` bytes]. `length` <= ULINK_MAX_PAYLOAD.
 *
 * Frame types (host = the browser):
 *  - 0x51 HELLO (host->dev) `{"proto":1}`, answered by 0x52 INFO (dev->host)
 * `{"proto":1,"fw":...,"board":...,"open":bool}`.
 *  - 0x01 REQ_HEAD (host->dev) `{"id":n,"method":"GET","path":"/api/v1/...","ct":"...","len":bytes}`, then 0x02
 * REQ_BODY chunks. The device answers REQ_HEAD (when len > 0) and every REQ_BODY with 0x41 ACK: the host sends the next
 * chunk only after the ACK (a window of one), so the device never needs a deep buffer.
 *  - 0x11 RES_HEAD (dev->host) `{"id":n,"status":code,"ct":"..."}`, 0x12 RES_BODY chunks, 0x13 RES_END.
 *  - 0x21 EVENT (dev->host) `{"event":"...","data":...}`, the same JSON as the WebSocket events.
 *  - 0x31 LIVE (host->dev) the same JSON text the web app sends over the WebSocket (`hello`, `input.subscribe`...).
 *  - 0x7F ERROR (dev->host) `{"code":"...","message":"..."}`.
 * Dependencies: libc only. Threading: no locking; one parser per direction.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Size of one HID report of the link. */
#define ULINK_REPORT_SIZE 64
/** Stream bytes carried by one report. */
#define ULINK_REPORT_DATA (ULINK_REPORT_SIZE - 1)
/** Largest frame payload. A body chunk is at most 1024 bytes; heads are short JSON. */
#define ULINK_MAX_PAYLOAD 1100
/** Protocol version. */
#define ULINK_PROTO 1

/** Frame types. */
typedef enum {
    ULINK_REQ_HEAD = 0x01, /**< Host -> dongle: method, path and headers of a request. */
    ULINK_REQ_BODY = 0x02, /**< Host -> dongle: a chunk of the request body. */
    ULINK_RES_HEAD = 0x11, /**< Dongle -> host: status and headers of the response. */
    ULINK_RES_BODY = 0x12, /**< Dongle -> host: a chunk of the response body. */
    ULINK_RES_END = 0x13,  /**< Dongle -> host: end of the response. */
    ULINK_EVENT = 0x21,    /**< Dongle -> host: a WebSocket-equivalent event. */
    ULINK_LIVE = 0x31,     /**< Host -> dongle: keep-alive holding the configuration window open. */
    ULINK_ACK = 0x41,      /**< Flow-control acknowledgement. */
    ULINK_HELLO = 0x51,    /**< Host -> dongle: handshake request. */
    ULINK_INFO = 0x52,     /**< Dongle -> host: handshake answer (protocol, firmware, board). */
    ULINK_ERROR = 0x7F,    /**< Either direction: protocol error, the link resets. */
} ulink_type_t;

/** Callback receiving a complete frame. */
typedef void (*ulink_frame_cb)(uint8_t type, const uint8_t *payload, size_t len, void *ctx);

/** Incremental frame parser. */
typedef struct {
    uint8_t buf[3 + ULINK_MAX_PAYLOAD]; /**< Bytes of the frame being assembled (header included). */
    size_t have;                        /**< Bytes in @c buf. */
    uint32_t errors;                    /**< Oversized frames discarded (the stream was resynchronised). */
} ulink_parser_t;

/**
 * @brief Reset a parser.
 *
 * @param p Parser.
 */
void ulink_parser_init(ulink_parser_t *p);

/**
 * @brief Feed the valid bytes of one HID report (report byte 0 is the count) to the parser.
 *
 * @param p      Parser.
 * @param report A report of ULINK_REPORT_SIZE bytes.
 * @param len    Bytes available in @p report (at least 1).
 * @param cb     Called once per completed frame.
 * @param ctx    Passed to @p cb.
 */
void ulink_parser_feed_report(ulink_parser_t *p, const uint8_t *report, size_t len, ulink_frame_cb cb, void *ctx);

/**
 * @brief Feed raw stream bytes to the parser.
 *
 * A frame announcing more than ULINK_MAX_PAYLOAD bytes is a protocol violation: the parser drops its state and counts
 * an error, so a corrupted stream cannot wedge it for ever.
 *
 * @param p    Parser.
 * @param data Stream bytes.
 * @param n    Their count.
 * @param cb   Called once per completed frame.
 * @param ctx  Passed to @p cb.
 */
void ulink_parser_feed(ulink_parser_t *p, const uint8_t *data, size_t n, ulink_frame_cb cb, void *ctx);

/**
 * @brief Encode a frame header.
 *
 * @param type Frame type.
 * @param len  Payload length (<= ULINK_MAX_PAYLOAD).
 * @param out  Receives 3 bytes.
 */
void ulink_encode_header(uint8_t type, size_t len, uint8_t out[3]);

/**
 * @brief Pack stream bytes into one report.
 *
 * @param out  Receives ULINK_REPORT_SIZE bytes.
 * @param data Stream bytes.
 * @param n    Count, at most ULINK_REPORT_DATA.
 */
void ulink_pack_report(uint8_t out[ULINK_REPORT_SIZE], const uint8_t *data, size_t n);

/* ---- HTTP helpers for the loopback bridge ---------------------------------------------------------- */

/** What the head of an HTTP response says. */
typedef struct {
    int status;            /**< Status code. */
    char content_type[64]; /**< Content-Type value ("" if absent). */
    long content_length;   /**< Content-Length, or -1 if absent. */
    bool chunked;          /**< Transfer-Encoding: chunked. */
    size_t head_len;       /**< Bytes of the head including the blank line. */
} ulink_http_head_t;

/**
 * @brief Parse the head of an HTTP response.
 *
 * @param data Received bytes (a prefix of the response).
 * @param n    Their count.
 * @param[out] out Receives the parsed head.
 * @return 1 if complete, 0 if more bytes are needed, -1 if it is not an HTTP response.
 */
int ulink_http_parse_head(const uint8_t *data, size_t n, ulink_http_head_t *out);

/** Incremental decoder of a chunked HTTP body. */
typedef struct {
    uint8_t state;       /**< Internal state. */
    size_t remaining;    /**< Bytes left in the current chunk. */
    uint8_t size_digits; /**< Hex digits of the chunk size read so far (bounds the line). */
} ulink_chunked_t;

/**
 * @brief Reset a chunked decoder.
 *
 * @param c Decoder.
 */
void ulink_chunked_init(ulink_chunked_t *c);

/**
 * @brief Decode chunked body bytes.
 *
 * @param c    Decoder.
 * @param in   Encoded bytes.
 * @param n    Their count.
 * @param out  Receives the decoded bytes (capacity at least @p n).
 * @param[out] done Set to true once the terminating chunk has been seen.
 * @return Number of decoded bytes written to @p out, or -1 on a malformed stream.
 */
long ulink_chunked_feed(ulink_chunked_t *c, const uint8_t *in, size_t n, uint8_t *out, bool *done);

#ifdef __cplusplus
}
#endif
