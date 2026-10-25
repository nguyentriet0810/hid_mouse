/* SPDX-License-Identifier: MIT
 * ESP-IDF boundary adapter. See docs/MISRA.md for SDK-specific deviations.
 */
/**
 * @file ble_mouse.c
 * @brief Report-mode HID-over-GATT server and its connection state machine.
 *
 * GAP/GATT callbacks run in SDK task context while the main task sends reports.
 * Only the small connection record is shared under a spinlock; SDK calls never
 * execute while that lock is held. Initialization uses a separate event group
 * because attribute creation and advertising-data configuration are asynchronous.
 */
#include "ble_mouse.h"
#include "ble_ccc_store.h"
#include <stddef.h>
#include <string.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wundef"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_log.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#pragma GCC diagnostic pop

/* Advertising must wait for both payloads and all three GATT services. A single
 * failure bit makes callback errors visible to the bounded initialization wait. */
#define INIT_ADV_BIT BIT0
#define INIT_SCAN_BIT BIT1
#define INIT_HID_BIT BIT2
#define INIT_INFO_BIT BIT3
#define INIT_FAILURE_BIT BIT4
#define INIT_BATTERY_BIT BIT5
#define INIT_REQUIRED_BITS (INIT_ADV_BIT | INIT_SCAN_BIT | INIT_HID_BIT | INIT_INFO_BIT | INIT_BATTERY_BIT)
#define HID_SERVICE_INSTANCE UINT8_C(0)
#define INFO_SERVICE_INSTANCE UINT8_C(1)
#define BATTERY_SERVICE_INSTANCE UINT8_C(2)
#define MOUSE_REPORT_ID UINT8_C(1)

/* Attribute indexes must match the designated entries in s_hid_database. The
 * SDK fills handle arrays in this same order after table creation succeeds. */
typedef enum
{
    HID_SERVICE = 0,
    HID_INFO_DECLARATION,
    HID_INFO_VALUE,
    HID_MAP_DECLARATION,
    HID_MAP_VALUE,
    HID_CONTROL_DECLARATION,
    HID_CONTROL_VALUE,
    HID_REPORT_DECLARATION,
    HID_REPORT_VALUE,
    HID_REPORT_CCC,
    HID_REPORT_REFERENCE,
    HID_ATTRIBUTE_COUNT
} hid_attribute_t;

typedef enum
{
    INFO_SERVICE = 0,
    INFO_MANUFACTURER_DECLARATION,
    INFO_MANUFACTURER_VALUE,
    INFO_MODEL_DECLARATION,
    INFO_MODEL_VALUE,
    INFO_PNP_DECLARATION,
    INFO_PNP_VALUE,
    INFO_ATTRIBUTE_COUNT
} info_attribute_t;

typedef enum
{
    BATTERY_SERVICE = 0,
    BATTERY_LEVEL_DECLARATION,
    BATTERY_LEVEL_VALUE,
    BATTERY_LEVEL_CCC,
    BATTERY_ATTRIBUTE_COUNT
} battery_attribute_t;

typedef struct
{
    /** Link-layer connection exists, independently of pairing readiness. */
    bool connected;
    /** Authentication completed successfully for the current link. */
    bool encrypted;
    /** Host enabled the input-report CCC descriptor, not the battery CCC. */
    bool subscribed;
    /** Host HID control point requested suspend (0) rather than resume (1). */
    bool suspended;
    /** SDK transmit congestion temporarily prevents accepting a new report. */
    bool congested;
    /** Current GATT connection identifier; only use with a synchronized snapshot. */
    uint16_t connection_id;
    /** Session epoch; lets the application invalidate queued events after reconnect. */
    uint32_t generation;
} connection_t;

/* Only the SDK callback task owns this record and the bond lookup buffer.
 * Shared main-task readiness remains separately protected by s_state_lock. */
typedef struct
{
    bool active;
    bool bonded;
    bool new_keys;
    bool mouse_written;
    bool battery_written;
    bool mouse_notify;
    bool battery_notify;
    uint8_t address[ESP_BD_ADDR_LEN];
    uint8_t address_type;
    ble_ccc_peer_t identity;
} peer_t;

static peer_t s_peer = {0};
static esp_ble_bond_dev_t s_bond_scratch[CONFIG_BT_SMP_MAX_BONDS];
static const char * const TAG = "ble_mouse";
static StaticEventGroup_t s_init_storage;
static EventGroupHandle_t s_init_events = NULL;
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static connection_t s_connection = {0};
/* The interface and attribute handles are established before init returns and
 * then remain fixed for this single-instance server's lifetime. */
static esp_gatt_if_t s_interface = ESP_GATT_IF_NONE;
static uint16_t s_hid_handles[HID_ATTRIBUTE_COUNT];
static uint16_t s_info_handles[INFO_ATTRIBUTE_COUNT];
static uint16_t s_battery_handles[BATTERY_ATTRIBUTE_COUNT];

/* SDK attribute API takes mutable pointers. Objects remain static and writable;
 * application code does not cast away const (deviation D-02). */
