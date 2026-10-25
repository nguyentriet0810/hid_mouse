/* Fault-injection tests for the real production CCC store, not a copied codec. */
#include "ble_ccc_store.h"
#include "nvs.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    (void)fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); \
} } while (0)

typedef struct
{
    char key[14];
    uint8_t value;
    bool used;
} record_t;

static record_t records[8];
static bool namespace_exists;
static bool opened;
static int pending_index;
static uint8_t pending_value;
static unsigned int sets;
static unsigned int commits;
static unsigned int closes;
static esp_err_t open_error;
static esp_err_t get_error;
static esp_err_t set_error;
static esp_err_t commit_error;
static const ble_ccc_peer_t phone = {{1U, 2U, 3U, 4U, 5U, 6U}, 0U};

static void reset_fake(void)
{
    (void)memset(records, 0, sizeof(records));
    namespace_exists = false;
    opened = false;
    pending_index = -1;
    pending_value = 0U;
    sets = 0U;
    commits = 0U;
    closes = 0U;
    open_error = ESP_OK;
    get_error = ESP_OK;
    set_error = ESP_OK;
    commit_error = ESP_OK;
}

static int find_key(const char *key)
{
    for (int index = 0; index < 8; index++)
    {
        if (records[index].used && (strcmp(records[index].key, key) == 0))
        {
            return index;
        }
    }
    return -1;
}

esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *handle)
{
    CHECK(strcmp(name, "hid_ccc") == 0);
    CHECK(!opened);
    if (open_error != ESP_OK) { return open_error; }
    if ((!namespace_exists) && (mode == NVS_READONLY)) { return ESP_ERR_NVS_NOT_FOUND; }
    namespace_exists = true;
    opened = true;
    *handle = 7U;
    return ESP_OK;
}

esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *value)
{
    CHECK(opened && (handle == 7U));
    CHECK(strlen(key) == 13U);
    if (get_error != ESP_OK) { return get_error; }
    const int index = find_key(key);
    if (index < 0) { return ESP_ERR_NVS_NOT_FOUND; }
    *value = records[index].value;
    return ESP_OK;
}

esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value)
{
    CHECK(opened && (handle == 7U));
    sets++;
    if (set_error != ESP_OK) { return set_error; }
    int index = find_key(key);
    for (int next = 0; (index < 0) && (next < 8); next++)
    {
        if (!records[next].used) { index = next; }
    }
    CHECK(index >= 0);
    (void)strcpy(records[index].key, key);
    pending_index = index;
    pending_value = value;
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    CHECK(opened && (handle == 7U) && (pending_index >= 0));
    commits++;
    if (commit_error != ESP_OK) { return commit_error; }
    records[pending_index].value = pending_value;
    records[pending_index].used = true;
    pending_index = -1;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle)
{
    CHECK(opened && (handle == 7U));
    opened = false;
    pending_index = -1;
    closes++;
}

