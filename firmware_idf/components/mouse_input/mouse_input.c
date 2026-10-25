/* SPDX-License-Identifier: MIT */
/**
 * @file mouse_input.c
 * @brief Debounce three keys without delaying normal left/right mouse presses.
 *
 * The FIFO stores absolute button states, retaining their order across BLE
 * congestion. Gesture recognition applies only to the mode key; the host OS
 * sees ordinary left/right press/release sequences for double-click and drag.
 */
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

/* Queue state transitions rather than a single pending action: otherwise a
 * quick release could overwrite an unsent press and silently lose a click.
 */
static void enqueue(mouse_input_t * const context, uint8_t buttons)
{
    if (context->count < MOUSE_INPUT_FIFO_SIZE)
    {
        /* Head/count invariants keep this ring index within fixed storage. */
        const uint8_t tail = (uint8_t)(((uint32_t)context->head + (uint32_t)context->count) %
                                      MOUSE_INPUT_FIFO_SIZE);
        context->fifo[tail] = buttons;
        context->count++;
    }
    else
    {
        /* Once ordering cannot be preserved, replaying only part of a gesture
         * could leave a host button held. Force all-buttons-up and require a
         * physical release before accepting a fresh sequence.
         */
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
        /* recenter is a pulse for this update, whereas scroll is a held level. */
        context->recenter = false;
        for (uint8_t index = 0U; index < MOUSE_INPUT_KEY_COUNT; ++index)
        {
            const uint8_t mask = (uint8_t)(UINT32_C(1) << index);
            /* Each key owns a separate settling timer; bounce on one key must
             * not postpone the other key's press or release.
             */
            if ((raw_keys & mask) != (context->candidate_keys & mask))
            {
                context->candidate_keys ^= mask;
                context->changed_ms[index] = now_ms;
            }
            /* Unsigned elapsed-time subtraction intentionally tolerates the
             * millisecond counter wrapping, as covered by the rollover test.
             */
            if ((uint32_t)(now_ms - context->changed_ms[index]) >= DEBOUNCE_MS)
            {
                context->stable_keys = (uint8_t)((context->stable_keys &
                    (uint8_t)(~mask)) | (context->candidate_keys & mask));
            }
        }
        /* Gesture duration starts at the accepted press, not at an electrical
         * bounce. A short release recenters; a long hold becomes wheel mode.
         */
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
                /* Releasing a long hold must exit scroll without also recentering. */
                context->recenter = !context->scroll;
            }
            context->scroll = false;
        }
        /* The mode key is local control and never becomes a HID mouse button. */
        const uint8_t buttons = (uint8_t)(context->stable_keys &
            (MOUSE_BUTTON_LEFT | MOUSE_BUTTON_RIGHT));
        if (!link_ready)
        {
            /* Never replay queued gestures after encryption/subscription loss or
             * into a later connection. Debounce and local mode handling continue.
             */
            context->armed = false;
            context->head = 0U;
            context->count = 0U;
            context->observed_buttons = 0U;
        }
        else if (!context->armed)
        {
            /* Check raw AND debounced release: immediately after reconnect/reset,
             * debounced state may still be zero while a physical key is held.
             */
            if ((buttons == 0U) && ((raw_keys & UINT8_C(0x03)) == 0U))
            {
                context->armed = true;
                context->overflow = false;
                context->observed_buttons = 0U;
                /* Establish a known released state before the next real press. */
                enqueue(context, 0U);
            }
        }
        else if (buttons != context->observed_buttons)
        {
            /* A held state needs no repeated edge; simultaneous keys remain a
             * single absolute snapshot and therefore support dragging/chords.
             */
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
            /* Peeking does not consume: failed transport sends retry this state. */
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
            /* Only the caller knows that the SDK accepted the corresponding
             * report; advancing here makes the next ordered state visible.
             */
            context->head = (uint8_t)((context->head + 1U) % MOUSE_INPUT_FIFO_SIZE);
            context->count--;
        }
    }
}