static uint16_t s_primary_uuid = ESP_GATT_UUID_PRI_SERVICE;
static uint16_t s_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static uint16_t s_ccc_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
static uint16_t s_hid_uuid = UINT16_C(0x1812);
static uint16_t s_hid_info_uuid = UINT16_C(0x2A4A);
static uint16_t s_hid_map_uuid = UINT16_C(0x2A4B);
static uint16_t s_hid_control_uuid = UINT16_C(0x2A4C);
static uint16_t s_hid_report_uuid = UINT16_C(0x2A4D);
static uint16_t s_report_reference_uuid = UINT16_C(0x2908);
static uint16_t s_info_uuid = UINT16_C(0x180A);
static uint16_t s_manufacturer_uuid = UINT16_C(0x2A29);
static uint16_t s_model_uuid = UINT16_C(0x2A24);
static uint16_t s_pnp_uuid = UINT16_C(0x2A50);
static uint16_t s_battery_uuid = UINT16_C(0x180F);
static uint16_t s_battery_level_uuid = UINT16_C(0x2A19);
static uint8_t s_property_read = ESP_GATT_CHAR_PROP_BIT_READ;
static uint8_t s_property_write = ESP_GATT_CHAR_PROP_BIT_WRITE_NR;
static uint8_t s_property_notify = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
/* HID 1.11, no country code, normally connectable. Boot protocol is not exposed;
 * hosts use the Report Map and the input Report Reference characteristic. */
static uint8_t s_hid_information[4] = {0x11U, 0x01U, 0x00U, 0x02U};
static uint8_t s_control[1] = {0x01U};
static uint8_t s_report[4] = {0U};
static uint8_t s_ccc[2] = {0U};
static uint8_t s_reference[2] = {MOUSE_REPORT_ID, 0x01U};
static uint8_t s_manufacturer[] = "ESP Mouse Project";
static uint8_t s_model[] = "ESP32-S3 MPU6050";
/* Explicit development value, not a fuel-gauge measurement. */
static uint8_t s_battery_level = (uint8_t)CONFIG_MOUSE_BATTERY_LEVEL;
static uint8_t s_battery_ccc[2] = {0U};
static uint8_t s_pnp[7] = {
    0x01U, /* Bluetooth SIG vendor ID source. 0xFFFF = development placeholder. */
    (uint8_t)(CONFIG_MOUSE_VENDOR_ID & 0xFFU),
    (uint8_t)((CONFIG_MOUSE_VENDOR_ID >> 8U) & 0xFFU),
    0x01U, 0x00U, /* Product 0x0001. */
    0x00U, 0x01U  /* Version 0x0100. */
};
/* Report ID 1 describes four payload bytes:
 *   [0] left/right/middle bits + five padding bits
 *   [1] relative X, [2] relative Y, [3] relative wheel, each -127..127.
 * The ID is carried by the GATT Report Reference, not prefixed to that payload. */
static uint8_t s_report_map[] = {
    0x05U, 0x01U,       /* Generic Desktop */
    0x09U, 0x02U,       /* Mouse */
    0xA1U, 0x01U,       /* Application collection */
    0x85U, MOUSE_REPORT_ID,
    0x09U, 0x01U,       /* Pointer */
    0xA1U, 0x00U,       /* Physical collection */
    0x05U, 0x09U,       /* Buttons */
    0x19U, 0x01U, 0x29U, 0x03U, /* Button usages 1..3. */
    0x15U, 0x00U, 0x25U, 0x01U,
    0x95U, 0x03U, 0x75U, 0x01U, 0x81U, 0x02U, /* Three one-bit absolute button states. */
    0x95U, 0x01U, 0x75U, 0x05U, 0x81U, 0x03U, /* Pad the remaining five bits. */
    0x05U, 0x01U,
    0x09U, 0x30U,       /* X */
    0x09U, 0x31U,       /* Y */
    0x09U, 0x38U,       /* Wheel */
    0x15U, 0x81U, 0x25U, 0x7FU, /* Signed logical range -127..127. */
    0x75U, 0x08U, 0x95U, 0x03U, 0x81U, 0x06U, /* Three eight-bit relative values. */
    0xC0U, 0xC0U
};

/* Casts expose object representations to the vendor GATT API; see D-02. */
#define ATTRIBUTE(uuid_object, permission, maximum, length, pointer) \
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&(uuid_object), \
      (permission), (maximum), (length), (uint8_t *)(pointer)}}

/* AUTO_RSP lets the SDK serve the configured attribute values. The application
 * separately interprets accepted CCC/control writes to gate notification sends.
 * Sensitive HID values and descriptors require an encrypted connection. */