int main(void)
{
    uint8_t flags = 99U;
    reset_fake();
    CHECK(ble_ccc_store_load(&phone, &flags) == ESP_ERR_NVS_NOT_FOUND);
    CHECK((flags == 0U) && (closes == 0U));
    (void)puts("PASS 1: missing persistence is disabled");

    CHECK(ble_ccc_store_save(&phone, BLE_CCC_MOUSE) == ESP_OK);
    CHECK(ble_ccc_store_load(&phone, &flags) == ESP_OK && flags == BLE_CCC_MOUSE);
    CHECK(ble_ccc_store_save(&phone, BLE_CCC_MASK) == ESP_OK);
    CHECK(ble_ccc_store_load(&phone, &flags) == ESP_OK && flags == BLE_CCC_MASK);
    (void)puts("PASS 2: mouse and battery round trip");

    ble_ccc_peer_t other = phone;
    other.address[5] = 7U;
    CHECK(ble_ccc_store_save(&other, BLE_CCC_BATTERY) == ESP_OK);
    CHECK(ble_ccc_store_load(&phone, &flags) == ESP_OK && flags == BLE_CCC_MASK);
    CHECK(ble_ccc_store_load(&other, &flags) == ESP_OK && flags == BLE_CCC_BATTERY);
    other = phone;
    other.address_type = 1U;
    CHECK(ble_ccc_store_save(&other, 0U) == ESP_OK);
    CHECK(ble_ccc_store_load(&phone, &flags) == ESP_OK && flags == BLE_CCC_MASK);
    CHECK(ble_ccc_store_load(&other, &flags) == ESP_OK && flags == 0U);
    (void)puts("PASS 3: address and public/random identity isolation");

    CHECK(ble_ccc_store_save(&phone, 0U) == ESP_OK);
    CHECK(ble_ccc_store_load(&phone, &flags) == ESP_OK && flags == 0U);
    (void)puts("PASS 4: explicit unsubscribe survives reload");

    const unsigned int previous_sets = sets;
    const unsigned int previous_commits = commits;
    CHECK(ble_ccc_store_save(&phone, 0U) == ESP_OK);
    CHECK(sets == previous_sets && commits == previous_commits);
    (void)puts("PASS 5: unchanged reconnects do not rewrite flash");

    const int phone_index = find_key("p010203040506");
    CHECK(phone_index >= 0);
    records[phone_index].value = UINT8_C(0xFF);
    flags = 99U;
    CHECK(ble_ccc_store_load(&phone, &flags) == ESP_ERR_INVALID_RESPONSE && flags == 0U);
    (void)puts("PASS 6: unknown schema/reserved flags fail closed");

    CHECK(ble_ccc_store_load(NULL, &flags) == ESP_ERR_INVALID_ARG && flags == 0U);
    CHECK(ble_ccc_store_load(&phone, NULL) == ESP_ERR_INVALID_ARG);
    CHECK(ble_ccc_store_save(NULL, 0U) == ESP_ERR_INVALID_ARG);
    CHECK(ble_ccc_store_save(&phone, 4U) == ESP_ERR_INVALID_ARG);
    other.address_type = 2U;
    CHECK(ble_ccc_store_load(&other, &flags) == ESP_ERR_INVALID_ARG && flags == 0U);
    CHECK(ble_ccc_store_save(&other, 0U) == ESP_ERR_INVALID_ARG);
    (void)puts("PASS 7: invalid arguments do not access NVS");

    open_error = ESP_FAIL;
    const unsigned int previous_closes = closes;
    flags = 99U;
    CHECK(ble_ccc_store_load(&phone, &flags) == ESP_FAIL && flags == 0U);
    CHECK(ble_ccc_store_save(&phone, 0U) == ESP_FAIL && closes == previous_closes);
    open_error = ESP_OK;
    (void)puts("PASS 8: open failure is propagated without invalid close");

    get_error = ESP_FAIL;
    flags = 99U;
    CHECK(ble_ccc_store_load(&phone, &flags) == ESP_FAIL && flags == 0U && !opened);
    CHECK(ble_ccc_store_save(&phone, 0U) == ESP_FAIL && !opened);
    CHECK(sets == previous_sets && commits == previous_commits);
    get_error = ESP_OK;
    (void)puts("PASS 9: read failure cannot silently overwrite storage");

    set_error = ESP_FAIL;
    CHECK(ble_ccc_store_save(&phone, 0U) == ESP_FAIL && !opened);
    CHECK(commits == previous_commits);
    set_error = ESP_OK;
    (void)puts("PASS 10: write failure skips commit and closes handle");

    CHECK(ble_ccc_store_save(&phone, BLE_CCC_MOUSE) == ESP_OK);
    commit_error = ESP_FAIL;
    CHECK(ble_ccc_store_save(&phone, 0U) == ESP_FAIL && !opened);
    commit_error = ESP_OK;
    CHECK(ble_ccc_store_load(&phone, &flags) == ESP_OK && flags == BLE_CCC_MOUSE);
    (void)puts("PASS 11: commit error is reported; prior committed value retained");

    flags = 0U; /* Simulated application restart: only the fake NVS records remain. */
    CHECK(ble_ccc_store_load(&phone, &flags) == ESP_OK && flags == BLE_CCC_MOUSE);
    CHECK(!opened && pending_index < 0);
    (void)puts("PASS 12: reload after application state reset; no leaked handles");
    (void)puts("BLE CCC persistence: 12 groups passed");
    return 0;
}
