/* SPDX-License-Identifier: MIT */
/**
 * @file mouse_input.h
 * @brief Hardware-independent key debounce, mode gestures and ordered HID button states.
 *
 * A single application task owns the context and all API calls. Raw key bits are
 * logical pressed states, not GPIO levels: bits 0/1 are left/right and bit 2 is
 * mode. The board adapter performs the active-low electrical conversion.
 */
#ifndef MOUSE_INPUT_H
#define MOUSE_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#define MOUSE_INPUT_KEY_COUNT 3U
#define MOUSE_INPUT_FIFO_SIZE 16U
#define MOUSE_INPUT_MODE_MASK UINT8_C(0x04)

/** @brief Caller-owned debouncer and bounded FIFO; not safe for concurrent access. */
typedef struct
{
    uint8_t stable_keys; /**< Debounced pressed mask, including the mode key. */
    uint8_t candidate_keys; /**< Most recent raw state being timed for each key. */
    uint32_t changed_ms[MOUSE_INPUT_KEY_COUNT]; /**< Raw transition timestamp per key. */
    uint32_t mode_pressed_ms; /**< Timestamp of the debounced mode press. */
    bool scroll; /**< Level flag: mode has been held for at least 300 ms. */
    bool recenter; /**< One-update pulse when a short mode press is released. */
    bool armed; /**< Button delivery permitted after a physical release on this link. */
    bool overflow; /**< Latched FIFO overflow; cleared on reset or successful re-arm. */
    uint8_t observed_buttons; /**< Last button state queued, not necessarily transmitted. */
    uint8_t fifo[MOUSE_INPUT_FIFO_SIZE]; /**< Ordered absolute button-state snapshots. */
    uint8_t head; /**< Index of the oldest state waiting for transport acceptance. */
    uint8_t count; /**< Number of queued snapshots, bounded by FIFO capacity. */
} mouse_input_t;

/**
 * @brief Reset debounce, gesture and queued-button state to a disarmed state.
 * @param[in,out] context Caller-owned storage; NULL is ignored.
 * @note Call at initialization and connection-generation changes. Old presses
 *       must never be replayed into a different host/session.
 */
void mouse_input_reset(mouse_input_t * const context);
/**
 * @brief Advance independent debouncers and enqueue left/right state changes.
 * @param[in,out] context Initialized single-owner input context; NULL is ignored.
 * @param[in] raw_keys Logical pressed mask: left bit 0, right bit 1, mode bit 2.
 * @param[in] now_ms Monotonic millisecond timestamp; unsigned rollover is supported.
 * @param[in] link_ready Host is connected, encrypted, subscribed and not suspended.
 *                     Temporary transmit congestion is handled by the sender,
 *                     without clearing this session-readiness flag.
 * @note Call every 10 ms or faster. Debounce requires 20 ms of stable raw state.
 *       Mode gestures continue while the link is inactive; button delivery is
 *       disarmed and queued button states are discarded until physical release.
 */
void mouse_input_update(mouse_input_t * const context, uint8_t raw_keys,
                        uint32_t now_ms, bool link_ready);
/**
 * @brief Inspect the oldest queued state without removing it.
 * @param[in] context Owning task's input state.
 * @param[out] buttons Receives an absolute left/right pressed mask when available.
 * @return True if a state was copied; false for an empty FIFO or NULL arguments.
 * @note A failed read leaves the caller's buttons value unchanged.
 */
bool mouse_input_peek(const mouse_input_t * const context, uint8_t * const buttons);
/**
 * @brief Remove the oldest state only after its HID report is accepted by the SDK.
 * @param[in,out] context Owning task's input state; NULL/empty contexts are ignored.
 * @note Do not acknowledge a failed send: a later release must not overtake a press.
 */
void mouse_input_ack(mouse_input_t * const context);

#endif