static const esp_gatts_attr_db_t s_hid_database[HID_ATTRIBUTE_COUNT] = {
    [HID_SERVICE] = ATTRIBUTE(s_primary_uuid, ESP_GATT_PERM_READ,
        2U, 2U, &s_hid_uuid),
    [HID_INFO_DECLARATION] = ATTRIBUTE(s_declaration_uuid, ESP_GATT_PERM_READ,
        1U, 1U, &s_property_read),
    [HID_INFO_VALUE] = ATTRIBUTE(s_hid_info_uuid, ESP_GATT_PERM_READ_ENCRYPTED,
        sizeof(s_hid_information), sizeof(s_hid_information), s_hid_information),
    [HID_MAP_DECLARATION] = ATTRIBUTE(s_declaration_uuid, ESP_GATT_PERM_READ,
        1U, 1U, &s_property_read),
    [HID_MAP_VALUE] = ATTRIBUTE(s_hid_map_uuid, ESP_GATT_PERM_READ_ENCRYPTED,
        sizeof(s_report_map), sizeof(s_report_map), s_report_map),
    [HID_CONTROL_DECLARATION] = ATTRIBUTE(s_declaration_uuid, ESP_GATT_PERM_READ,
        1U, 1U, &s_property_write),
    [HID_CONTROL_VALUE] = ATTRIBUTE(s_hid_control_uuid, ESP_GATT_PERM_WRITE_ENCRYPTED,
        sizeof(s_control), sizeof(s_control), s_control),
    [HID_REPORT_DECLARATION] = ATTRIBUTE(s_declaration_uuid, ESP_GATT_PERM_READ,
        1U, 1U, &s_property_notify),
    [HID_REPORT_VALUE] = ATTRIBUTE(s_hid_report_uuid, ESP_GATT_PERM_READ_ENCRYPTED,
        sizeof(s_report), sizeof(s_report), s_report),
    [HID_REPORT_CCC] = ATTRIBUTE(s_ccc_uuid,
        ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED,
        sizeof(s_ccc), sizeof(s_ccc), s_ccc),
    [HID_REPORT_REFERENCE] = ATTRIBUTE(s_report_reference_uuid, ESP_GATT_PERM_READ_ENCRYPTED,
        sizeof(s_reference), sizeof(s_reference), s_reference)
};

/* DIS supplies readable model/manufacturer/PnP identity without inventing a
 * production vendor assignment; the default vendor ID is a development value. */
static const esp_gatts_attr_db_t s_info_database[INFO_ATTRIBUTE_COUNT] = {
    [INFO_SERVICE] = ATTRIBUTE(s_primary_uuid, ESP_GATT_PERM_READ, 2U, 2U, &s_info_uuid),
    [INFO_MANUFACTURER_DECLARATION] = ATTRIBUTE(s_declaration_uuid, ESP_GATT_PERM_READ,
        1U, 1U, &s_property_read),
    [INFO_MANUFACTURER_VALUE] = ATTRIBUTE(s_manufacturer_uuid, ESP_GATT_PERM_READ,
        sizeof(s_manufacturer) - 1U, sizeof(s_manufacturer) - 1U, s_manufacturer),
    [INFO_MODEL_DECLARATION] = ATTRIBUTE(s_declaration_uuid, ESP_GATT_PERM_READ,
        1U, 1U, &s_property_read),
    [INFO_MODEL_VALUE] = ATTRIBUTE(s_model_uuid, ESP_GATT_PERM_READ,
        sizeof(s_model) - 1U, sizeof(s_model) - 1U, s_model),
    [INFO_PNP_DECLARATION] = ATTRIBUTE(s_declaration_uuid, ESP_GATT_PERM_READ,
        1U, 1U, &s_property_read),
    [INFO_PNP_VALUE] = ATTRIBUTE(s_pnp_uuid, ESP_GATT_PERM_READ,
        sizeof(s_pnp), sizeof(s_pnp), s_pnp)
};

/* BAS is present for HID host discovery. Its configured level is a fixed
 * development placeholder because the schematic has no battery measurement. */
static const esp_gatts_attr_db_t s_battery_database[BATTERY_ATTRIBUTE_COUNT] = {
    [BATTERY_SERVICE] = ATTRIBUTE(s_primary_uuid, ESP_GATT_PERM_READ,
        2U, 2U, &s_battery_uuid),
    [BATTERY_LEVEL_DECLARATION] = ATTRIBUTE(s_declaration_uuid, ESP_GATT_PERM_READ,
        1U, 1U, &s_property_notify),
    [BATTERY_LEVEL_VALUE] = ATTRIBUTE(s_battery_level_uuid, ESP_GATT_PERM_READ,
        1U, 1U, &s_battery_level),
    [BATTERY_LEVEL_CCC] = ATTRIBUTE(s_ccc_uuid,
        ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED,
        sizeof(s_battery_ccc), sizeof(s_battery_ccc), s_battery_ccc)
};

/* Advertising intervals use 0.625 ms units: 0x30..0x60 means 30..60 ms. */
static esp_ble_adv_params_t s_advertising = {
    .adv_int_min = 0x30U,
    .adv_int_max = 0x60U,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY
};
static uint8_t s_advertisement[] = {
    0x02U, 0x01U, 0x06U,             /* General discoverable, BLE only. */
    0x03U, 0x03U, 0x12U, 0x18U,      /* Complete list: HID UUID 0x1812. */
    0x03U, 0x19U, 0xC2U, 0x03U       /* Appearance: mouse 0x03C2. */
};
/* Put the full name in the scan response so the primary advertising packet can
 * retain HID UUID/appearance without exceeding the legacy 31-byte limit. */
static esp_ble_adv_data_t s_scan_response = {
    .set_scan_rsp = true,
    .include_name = true,
    .include_txpower = false
};

/** Log a callback-side SDK failure without replacing the caller's result. */
static void log_sdk_error(const char * const operation, esp_err_t result)
{
    if (result != ESP_OK)
    {
        ESP_LOGE(TAG, "%s: %s", operation, esp_err_to_name(result));
    }
}

/** Compare SDK bond identities without treating public/random address spaces alike. */
static bool same_identity(const ble_ccc_peer_t * const left,
                          const ble_ccc_peer_t * const right)
{
    return (left->address_type == right->address_type) &&
        (memcmp(left->address, right->address, sizeof(left->address)) == 0);
}

