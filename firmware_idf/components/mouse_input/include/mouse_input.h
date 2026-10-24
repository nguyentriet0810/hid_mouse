/* SPDX-License-Identifier: MIT */
#ifndef MOUSE_INPUT_H
#define MOUSE_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#define MOUSE_INPUT_KEY_COUNT 3U
#define MOUSE_INPUT_FIFO_SIZE 16U
#define MOUSE_INPUT_MODE_MASK UINT8_C(0x04)

typedef struct
{
    uint8_t stable_keys;
    uint8_t candidate_keys;
    uint32_t changed_ms[MOUSE_INPUT_KEY_COUNT];
    uint32_t mode_pressed_ms;
    bool scroll;
    bool recenter;
    bool armed;
    bool overflow;
    uint8_t observed_buttons;
    uint8_t fifo[MOUSE_INPUT_FIFO_SIZE];
    uint8_t head;
    uint8_t count;
} mouse_input_t;

/* Single owner; update at least every 10 ms. now_ms may wrap at UINT32_MAX. */
void mouse_input_reset(mouse_input_t * const context);
void mouse_input_update(mouse_input_t * const context, uint8_t raw_keys,
                        uint32_t now_ms, bool link_ready);
bool mouse_input_peek(const mouse_input_t * const context, uint8_t * const buttons);
void mouse_input_ack(mouse_input_t * const context);

#endif
