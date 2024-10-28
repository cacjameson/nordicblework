#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

// Define necessary macros
#define SAMPLE_RATE 1000       // 1000 Hz sampling rate (1ms per sample)
#define PEAK_THRESHOLD 2000    // Adjust based on your sensor's signal
#define MIN_PEAK_DISTANCE 600  // Minimum time between peaks (600 ms, ~100 BPM max)

// Define Bluetooth UUIDs if not already defined
#ifndef BT_UUID_HEART_RATE_SERVICE
#define BT_UUID_HEART_RATE_SERVICE BT_UUID_DECLARE_16(0x180D)
#endif

#ifndef BT_UUID_HEART_RATE_MEASUREMENT
#define BT_UUID_HEART_RATE_MEASUREMENT BT_UUID_DECLARE_16(0x2A37)
#endif

LOG_MODULE_REGISTER(HeartRateMonitor, LOG_LEVEL_INF);

// ADC configuration structure
static const struct adc_channel_cfg adc_cfg = {
	.gain = ADC_GAIN_1_6,
	.reference = ADC_REF_INTERNAL,
	.acquisition_time = ADC_ACQ_TIME_DEFAULT,
	.channel_id = 0,          // Matches channel@0 in the overlay
	.differential = 0,
};

// GATT Heart Rate Service
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

// Function to send heart rate notifications
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
	uint32_t bpm = 0;              // To store calculated BPM

	/* Initialize ADC */
	const struct device *adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc));
	if (!device_is_ready(adc_dev)) {
		LOG_ERR("ADC device not ready");
		return 0;
	}

	err = adc_channel_setup(adc_dev, 0, &adc_cfg);
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
		struct adc_sequence sequence = {
			.channels = BIT(0),  // Corresponds to channel_id = 0
			.buffer = &adc_value,
			.buffer_size = sizeof(adc_value),
			.resolution = 12,
		};

		err = adc_read(adc_dev, &sequence);
		if (err < 0) {
			LOG_ERR("ADC read error (%d)", err);
			k_sleep(K_MSEC(1000 / SAMPLE_RATE));
			continue;
		}

		LOG_INF("ADC Value: %d", adc_value);  // Additional logging for debugging

		current_time = k_uptime_get_32();  // Get current time in ms

		// Check if the ADC value exceeds the threshold, indicating a peak
		if (adc_value > PEAK_THRESHOLD) {
			if (current_time - last_peak_time > MIN_PEAK_DISTANCE) {
				// A peak is detected, calculate BPM
				uint32_t time_diff = current_time - last_peak_time;  // Time between two peaks
				bpm = (60000 / time_diff);                            // Calculate BPM

				LOG_INF("Heartbeat detected! BPM: %d", bpm);

				/* Send BPM via UART (handled by LOG_INF) */

				/* Send BPM via BLE Notification */
				send_heart_rate_notification((uint8_t)bpm);

				last_peak_time = current_time;  // Update last peak time
			}
		}

		k_sleep(K_MSEC(1000 / SAMPLE_RATE));  // Sleep to achieve the desired sampling rate
	}

	return 0;
}