/**
 * Resolve only addresses already present in the SDK's bond database. Bluedroid
 * maps a resolved private address back to its stored pseudo address; PID keys
 * provide the stable public/static identity for our NVS key. No "last host"
 * fallback is allowed. The static scratch buffer avoids overflowing BTC's stack.
 */
static bool bonded_identity(const uint8_t address[ESP_BD_ADDR_LEN],
                            uint8_t address_type, ble_ccc_peer_t * const identity)
{
    bool found = false;
    int count = esp_ble_get_bond_device_num();
    (void)memset(s_bond_scratch, 0, sizeof(s_bond_scratch));
    if ((count > 0) && (count <= CONFIG_BT_SMP_MAX_BONDS))
    {
        const esp_err_t result = esp_ble_get_bond_device_list(&count, s_bond_scratch);
        if (result == ESP_OK)
        {
            for (int index = 0; (index < count) && (index < CONFIG_BT_SMP_MAX_BONDS) && (!found); index++)
            {
                const esp_ble_bond_dev_t * const bond = &s_bond_scratch[index];
                const bool has_identity =
                    (bond->bond_key.key_mask & ESP_BLE_ID_KEY_MASK) != 0U;
                if ((memcmp(address, bond->bd_addr, ESP_BD_ADDR_LEN) == 0) ||
                    (has_identity && (memcmp(address,
                        bond->bond_key.pid_key.static_addr, ESP_BD_ADDR_LEN) == 0)))
                {
                    const uint8_t * const stable_address = has_identity ?
                        bond->bond_key.pid_key.static_addr : bond->bd_addr;
                    (void)memcpy(identity->address, stable_address, ESP_BD_ADDR_LEN);
                    identity->address_type = has_identity ?
                        ((uint8_t)bond->bond_key.pid_key.addr_type & UINT8_C(1)) :
                        (address_type & UINT8_C(1));
                    found = true;
                }
            }
        }
        else
        {
            log_sdk_error("Read bond identities", result);
        }
    }
    /* Do not retain an extra application copy of the SDK's keys after lookup. */
    (void)memset(s_bond_scratch, 0, sizeof(s_bond_scratch));
    return found;
}

/** Called from serialized BLE callbacks; never from the application's main task. */
static bool current_connection(uint16_t connection_id)
{
    bool current;
    portENTER_CRITICAL(&s_state_lock);
    current = s_connection.connected && (s_connection.connection_id == connection_id);
    portEXIT_CRITICAL(&s_state_lock);
    return current;
}

/** Update the two shared attribute values for this single active connection. */
static void apply_ccc_attributes(void)
{
    s_ccc[0] = s_peer.mouse_notify ? 1U : 0U;
    s_ccc[1] = 0U;
    s_battery_ccc[0] = s_peer.battery_notify ? 1U : 0U;
    s_battery_ccc[1] = 0U;
    log_sdk_error("Set mouse subscription", esp_ble_gatts_set_attr_value(
        s_hid_handles[HID_REPORT_CCC], sizeof(s_ccc), s_ccc));
    log_sdk_error("Set battery subscription", esp_ble_gatts_set_attr_value(
        s_battery_handles[BATTERY_LEVEL_CCC], sizeof(s_battery_ccc), s_battery_ccc));
}

/** Persist accepted writes only for a currently authenticated SDK bond. */
static void save_subscriptions(void)
{
    if (s_peer.bonded)
    {
        const uint8_t flags = (s_peer.mouse_notify ? BLE_CCC_MOUSE : 0U) |
                              (s_peer.battery_notify ? BLE_CCC_BATTERY : 0U);
        log_sdk_error("Persist bonded subscriptions",
            ble_ccc_store_save(&s_peer.identity, flags));
    }
}

/**
 * Authenticate the active peer, then restore only that bond's CCC settings.
 * Fresh key exchange starts with defaults; an explicit write on this link takes
 * precedence over storage. No notifications are permitted before authentication.
 */
static void handle_authentication(const esp_ble_auth_cmpl_t * const authentication)
{
    ble_ccc_peer_t identity = {0};
    const bool bonded = authentication->success &&
        bonded_identity(authentication->bd_addr, (uint8_t)authentication->addr_type,
                        &identity);
    bool address_matches = memcmp(authentication->bd_addr, s_peer.address,
                                  ESP_BD_ADDR_LEN) == 0;
    if ((!address_matches) && bonded)
    {
        ble_ccc_peer_t active_identity = {0};
        address_matches = bonded_identity(s_peer.address, s_peer.address_type,
            &active_identity) && same_identity(&identity, &active_identity);
    }
    if (s_peer.active && address_matches)
    {
        if (authentication->success)
        {
            uint8_t flags = 0U;
            s_peer.bonded = bonded &&
                (((uint8_t)authentication->auth_mode & UINT8_C(1)) != 0U);
            s_peer.identity = identity;
            if (s_peer.bonded && (!s_peer.new_keys))
            {
                const esp_err_t result = ble_ccc_store_load(&identity, &flags);
                if ((result != ESP_OK) && (result != ESP_ERR_NVS_NOT_FOUND))
                {
                    log_sdk_error("Restore bonded subscriptions", result);
                }
            }
            if (!s_peer.mouse_written)
            {
                s_peer.mouse_notify = (flags & BLE_CCC_MOUSE) != 0U;
            }
            if (!s_peer.battery_written)
            {
                s_peer.battery_notify = (flags & BLE_CCC_BATTERY) != 0U;
            }
            apply_ccc_attributes();
            portENTER_CRITICAL(&s_state_lock);
            s_connection.encrypted = true;
            s_connection.subscribed = s_peer.mouse_notify;
            portEXIT_CRITICAL(&s_state_lock);
            save_subscriptions();
            ESP_LOGI(TAG, "Encrypted BLE connection established; auth_mode=0x%02x",
                (unsigned int)authentication->auth_mode);
            ESP_LOGI(TAG, "Bond subscriptions: bonded=%u fresh_keys=%u mouse=%u battery=%u",
                (unsigned int)s_peer.bonded, (unsigned int)s_peer.new_keys,
                (unsigned int)s_peer.mouse_notify, (unsigned int)s_peer.battery_notify);
        }
        else
        {
            portENTER_CRITICAL(&s_state_lock);
            s_connection.encrypted = false;
            s_connection.subscribed = false;
            portEXIT_CRITICAL(&s_state_lock);
            s_peer.bonded = false;
            ESP_LOGE(TAG, "Pairing failed: 0x%02x", (unsigned int)authentication->fail_reason);
            esp_bd_addr_t address;
            (void)memcpy(address, s_peer.address, sizeof(address));
            log_sdk_error("Disconnect after pairing failure", esp_ble_gap_disconnect(address));
        }
    }
    else
    {
        ESP_LOGW(TAG, "Ignoring authentication event outside the active peer session");
    }
}

