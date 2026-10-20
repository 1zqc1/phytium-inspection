#include "ftinspect/protocol.h"

#include <string.h>

static void write_u16_le(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static void write_u32_le(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)((value >> 8) & 0xFFu);
    out[2] = (uint8_t)((value >> 16) & 0xFFu);
    out[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static uint16_t read_u16_le(const uint8_t *input)
{
    return (uint16_t)((uint16_t)input[0] | ((uint16_t)input[1] << 8));
}

static uint32_t read_u32_le(const uint8_t *input)
{
    return (uint32_t)input[0]
        | ((uint32_t)input[1] << 8)
        | ((uint32_t)input[2] << 16)
        | ((uint32_t)input[3] << 24);
}

uint16_t fi_crc16_ccitt(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFu;
    size_t i;
    unsigned int bit;

    for (i = 0u; i < length; ++i) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (bit = 0u; bit < 8u; ++bit) {
            if ((crc & 0x8000u) != 0u) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

int fi_message_type_is_valid(uint8_t type)
{
    switch (type) {
    case FI_MSG_HELLO:
    case FI_MSG_HEARTBEAT:
    case FI_MSG_ARRIVED:
    case FI_MSG_CAPTURE_REQUEST:
    case FI_MSG_RESULT:
    case FI_MSG_SET_ROUTE:
    case FI_MSG_RELEASE:
    case FI_MSG_COMPLETE:
    case FI_MSG_ACK:
    case FI_MSG_FAULT:
        return 1;
    default:
        return 0;
    }
}

fi_status_t fi_frame_encode(
    const fi_frame_t *frame,
    uint8_t *out,
    size_t capacity,
    size_t *written)
{
    size_t total_length;
    uint16_t crc;

    if (written != NULL) {
        *written = 0u;
    }
    if (frame == NULL || out == NULL || written == NULL) {
        return FI_ERR_ARGUMENT;
    }
    if (!fi_message_type_is_valid((uint8_t)frame->type)) {
        return FI_ERR_TYPE;
    }
    if (frame->payload_length > FI_MAX_PAYLOAD_SIZE) {
        return FI_ERR_LENGTH;
    }

    total_length = FI_MIN_FRAME_SIZE + frame->payload_length;
    if (capacity < total_length) {
        return FI_ERR_CAPACITY;
    }

    out[0] = FI_MAGIC_0;
    out[1] = FI_MAGIC_1;
    out[2] = FI_PROTOCOL_VERSION;
    out[3] = (uint8_t)frame->type;
    write_u16_le(out + 4, frame->payload_length);
    write_u32_le(out + 6, frame->session_id);
    write_u32_le(out + 10, frame->job_id);
    write_u32_le(out + 14, frame->seq);
    if (frame->payload_length > 0u) {
        memcpy(out + FI_FIXED_FIELDS_SIZE, frame->payload, frame->payload_length);
    }

    crc = fi_crc16_ccitt(out + 2, total_length - 4u);
    write_u16_le(out + total_length - FI_CRC_SIZE, crc);
    *written = total_length;
    return FI_OK;
}

fi_status_t fi_frame_decode(
    const uint8_t *bytes,
    size_t length,
    fi_frame_t *out)
{
    fi_frame_t decoded;
    uint16_t payload_length;
    size_t expected_length;
    uint16_t expected_crc;
    uint16_t actual_crc;

    if (bytes == NULL || out == NULL) {
        return FI_ERR_ARGUMENT;
    }
    if (length < FI_MIN_FRAME_SIZE || length > FI_MAX_FRAME_SIZE) {
        return FI_ERR_LENGTH;
    }
    if (bytes[0] != FI_MAGIC_0 || bytes[1] != FI_MAGIC_1) {
        return FI_ERR_MAGIC;
    }
    if (bytes[2] != FI_PROTOCOL_VERSION) {
        return FI_ERR_VERSION;
    }
    if (!fi_message_type_is_valid(bytes[3])) {
        return FI_ERR_TYPE;
    }

    payload_length = read_u16_le(bytes + 4);
    if (payload_length > FI_MAX_PAYLOAD_SIZE) {
        return FI_ERR_LENGTH;
    }
    expected_length = FI_MIN_FRAME_SIZE + payload_length;
    if (length != expected_length) {
        return FI_ERR_LENGTH;
    }

    expected_crc = read_u16_le(bytes + length - FI_CRC_SIZE);
    actual_crc = fi_crc16_ccitt(bytes + 2, length - 4u);
    if (actual_crc != expected_crc) {
        return FI_ERR_CRC;
    }

    memset(&decoded, 0, sizeof(decoded));
    decoded.type = (fi_message_type_t)bytes[3];
    decoded.payload_length = payload_length;
    decoded.session_id = read_u32_le(bytes + 6);
    decoded.job_id = read_u32_le(bytes + 10);
    decoded.seq = read_u32_le(bytes + 14);
    if (payload_length > 0u) {
        memcpy(decoded.payload, bytes + FI_FIXED_FIELDS_SIZE, payload_length);
    }
    *out = decoded;
    return FI_OK;
}
