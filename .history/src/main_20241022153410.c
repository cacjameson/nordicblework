#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>

/* Constants for heart rate calculation */
#define SAMPLE_RATE 1000       // 1000 Hz sampling rate (1ms per sample)
#define PEAK_THRESHOLD 2000    // Adjust this based on your sensor's signal
#define MIN_PEAK_DISTANCE 600  // Minimum time between peaks (600 ms, ~100 BPM max)

LOG_MODULE_REGISTER(HeartRateMonitor, LOG_LEVEL_DBG);

/* ADC configuration */
static const struct adc_dt_spec adc_channel = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));

/* Heart Rate GATT Service UUID (Standard) */
#define BT_UUID_HEART_RATE_SERVICE BT_UUID_DECLARE_16(0x180D)

/* Heart Rate Measurement Characteristic UUID (Standard) */
#define BT_UUID_HEART_RATE_MEASUREMENT BT_UUID_DECLARE_16(0x2A37)

/* Heart Rate GATT Service Declaration */
BT_GATT_SERVICE_DEFINE(heart_rate_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_HEART_RATE_SERVICE),

    BT_GATT_CHARACTERISTIC(
        BT_UUID_HEART_RATE_MEASUREMENT,
        BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_NONE,
        NULL, NULL, NULL
    ),

    BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

/* Function to send heart rate notifications */
static void send_heart_rate_notification(uint8_t bpm) {
    uint8_t hrm_value[2];

    /* Heart Rate Measurement Value Format:
       Byte 0: Flags
       Byte 1: Heart Rate Measurement Value
    */
    hrm_value[0] = 0x06;  // Flags: 00000110
                           // Bit 0: Heart Rate Value Format (0 = UINT8)
                           // Bit 1: Sensor Contact Status (1)
                           // Bit 2: Sensor Contact detected
                           // Bits 3-7: Reserved

    hrm_value[1] = bpm;    // BPM value

    /* Send notification */
    int err = bt_gatt_notify(NULL, &heart_rate_svc.attrs[1], hrm_value, sizeof(hrm_value));
    if (err) {
        LOG_ERR("Failed to send heart rate notification (err %d)", err);
    } else {
        LOG_INF("Heart rate notification sent: %d BPM", bpm);
    }
}

int main(void) {
    int err;
    int16_t adc_value;
    uint32_t last_peak_time = 0;  // Timestamp of the last detected peak
    uint32_t current_time;
    uint32_t bpm = 0;  // To store calculated BPM

    /* Define ADC sequence */
    struct adc_sequence sequence = {
        .buffer = &adc_value,
        .buffer_size = sizeof(adc_value),
        .resolution = adc_channel.resolution,
        .oversampling = 0,
        .calibrate = false,
    };

    /* Initialize ADC */
    if (!adc_is_ready_dt(&adc_channel)) {
        LOG_ERR("ADC device not ready.");
        return 0;
    }

    err = adc_channel_setup_dt(&adc_channel);
    if (err < 0) {
        LOG_ERR("Could not set up ADC channel (%d)", err);
        return 0;
    }

    LOG_INF("ADC initialized");

    /* Initialize Bluetooth */
    err = bt_enable(NULL);
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return 0;
    }

    LOG_INF("Bluetooth initialized");

    /* Start Advertising */
    err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, NULL, 0, NULL, 0);
    if (err) {
        LOG_ERR("Advertising failed to start (err %d)", err);
        return 0;
    }

    LOG_INF("Advertising successfully started");

    /* Main Loop */
    while (1) {
        err = adc_read(adc_channel.dev, &sequence);
        if (err < 0) {
            LOG_ERR("ADC read error (%d)", err);
            k_sleep(K_MSEC(1000 / SAMPLE_RATE));
            continue;
        }

        current_time = k_uptime_get_32();  // Get current time in ms

        // Check if the ADC value exceeds the threshold, indicating a peak
        if (adc_value > PEAK_THRESHOLD) {
            if (current_time - last_peak_time > MIN_PEAK_DISTANCE) {
                // A peak is detected, calculate BPM
                uint32_t time_diff = current_time - last_peak_time;  // Time between two peaks
                bpm = (60000 / time_diff);  // Calculate BPM

                LOG_INF("Heartbeat detected! BPM: %d", bpm);

                /* Send BPM via UART (handled by LOG_INF) */

                /* Send BPM via BLE Notification */
                if (bt_conn_le_is_connected(NULL)) {
                    send_heart_rate_notification((uint8_t)bpm);
                } else {
                    LOG_INF("No BLE connection to send notification");
                }

                last_peak_time = current_time;  // Update last peak time
            }
        }

        k_sleep(K_MSEC(1000 / SAMPLE_RATE));  // Sleep to achieve the desired sampling rate
    }

    return 0;
}