/** Publish one asynchronous startup result to the application task. */
static void complete_initialization(esp_err_t result, EventBits_t bit)
{
    if (result == ESP_OK)
    {
        (void)xEventGroupSetBits(s_init_events, bit);
    }
    else
    {
        (void)xEventGroupSetBits(s_init_events, INIT_FAILURE_BIT);
        log_sdk_error("BLE initialization callback", result);
    }
}

/**
 * Handle advertising setup and pairing results in SDK callback context.
 *
 * A physical connection is insufficient for sending HID input: authentication
 * must also succeed. Pairing failure explicitly disconnects that peer.
 */
static void gap_callback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    if (param != NULL)
    {
        switch (event)
        {
            case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
                complete_initialization((param->adv_data_raw_cmpl.status ==
                    ESP_BT_STATUS_SUCCESS) ? ESP_OK : ESP_FAIL, INIT_ADV_BIT);
                break;
            case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
                complete_initialization((param->scan_rsp_data_cmpl.status ==
                    ESP_BT_STATUS_SUCCESS) ? ESP_OK : ESP_FAIL, INIT_SCAN_BIT);
                break;
            case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
                if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS)
                {
                    ESP_LOGI(TAG, "Advertising as %s", CONFIG_MOUSE_DEVICE_NAME);
                }
                else
                {
                    ESP_LOGE(TAG, "Advertising failed: %u",
                             (unsigned int)param->adv_start_cmpl.status);
                }
                break;
            case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
                ESP_LOGI(TAG, "Connection parameters: status=%u interval=%u latency=%u timeout=%u",
                    (unsigned int)param->update_conn_params.status,
                    (unsigned int)param->update_conn_params.conn_int,
                    (unsigned int)param->update_conn_params.latency,
                    (unsigned int)param->update_conn_params.timeout);
                break;
            case ESP_GAP_BLE_SEC_REQ_EVT:
                log_sdk_error("Security response", esp_ble_gap_security_rsp(
                    param->ble_security.ble_req.bd_addr, true));
                break;
            case ESP_GAP_BLE_KEY_EVT:
                /* Keys are emitted for fresh pairing, not normal bond reuse.
                 * Discard old CCC persistence for the new security association. */
                if (s_peer.active && (memcmp(s_peer.address,
                    param->ble_security.ble_key.bd_addr, ESP_BD_ADDR_LEN) == 0))
                {
                    s_peer.new_keys = true;
                }
                break;
            case ESP_GAP_BLE_AUTH_CMPL_EVT:
                handle_authentication(&param->ble_security.auth_cmpl);
                break;
            default:
                break;
        }
    }
}

/** Start a fresh session, request a responsive interval and initiate encryption. */
static void handle_connection(const esp_ble_gatts_cb_param_t * const param)
{
    /* Advertising stops on connect, so one peer owns the attribute CCC values. */
    (void)memset(&s_peer, 0, sizeof(s_peer));
    s_peer.active = true;
    s_peer.address_type = (uint8_t)param->connect.ble_addr_type;
    (void)memcpy(s_peer.address, param->connect.remote_bda, ESP_BD_ADDR_LEN);
    apply_ccc_attributes();
    esp_ble_conn_update_params_t parameters = {0};
    (void)memcpy(parameters.bda, param->connect.remote_bda, ESP_BD_ADDR_LEN);
    parameters.min_int = 8U;   /* 10 ms; the central chooses the final interval. */
    parameters.max_int = 16U;  /* 20 ms, in the same 1.25 ms connection units. */
    parameters.latency = 0U;
    parameters.timeout = 400U; /* Four-second supervision timeout, in 10 ms units. */
    portENTER_CRITICAL(&s_state_lock);
    s_connection.connected = true;
    s_connection.encrypted = false;
    s_connection.subscribed = false;
    s_connection.suspended = false;
    s_connection.congested = false;
    s_connection.connection_id = param->connect.conn_id;
    /* Reusing a numeric connection ID must not make old click events belong
     * to a new host session; the application observes this separate epoch. */
    s_connection.generation++;
    portEXIT_CRITICAL(&s_state_lock);
    ESP_LOGI(TAG, "BLE link connected: id=%u interval=%u latency=%u timeout=%u",
        (unsigned int)param->connect.conn_id,
        (unsigned int)param->connect.conn_params.interval,
        (unsigned int)param->connect.conn_params.latency,
        (unsigned int)param->connect.conn_params.timeout);
    log_sdk_error("Connection interval request", esp_ble_gap_update_conn_params(&parameters));
    /* API takes a mutable address; the vendor callback's address is mutable. */
    esp_bd_addr_t address;
    (void)memcpy(address, param->connect.remote_bda, sizeof(address));
    log_sdk_error("Enable encryption", esp_ble_set_encryption(address,
                   ESP_BLE_SEC_ENCRYPT_NO_MITM));
}

