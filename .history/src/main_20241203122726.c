#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>

LOG_MODULE_REGISTER(PWM_BLE_Control);

/* Define the PWM device and channels */
#define PWM_NODE DT_NODELABEL(pwm0)
#define PWM_CHANNEL_1 0 // Motor 1 on P0.28
#define PWM_CHANNEL_2 1 // Motor 2 on P0.29
#define PWM_FLAGS PWM_POLARITY_INVERTED // Adjust for motor polarity

/* Motor-specific PWM settings */
#define PWM_PERIOD PWM_MSEC(20)         // 20 ms period (50 Hz for most motors)
#define PWM_MIN_PULSE_WIDTH 1000000     // 1 ms for minimum angle/power
#define PWM_MAX_PULSE_WIDTH 2000000     // 2 ms for maximum angle/power

static const struct device *pwm_dev;

/* Function to set motor PWM based on percentage */
static void set_motor(uint8_t motor_num, uint8_t percentage) {
    // Clamp the percentage to a valid range (0 to 100)
    if (percentage > 100) {
        percentage = 100;
    }

    // Calculate the pulse width based on the percentage
    uint32_t pulse_width = PWM_MIN_PULSE_WIDTH +
                           (percentage * (PWM_MAX_PULSE_WIDTH - PWM_MIN_PULSE_WIDTH)) / 100;

    int ret;
    if (motor_num == 1) {
        ret = pwm_set(pwm_dev, PWM_CHANNEL_1, PWM_PERIOD, pulse_width, PWM_FLAGS);
        if (ret) {
            LOG_ERR("Error setting PWM for Motor 1 (P0.28): %d", ret);
        } else {
            LOG_INF("Motor 1 PWM updated: Period = %u, Pulse Width = %u ns", PWM_PERIOD, pulse_width);
        }
    } else if (motor_num == 2) {
        ret = pwm_set(pwm_dev, PWM_CHANNEL_2, PWM_PERIOD, pulse_width, PWM_FLAGS);
        if (ret) {
            LOG_ERR("Error setting PWM for Motor 2 (P0.29): %d", ret);
        } else {
            LOG_INF("Motor 2 PWM updated: Period = %u, Pulse Width = %u ns", PWM_PERIOD, pulse_width);
        }
    }
}

/* BLE Write Handlers */
static ssize_t motor1_write_handler(struct bt_conn *conn,
                                    const struct bt_gatt_attr *attr,
                                    const void *buf, uint16_t len,
                                    uint16_t offset, uint8_t flags)
{
    if (len == 1) {
        uint8_t received_value = *((const uint8_t *)buf);
        LOG_INF("Received value for Motor 1: %d%%", received_value);
        set_motor(1, received_value);
    } else {
        LOG_WRN("Unexpected write length for Motor 1: %d", len);
    }
    return len;
}

static ssize_t motor2_write_handler(struct bt_conn *conn,
                                    const struct bt_gatt_attr *attr,
                                    const void *buf, uint16_t len,
                                    uint16_t offset, uint8_t flags)
{
    if (len == 1) {
        uint8_t received_value = *((const uint8_t *)buf);
        LOG_INF("Received value for Motor 2: %d%%", received_value);
        set_motor(2, received_value);
    } else {
        LOG_WRN("Unexpected write length for Motor 2: %d", len);
    }
    return len;
}

/* GATT Service and Characteristic Definition */
BT_GATT_SERVICE_DEFINE(pwm_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_DECLARE_128(
        BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0))),
    BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(
        BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcde01)),
        BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
        BT_GATT_PERM_WRITE,
        NULL, motor1_write_handler, NULL),
    BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(
        BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcde02)),
        BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
        BT_GATT_PERM_WRITE,
        NULL, motor2_write_handler, NULL),
);

void on_connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        LOG_ERR("Failed to connect (err %d)", err);
    } else {
        LOG_INF("Connected to BLE device");
    }
}

void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("Disconnected from BLE device (reason %d)", reason);
}

static struct bt_conn_cb conn_callbacks = {
    .connected = on_connected,
    .disconnected = on_disconnected,
};

/* Advertising data */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL,
        BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)),
};

int main(void)
{
    int err;

    LOG_INF("Starting PWM BLE Control for Motors");

    /* Initialize PWM Device */
    pwm_dev = DEVICE_DT_GET(PWM_NODE);
    if (!device_is_ready(pwm_dev)) {
        LOG_ERR("PWM device not ready");
        return -1;
    }
    LOG_INF("PWM device initialized");

    /* Initialize Bluetooth */
    err = bt_enable(NULL);
    if (err) {
        LOG_ERR("Bluetooth initialization failed (err %d)", err);
        return -1;
    }
    LOG_INF("Bluetooth initialized");

    /* Register Bluetooth connection callbacks */
    bt_conn_cb_register(&conn_callbacks);

    /* Start Bluetooth advertising */
    err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        LOG_ERR("Advertising failed to start (err %d)", err);
        return -1;
    }
    LOG_INF("Advertising started");

    LOG_INF("BLE service and characteristics initialized");

    /* Main loop */
    while (1) {
        k_sleep(K_FOREVER);
    }

    return 0;
}
