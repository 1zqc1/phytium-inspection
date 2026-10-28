#ifndef FTINSPECT_STREAM_DECODER_H
#define FTINSPECT_STREAM_DECODER_H

#include <stddef.h>
#include <stdint.h>

#include "ftinspect/protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FI_STREAM_NEED_MORE = 0,
    FI_STREAM_FRAME_READY,
    FI_STREAM_FRAME_DROPPED
} fi_stream_result_t;

typedef struct {
    uint8_t buffer[FI_MAX_FRAME_SIZE];
    size_t length;
    size_t expected_length;
    uint32_t last_byte_ms;
} fi_stream_decoder_t;

void fi_stream_decoder_init(fi_stream_decoder_t *decoder);
fi_stream_result_t fi_stream_decoder_feed(
    fi_stream_decoder_t *decoder,
    uint8_t byte,
    fi_frame_t *frame,
    fi_status_t *decode_status);
/* A timeout drops the partial candidate while retaining the current byte. */
fi_stream_result_t fi_stream_decoder_feed_timed(
    fi_stream_decoder_t *decoder, uint8_t byte, fi_frame_t *frame,
    fi_status_t *decode_status, uint32_t now_ms, uint32_t interbyte_timeout_ms);

#ifdef __cplusplus
}
#endif

#endif