/**
 * Interpret complete writes to the HID subscription/control descriptors.
 * Prepared/offset writes and unrelated attributes never change readiness.
 */
static void handle_write(const esp_ble_gatts_cb_param_t * const param)
{
    /* Log metadata only: attribute values can contain host-specific data. */
    ESP_LOGI(TAG, "GATT write: connection=%u handle=0x%04x length=%u offset=%u prepared=%u",
        (unsigned int)param->write.conn_id, (unsigned int)param->write.handle,
        (unsigned int)param->write.len, (unsigned int)param->write.offset,
        (unsigned int)param->write.is_prep);
    if (current_connection(param->write.conn_id) && (param->write.value != NULL) &&
        (!param->write.is_prep) && (param->write.offset == 0U))
    {
        if ((param->write.handle == s_hid_handles[HID_REPORT_CCC]) &&
            (param->write.len == 2U))
        {
            /* CCC is little-endian 0x0001 for notifications. Indications and
             * malformed values do not enable this notification-only report. */
            const bool subscribed = (param->write.value[0] == 1U) &&
                                    (param->write.value[1] == 0U);
            s_peer.mouse_notify = subscribed;
            s_peer.mouse_written = true;
            save_subscriptions();
            portENTER_CRITICAL(&s_state_lock);
            s_connection.subscribed = subscribed;
            portEXIT_CRITICAL(&s_state_lock);
            ESP_LOGI(TAG, "Mouse notifications %s", subscribed ? "enabled" : "disabled");
        }
        else if ((param->write.handle == s_battery_handles[BATTERY_LEVEL_CCC]) &&
                 (param->write.len == 2U))
        {
            s_peer.battery_notify = (param->write.value[0] == 1U) &&
                                   (param->write.value[1] == 0U);
            s_peer.battery_written = true;
            save_subscriptions();
        }
        else if ((param->write.handle == s_hid_handles[HID_CONTROL_VALUE]) &&
                 (param->write.len == 1U) && (param->write.value[0] <= 1U))
        {
            portENTER_CRITICAL(&s_state_lock);
            s_connection.suspended = (param->write.value[0] == 0U);
            portEXIT_CRITICAL(&s_state_lock);
            ESP_LOGI(TAG, "HID control: %s",
                (param->write.value[0] == 0U) ? "suspended" : "resumed");
        }
        else
        {
            /* Other writes cannot change application state. */
        }
    }
}

/** Save only a table with the expected shape, then request its service start. */
static void handle_attribute_table(const esp_ble_gatts_cb_param_t * const param)
{
    esp_err_t result = ESP_FAIL;
    if (param->add_attr_tab.status == ESP_GATT_OK)
    {
        if ((param->add_attr_tab.svc_inst_id == HID_SERVICE_INSTANCE) &&
            (param->add_attr_tab.num_handle == (uint16_t)HID_ATTRIBUTE_COUNT))
        {
            (void)memcpy(s_hid_handles, param->add_attr_tab.handles, sizeof(s_hid_handles));
            result = esp_ble_gatts_start_service(s_hid_handles[HID_SERVICE]);
        }
        else if ((param->add_attr_tab.svc_inst_id == INFO_SERVICE_INSTANCE) &&
                 (param->add_attr_tab.num_handle == (uint16_t)INFO_ATTRIBUTE_COUNT))
        {
            (void)memcpy(s_info_handles, param->add_attr_tab.handles, sizeof(s_info_handles));
            result = esp_ble_gatts_start_service(s_info_handles[INFO_SERVICE]);
        }
        else if ((param->add_attr_tab.svc_inst_id == BATTERY_SERVICE_INSTANCE) &&
                 (param->add_attr_tab.num_handle == (uint16_t)BATTERY_ATTRIBUTE_COUNT))
        {
            (void)memcpy(s_battery_handles, param->add_attr_tab.handles, sizeof(s_battery_handles));
            result = esp_ble_gatts_start_service(s_battery_handles[BATTERY_SERVICE]);
        }
        else
        {
            /* Unexpected service table shape is an initialization failure. */
        }
    }
    if (result != ESP_OK)
    {
        complete_initialization(result, 0U);
    }
}

/**
 * Drive GATT registration/service startup and maintain per-link state.
 * Attribute creation is not readiness: INIT_* is set only after service start.
 */
