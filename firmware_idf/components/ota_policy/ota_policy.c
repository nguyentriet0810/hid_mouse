/* SPDX-License-Identifier: MIT */
#include "ota_policy.h"
#include <string.h>

static bool decimal(const char * const text, size_t length, uint32_t * const number)
{
    bool valid = (length > 0U) && (length <= 10U);
    uint32_t value = 0U;
    for (size_t index = 0U; (index < length) && valid; ++index)
    {
        if ((text[index] < '0') || (text[index] > '9'))
        {
            valid = false;
        }
        else
        {
            const uint32_t digit = (uint32_t)((uint8_t)text[index] - (uint8_t)'0');
            if (value > ((UINT32_MAX - digit) / UINT32_C(10)))
            {
                valid = false;
            }
            else
            {
                value = (value * UINT32_C(10)) + digit;
            }
        }
    }
    if (valid)
    {
        *number = value;
    }
    return valid;
}

static bool version_numbers(const char * const text, uint32_t numbers[3])
{
    bool valid = (text != NULL);
    size_t position = 0U;
    for (uint8_t part = 0U; (part < 3U) && valid; ++part)
    {
        const size_t start = position;
        while ((position < (OTA_VERSION_CAPACITY - 1U)) &&
               (text[position] >= '0') && (text[position] <= '9'))
        {
            ++position;
        }
        const size_t length = position - start;
        valid = decimal(&text[start], length, &numbers[part]);
        if (valid && (length > 1U) && (text[start] == '0'))
        {
            valid = false;
        }
        if (part < 2U)
        {
            valid = valid && (text[position] == '.');
            if (valid)
            {
                ++position;
            }
        }
        else
        {
            valid = valid && (text[position] == '\0');
        }
    }
    return valid;
}

bool ota_policy_newer(const char * const candidate, const char * const current)
{
    uint32_t next[3] = {0U};
    uint32_t previous[3] = {0U};
    bool newer = false;
    bool decided = false;
    if (version_numbers(candidate, next) && version_numbers(current, previous))
    {
        for (uint8_t index = 0U; (index < 3U) && (!decided); ++index)
        {
            if (next[index] != previous[index])
            {
                newer = (next[index] > previous[index]);
                decided = true;
            }
        }
    }
    return newer;
}

static bool same_text(const char * const text, size_t length, const char * const expected)
{
    return (strlen(expected) == length) && (memcmp(text, expected, length) == 0);
}

static bool hex_digit(char character, uint8_t * const digit)
{
    bool valid = true;
    if ((character >= '0') && (character <= '9'))
    {
        *digit = (uint8_t)((uint8_t)character - (uint8_t)'0');
    }
    else if ((character >= 'a') && (character <= 'f'))
    {
        *digit = (uint8_t)((uint8_t)character - (uint8_t)'a' + UINT8_C(10));
    }
    else
    {
        valid = false;
    }
    return valid;
}

static bool parse_value(uint8_t key, const char * const value, size_t length,
                         ota_manifest_t * const manifest)
{
    bool valid = false;
    switch (key)
    {
        case 0U:
            valid = same_text(value, length, "1");
            break;
        case 1U:
            valid = same_text(value, length, OTA_PROJECT_NAME);
            break;
        case 2U:
            valid = same_text(value, length, "esp32s3");
            break;
        case 3U:
            valid = same_text(value, length, OTA_BOARD_NAME);
            break;
        case 4U:
            if ((length > 0U) && (length < OTA_VERSION_CAPACITY))
            {
                (void)memcpy(manifest->version, value, length);
                manifest->version[length] = '\0';
                uint32_t numbers[3] = {0U};
                valid = version_numbers(manifest->version, numbers);
            }
            break;
        case 5U:
            valid = decimal(value, length, &manifest->image_size);
            valid = valid && (manifest->image_size > 0U) &&
                    (manifest->image_size <= OTA_MAX_IMAGE_SIZE);
            break;
        case 6U:
            valid = (length == 64U);
            for (uint8_t index = 0U; (index < 32U) && valid; ++index)
            {
                uint8_t high = 0U;
                uint8_t low = 0U;
                valid = hex_digit(value[2U * index], &high) &&
                        hex_digit(value[(2U * index) + 1U], &low);
                manifest->sha256[index] = (uint8_t)(((uint32_t)high << 4U) | low);
            }
            break;
        case 7U:
            valid = same_text(value, length, OTA_ASSET_NAME);
            break;
        default:
            break;
    }
    return valid;
}

bool ota_policy_parse(const char * const data, size_t length,
                       ota_manifest_t * const manifest)
{
    static const char * const keys[8] = {
        "schema", "project", "target", "board", "version", "size", "sha256", "asset"
    };
    bool valid = (data != NULL) && (manifest != NULL) &&
                 (length > 0U) && (length < OTA_MANIFEST_CAPACITY);
    ota_manifest_t parsed = {0};
    uint32_t seen = 0U;
    size_t position = 0U;
    while ((position < length) && valid)
    {
        const size_t start = position;
        size_t separator = length;
        while ((position < length) && (data[position] != '\n'))
        {
            if (((uint8_t)data[position] < UINT8_C(0x20)) && (data[position] != '\r'))
            {
                valid = false;
            }
            if ((data[position] == '=') && (separator == length))
            {
                separator = position;
            }
            ++position;
        }
        size_t end = position;
        if ((end > start) && (data[end - 1U] == '\r'))
        {
            --end;
        }
        valid = valid && (separator > start) && (separator < end);
        bool matched = false;
        for (uint8_t key = 0U; (key < 8U) && valid && (!matched); ++key)
        {
            if (same_text(&data[start], separator - start, keys[key]))
            {
                const uint32_t mask = UINT32_C(1) << key;
                matched = true;
                valid = ((seen & mask) == 0U) && parse_value(key,
                    &data[separator + 1U], end - separator - 1U, &parsed);
                seen |= mask;
            }
        }
        valid = valid && matched;
        if (position < length)
        {
            ++position;
        }
    }
    valid = valid && (seen == UINT32_C(0xFF));
    if (valid)
    {
        *manifest = parsed;
    }
    return valid;
}
