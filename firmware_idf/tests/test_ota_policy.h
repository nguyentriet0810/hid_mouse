/** @file test_ota_policy.h
 *  @brief Shared entry point for the host-only OTA policy assertion groups.
 */
#ifndef TEST_OTA_POLICY_H
#define TEST_OTA_POLICY_H
#include <stdint.h>
/** @return Number of completed logical groups; an assertion failure terminates the test. */
uint32_t test_ota_policy(void);
#endif