static void gatts_callback(esp_gatts_cb_event_t event, esp_gatt_if_t interface,
                           esp_ble_gatts_cb_param_t *param)
{
    if (param != NULL)
    {
        switch (event)
        {
            case ESP_GATTS_REG_EVT:
                if (param->reg.status == ESP_GATT_OK)
                {
                    s_interface = interface;
                    esp_err_t result = esp_ble_gatts_create_attr_tab(s_hid_database,
                        interface, (uint16_t)HID_ATTRIBUTE_COUNT, HID_SERVICE_INSTANCE);
                    if (result == ESP_OK)
                    {
                        result = esp_ble_gatts_create_attr_tab(s_info_database,
                            interface, (uint16_t)INFO_ATTRIBUTE_COUNT, INFO_SERVICE_INSTANCE);
                    }
                    if (result == ESP_OK)
                    {
                        result = esp_ble_gatts_create_attr_tab(s_battery_database,
                            interface, (uint16_t)BATTERY_ATTRIBUTE_COUNT, BATTERY_SERVICE_INSTANCE);
                    }
                    if (result != ESP_OK)
                    {
                        complete_initialization(result, 0U);
                    }
                }
                else
                {
                    complete_initialization(ESP_FAIL, 0U);
                }
                break;
            case ESP_GATTS_CREAT_ATTR_TAB_EVT:
                handle_attribute_table(param);
                break;
            case ESP_GATTS_START_EVT:
                if (param->start.service_handle == s_hid_handles[HID_SERVICE])
                {
                    complete_initialization((param->start.status == ESP_GATT_OK) ?
                        ESP_OK : ESP_FAIL, INIT_HID_BIT);
                }
                else if (param->start.service_handle == s_info_handles[INFO_SERVICE])
                {
                    complete_initialization((param->start.status == ESP_GATT_OK) ?
                        ESP_OK : ESP_FAIL, INIT_INFO_BIT);
                }
                else if (param->start.service_handle == s_battery_handles[BATTERY_SERVICE])
                {
                    complete_initialization((param->start.status == ESP_GATT_OK) ?
                        ESP_OK : ESP_FAIL, INIT_BATTERY_BIT);
                }
                else
                {
                    complete_initialization(ESP_FAIL, 0U);
                }
                break;
            case ESP_GATTS_CONNECT_EVT:
                handle_connection(param);
                break;
            case ESP_GATTS_DISCONNECT_EVT:
                /* Clear live state, retaining per-bond CCC persistence. The
                 * next authenticated peer restores only its own configuration. */
                portENTER_CRITICAL(&s_state_lock);
                s_connection.connected = false;
                s_connection.encrypted = false;
                s_connection.subscribed = false;
                s_connection.generation++;
                portEXIT_CRITICAL(&s_state_lock);
                (void)memset(&s_peer, 0, sizeof(s_peer));
                apply_ccc_attributes();
                log_sdk_error("Restart advertising", esp_ble_gap_start_advertising(&s_advertising));
                ESP_LOGI(TAG, "Disconnected, reason 0x%02x",
                         (unsigned int)param->disconnect.reason);
                break;
            case ESP_GATTS_MTU_EVT:
                ESP_LOGI(TAG, "GATT MTU: connection=%u bytes=%u",
                    (unsigned int)param->mtu.conn_id, (unsigned int)param->mtu.mtu);
                break;
            case ESP_GATTS_READ_EVT:
                /* AUTO_RSP remains the sole response owner. */
                ESP_LOGI(TAG, "GATT read: connection=%u handle=0x%04x offset=%u",
                    (unsigned int)param->read.conn_id, (unsigned int)param->read.handle,
                    (unsigned int)param->read.offset);
                break;
            case ESP_GATTS_WRITE_EVT:
                handle_write(param);
                break;
            case ESP_GATTS_CONGEST_EVT:
                /* Preserve logical readiness during temporary backpressure;
                 * ble_mouse_send() refuses sends until the SDK clears it. */
                portENTER_CRITICAL(&s_state_lock);
                s_connection.congested = param->congest.congested;
                portEXIT_CRITICAL(&s_state_lock);
                break;
            default:
                break;
        }
    }
}

/**
 * Configure Secure Connections bonding with no keyboard/display capabilities.
 * This is Just Works pairing (no MITM passkey); encrypted-report permissions
 * and the application readiness gate remain separate requirements.
 */
static esp_err_t configure_security(void)
{
    esp_ble_auth_req_t authentication = ESP_LE_AUTH_REQ_SC_BOND;
    esp_ble_io_cap_t capability = ESP_IO_CAP_NONE;
    uint8_t key_size = 16U;
    uint8_t keys = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    esp_err_t result = esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE,
        &authentication, sizeof(authentication));
    if (result == ESP_OK)
    {
        result = esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE,
            &capability, sizeof(capability));
    }
    if (result == ESP_OK)
    {
        result = esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(key_size));
    }
    if (result == ESP_OK)
    {
        result = esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &keys, sizeof(keys));
    }
    if (result == ESP_OK)
    {
        result = esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &keys, sizeof(keys));
    }
    return result;
}

