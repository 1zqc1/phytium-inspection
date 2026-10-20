#ifndef FTINSPECT_PROTOCOL_H
#define FTINSPECT_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FI_PROTOCOL_VERSION 0x01u
#define FI_MAGIC_0 0xA5u
#define FI_MAGIC_1 0x5Au
#define FI_MAX_FRAME_SIZE 128u
#define FI_FIXED_FIELDS_SIZE 18u
#define FI_CRC_SIZE 2u
#define FI_MIN_FRAME_SIZE (FI_FIXED_FIELDS_SIZE + FI_CRC_SIZE)
#define FI_MAX_PAYLOAD_SIZE (FI_MAX_FRAME_SIZE - FI_MIN_FRAME_SIZE)

typedef enum {
    FI_OK = 0,
    FI_ERR_ARGUMENT,
    FI_ERR_CAPACITY,
    FI_ERR_MAGIC,
    FI_ERR_VERSION,
    FI_ERR_TYPE,
    FI_ERR_LENGTH,
    FI_ERR_CRC
} fi_status_t;

typedef enum {
    FI_MSG_HELLO = 0x01,
    FI_MSG_HEARTBEAT = 0x02,
    FI_MSG_ARRIVED = 0x10,
    FI_MSG_CAPTURE_REQUEST = 0x11,
    FI_MSG_RESULT = 0x12,
    FI_MSG_SET_ROUTE = 0x20,
    FI_MSG_RELEASE = 0x21,
    FI_MSG_COMPLETE = 0x22,
    FI_MSG_ACK = 0x30,
    FI_MSG_FAULT = 0x31
} fi_message_type_t;

typedef struct {
    fi_message_type_t type;
    uint32_t session_id;
    uint32_t job_id;
    uint32_t seq;
    uint16_t payload_length;
    uint8_t payload[FI_MAX_PAYLOAD_SIZE];
} fi_frame_t;

uint16_t fi_crc16_ccitt(const uint8_t *data, size_t length);
int fi_message_type_is_valid(uint8_t type);
fi_status_t fi_frame_encode(
    const fi_frame_t *frame,
    uint8_t *out,
    size_t capacity,
    size_t *written);
fi_status_t fi_frame_decode(
    const uint8_t *bytes,
    size_t length,
    fi_frame_t *out);

#ifdef __cplusplus
}
#endif

#endif

