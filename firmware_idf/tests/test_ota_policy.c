/* Verification code; not part of the production MISRA scope. */
/**
 * @file test_ota_policy.c
 * @brief Manifest acceptance/rejection and numeric release-order regression checks.
 */
#include "test_ota_policy.h"
#include "ota_policy.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/** Complete supported metadata fixture; its digest tests decoding, not a real image. */
static const char VALID[] =
    "schema=1\nproject=esp_mouse\ntarget=esp32s3\nboard=esp32s3-n16r8\n"
    "version=1.2.3\nsize=123456\n"
    "sha256=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n"
    "asset=esp32s3-n16r8.bin\n";

/** Change one property of a valid fixture so each rejection has a clear cause. */
static void reject_replacement(const char * const original, const char * const replacement)
{
    char buffer[OTA_MANIFEST_CAPACITY] = {0};
    const char * const found = strstr(VALID, original);
    assert(found != NULL);
    const size_t prefix = (size_t)(found - VALID);
    const int count = snprintf(buffer, sizeof(buffer), "%.*s%s%s", (int)prefix,
                                VALID, replacement, found + strlen(original));
    assert(count > 0 && (size_t)count < sizeof(buffer));
    ota_manifest_t parsed = {0};
    assert(!ota_policy_parse(buffer, (size_t)count, &parsed));
}

/** Exercise five policy groups and return their count to the common host runner. */
uint32_t test_ota_policy(void)
{
    ota_manifest_t parsed = {0};
    assert(ota_policy_parse(VALID, sizeof(VALID) - 1U, &parsed));
    assert(strcmp(parsed.version, "1.2.3") == 0);
    assert(parsed.image_size == 123456U);
    assert(parsed.sha256[0] == 0x01U && parsed.sha256[31] == 0xEFU);
    assert(ota_policy_parse(VALID, sizeof(VALID) - 2U, &parsed)); /* final LF optional */
    assert(!ota_policy_parse(NULL, 0U, &parsed));
    assert(!ota_policy_parse(VALID, sizeof(VALID) - 1U, NULL));
    /* Identity checks prevent a release for another project/board from being accepted. */
    reject_replacement("schema=1", "schema=2");
    reject_replacement("project=esp_mouse", "project=other_project");
    reject_replacement("target=esp32s3", "target=esp32");
    reject_replacement("board=esp32s3-n16r8", "board=esp32s3-n8r2");
    reject_replacement("asset=esp32s3-n16r8.bin", "asset=../foreign.bin");
    /* Canonical versions and bounded decimal values prevent path/overflow ambiguity. */
    reject_replacement("version=1.2.3", "version=1.2.3-rc1");
    reject_replacement("version=1.2.3", "version=1.02.3");
    reject_replacement("version=1.2.3", "version=4294967296.2.3");
    reject_replacement("size=123456", "size=0");
    reject_replacement("size=123456", "size=6291457");
    reject_replacement("size=123456", "size=4294967296");
    reject_replacement("sha256=012", "sha256=XYZ");
    /* A valid prefix must not hide duplicate, unknown, missing or binary content. */
    reject_replacement("schema=1\n", "schema=1\nschema=1\n");
    reject_replacement("schema=1\n", "unknown=1\n");
    reject_replacement("schema=1\n", "");
    assert(!ota_policy_parse(VALID, sizeof(VALID), &parsed)); /* embedded NUL */
    char oversized[OTA_MANIFEST_CAPACITY] = {0};
    assert(!ota_policy_parse(oversized, sizeof(oversized), &parsed));
    /* Numeric comparison must order 1.10 above 1.9 and never permit a downgrade. */
    assert(ota_policy_newer("1.10.0", "1.9.9"));
    assert(ota_policy_newer("2.0.0", "1.99.99"));
    assert(ota_policy_newer("1.1.1", "1.1.0"));
    assert(!ota_policy_newer("1.1.0", "1.1.0"));
    assert(!ota_policy_newer("1.1.0", "1.1.1"));
    assert(!ota_policy_newer("1.1.0-rc1", "1.0.0"));
    assert(!ota_policy_newer("1.2", "1.0.0"));
    assert(!ota_policy_newer("1.2.3/../x", "1.0.0"));
    assert(!ota_policy_newer(NULL, "1.0.0"));
    assert(!ota_policy_newer("1.2.3", NULL));
    return 5U;
}
