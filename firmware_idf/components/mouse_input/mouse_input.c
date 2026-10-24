/* SPDX-License-Identifier: MIT */
#include "mouse_input.h"
#include "mouse_types.h"
#include <stddef.h>

#define DEBOUNCE_MS UINT32_C(20)
#define SCROLL_HOLD_MS UINT32_C(300)

void mouse_input_reset(mouse_input_t * const context)
{
    if (context != NULL)
    {
        const mouse_input_t initial = {0};
        *context = initial;
    }
}

static void enqueue(mouse_input_t * const context, uint8_t buttons)
{
    if (context->count < MOUSE_INPUT_FIFO_SIZE)
    {
        const uint8_t tail = (uint8_t)(((uint32_t)context->head + (uint32_t)context->count) %
                                      MOUSE_INPUT_FIFO_SIZE);
        context->fifo[tail] = buttons;
        context->count++;
    }
    else
    {
        /* Fail safe: discard backlog, force release, require physical release. */
        context->overflow = true;
        context->armed = false;
        context->head = 0U;
        context->count = 1U;
        context->fifo[0] = 0U;
    }
}

void mouse_input_update(mouse_input_t * const context, uint8_t raw_keys,
                        uint32_t now_ms, bool link_ready)
{
    if (context != NULL)
    {
        const uint8_t previous = context->stable_keys;
        context->recenter = false;
        for (uint8_t index = 0U; index < MOUSE_INPUT_KEY_COUNT; ++index)
        {
            const uint8_t mask = (uint8_t)(UINT32_C(1) << index);
            if ((raw_keys & mask) != (context->candidate_keys & mask))
            {
                context->candidate_keys ^= mask;
                context->changed_ms[index] = now_ms;
            }
            if ((uint32_t)(now_ms - context->changed_ms[index]) >= DEBOUNCE_MS)
            {
                context->stable_keys = (uint8_t)((context->stable_keys &
                    (uint8_t)(~mask)) | (context->candidate_keys & mask));
            }
        }
        if (((previous & MOUSE_INPUT_MODE_MASK) == 0U) &&
            ((context->stable_keys & MOUSE_INPUT_MODE_MASK) != 0U))
        {
            context->mode_pressed_ms = now_ms;
        }
        if ((context->stable_keys & MOUSE_INPUT_MODE_MASK) != 0U)
        {
            context->scroll = ((uint32_t)(now_ms - context->mode_pressed_ms) >=
                               SCROLL_HOLD_MS);
        }
        else
        {
            if ((previous & MOUSE_INPUT_MODE_MASK) != 0U)
            {
                context->recenter = !context->scroll;
            }
            context->scroll = false;
        }
        const uint8_t buttons = (uint8_t)(context->stable_keys &
            (MOUSE_BUTTON_LEFT | MOUSE_BUTTON_RIGHT));
        if (!link_ready)
        {
            context->armed = false;
            context->head = 0U;
            context->count = 0U;
            context->observed_buttons = 0U;
        }
        else if (!context->armed)
        {
            if ((buttons == 0U) && ((raw_keys & UINT8_C(0x03)) == 0U))
            {
                context->armed = true;
                context->overflow = false;
                context->observed_buttons = 0U;
                enqueue(context, 0U);
            }
        }
        else if (buttons != context->observed_buttons)
        {
            context->observed_buttons = buttons;
            enqueue(context, buttons);
        }
        else
        {
            /* Stable buttons: no additional edge to queue. */
        }
    }
}

bool mouse_input_peek(const mouse_input_t * const context, uint8_t * const buttons)
{
    bool available = false;
    if ((context != NULL) && (buttons != NULL))
    {
        available = (context->count > 0U);
        if (available)
        {
            *buttons = context->fifo[context->head];
        }
    }
    return available;
}

void mouse_input_ack(mouse_input_t * const context)
{
    if (context != NULL)
    {
        if (context->count > 0U)
        {
            context->head = (uint8_t)((context->head + 1U) % MOUSE_INPUT_FIFO_SIZE);
            context->count--;
        }
    }
}