esp_err_t ble_mouse_init(void)
{
    esp_err_t result = ESP_ERR_INVALID_STATE;
    /* The configured name is encoded into the scan response at startup. */
    const size_t name_length = strlen(CONFIG_MOUSE_DEVICE_NAME);
    if ((name_length == 0U) || (name_length > 20U))
    {
        result = ESP_ERR_INVALID_ARG;
    }
    else if (s_init_events == NULL)
    {
        s_init_events = xEventGroupCreateStatic(&s_init_storage);
        result = (s_init_events != NULL) ? ESP_OK : ESP_ERR_NO_MEM;
        /* Controller -> host -> callbacks -> security/data -> application
         * registration. Later steps execute only after the prior one succeeds. */
        if (result == ESP_OK)
        {
            esp_bt_controller_config_t controller = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
            result = esp_bt_controller_init(&controller);
        }
        if (result == ESP_OK)
        {
            result = esp_bt_controller_enable(ESP_BT_MODE_BLE);
        }
        if (result == ESP_OK)
        {
            result = esp_bluedroid_init();
        }
        if (result == ESP_OK)
        {
            result = esp_bluedroid_enable();
        }
        if (result == ESP_OK)
        {
            result = esp_ble_gap_register_callback(gap_callback);
        }
        if (result == ESP_OK)
        {
            result = esp_ble_gatts_register_callback(gatts_callback);
        }
        if (result == ESP_OK)
        {
            result = configure_security();
        }
        if (result == ESP_OK)
        {
            result = esp_ble_gap_set_device_name(CONFIG_MOUSE_DEVICE_NAME);
        }
        if (result == ESP_OK)
        {
            result = esp_ble_gap_config_adv_data_raw(s_advertisement, sizeof(s_advertisement));
        }
        if (result == ESP_OK)
        {
            result = esp_ble_gap_config_adv_data(&s_scan_response);
        }
        if (result == ESP_OK)
        {
            result = esp_ble_gatts_app_register(UINT16_C(0x55));
        }
        if (result == ESP_OK)
        {
            /* Poll event bits with a finite ten-second deadline. Advertising
             * starts only when every service and both advertising payloads are
             * ready; one callback failure aborts this wait. */
            const TickType_t start = xTaskGetTickCount();
            EventBits_t bits = 0U;
            do
            {
                bits = xEventGroupGetBits(s_init_events);
                if (((bits & INIT_REQUIRED_BITS) != INIT_REQUIRED_BITS) &&
                    ((bits & INIT_FAILURE_BIT) == 0U))
                {
                    vTaskDelay(pdMS_TO_TICKS(10U));
                }
            } while (((bits & INIT_REQUIRED_BITS) != INIT_REQUIRED_BITS) &&
                     ((bits & INIT_FAILURE_BIT) == 0U) &&
                     ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(10000U)));
            if ((bits & INIT_FAILURE_BIT) != 0U)
            {
                result = ESP_FAIL;
            }
            else if ((bits & INIT_REQUIRED_BITS) != INIT_REQUIRED_BITS)
            {
                result = ESP_ERR_TIMEOUT;
            }
            else
            {
                ESP_LOGI(TAG, "HID handles: service=0x%04x map=0x%04x input=0x%04x ccc=0x%04x",
                    (unsigned int)s_hid_handles[HID_SERVICE],
                    (unsigned int)s_hid_handles[HID_MAP_VALUE],
                    (unsigned int)s_hid_handles[HID_REPORT_VALUE],
                    (unsigned int)s_hid_handles[HID_REPORT_CCC]);
                result = esp_ble_gap_start_advertising(&s_advertising);
            }
        }
    }
    return result;
}

ble_mouse_status_t ble_mouse_status(void)
{
    ble_mouse_status_t status;
    /* Copy the entire public view under one short lock to avoid mixing fields
     * from two SDK callback updates. Never call the SDK while holding this lock. */
    portENTER_CRITICAL(&s_state_lock);
    status.connected = s_connection.connected;
    status.ready = s_connection.connected && s_connection.encrypted &&
                   s_connection.subscribed && (!s_connection.suspended);
    status.encrypted = s_connection.encrypted;
    status.subscribed = s_connection.subscribed;
    status.suspended = s_connection.suspended;
    status.congested = s_connection.congested;
    status.generation = s_connection.generation;
    portEXIT_CRITICAL(&s_state_lock);
    return status;
}

esp_err_t ble_mouse_send(const mouse_report_t * const report)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if ((report != NULL) && ((report->buttons & (uint8_t)(~MOUSE_BUTTON_MASK)) == 0U))
    {
        connection_t connection;
        portENTER_CRITICAL(&s_state_lock);
        connection = s_connection;
        portEXIT_CRITICAL(&s_state_lock);
        /* Snapshot first, then release the lock before entering the SDK. A
         * disconnect can still race this call, so the SDK result is authoritative. */
        result = ESP_ERR_INVALID_STATE;
        if (connection.connected && connection.encrypted && connection.subscribed &&
            (!connection.suspended) && (!connection.congested))
        {
            /* BLE Report Reference carries the ID; the value has exactly four
             * bytes. Explicit serialization avoids structure-padding dependence.
             * Signed-to-unsigned conversion preserves each relative byte's
             * two's-complement wire representation modulo 256. */
            uint8_t data[4] = {report->buttons, (uint8_t)report->x,
                               (uint8_t)report->y, (uint8_t)report->wheel};
            /* false selects an unacknowledged notification, not an indication.
             * On failure the caller retains the queued button edge for retry. */
            result = esp_ble_gatts_send_indicate(s_interface, connection.connection_id,
                s_hid_handles[HID_REPORT_VALUE], sizeof(data), data, false);
        }
    }
    return result;
}
