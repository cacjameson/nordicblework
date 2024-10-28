#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>

/* Constants for ADC */
#define MAX_ADC_VALUE 4095  // Maximum value for 12-bit ADC (2^12 - 1)

static const struct adc_dt_spec adc_channel = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));

LOG_MODULE_REGISTER(PotentiometerMonitor, LOG_LEVEL_DBG);

/* Bluetooth Advertising Data */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
};

/* Custom GATT Service UUID */
static struct bt_uuid_128 pot_service_uuid = BT_UUID_INIT_128(
    0x00000001, 0x0000, 0x1000, 0x8000, 0x00805F9B34FB);

/* Custom GATT Characteristic UUID */
static struct bt_uuid_128 pot_char_uuid = BT_UUID_INIT_128(
    0x00000002, 0x0000, 0x1000, 0x8000, 0x00805F9B34FB);

/* Potentiometer value buffer */
static uint8_t pot_value = 0;

/* GATT callback */
static ssize_t read_pot(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                        void *buf, uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &pot_value, sizeof(pot_value));
}

/* GATT Attributes */
BT_GATT_SERVICE_DEFINE(pot_svc,
    BT_GATT_PRIMARY_SERVICE(&pot_service_uuid),
    BT_GATT_CHARACTERISTIC(&pot_char_uuid.uuid,
                           BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ,
                           read_pot, NULL, &pot_value)
);

/* Callback for Bluetooth connection */
static void connected(struct bt_conn *conn, uint8_t err) {
    if (err) {
        LOG_ERR("Connection failed (err %u)", err);
    } else {
        LOG_INF("Connected");
    }
}

/* Callback for Bluetooth disconnection */
static void disconnected(struct bt_conn *conn, uint8_t reason) {
    LOG_INF("Disconnected (reason %u)", reason);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

/* Function to initialize BLE */
static void bt_ready(void) {
    int err;

    err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        LOG_ERR("Advertising failed to start (err %d)", err);
    } else {
        LOG_INF("Advertising successfully started");
    }
}

int main(void) {
    int err;
    int16_t adc_value;

    struct adc_sequence sequence = {
        .buffer = &adc_value,
        .buffer_size = sizeof(adc_value),
    };

    /* Initialize Bluetooth */
    err = bt_enable(NULL);
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return 0;
    }
    bt_ready();

    if (!adc_is_ready_dt(&adc_channel)) {
        LOG_ERR("ADC device not ready.");
        return 0;
    }

    err = adc_channel_setup_dt(&adc_channel);
    if (err < 0) {
        LOG_ERR("Could not set up ADC channel (%d)", err);
        return 0;
    }

    err = adc_sequence_init_dt(&adc_channel, &sequence);
    if (err < 0) {
        LOG_ERR("Could not initialize ADC sequence.");
        return 0;
    }

    /* Potentiometer value reading loop */
    while (1) {
        err = adc_read(adc_channel.dev, &sequence);
        if (err < 0) {
            LOG_ERR("ADC read error (%d)", err);
            continue;
        }

        /* Convert ADC value to percentage (0 - 100) */
        pot_value = (adc_value * 100) / MAX_ADC_VALUE;
        LOG_INF("Potentiometer Value: %d%%", pot_value);

        /* Sleep for a while before the next read */
        k_sleep(K_MSEC(500));
    }

    return 0;
}
