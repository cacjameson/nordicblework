#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/adc.h>

#define SAMPLE_RATE 1000       // 1000 Hz sampling rate (1ms per sample)

LOG_MODULE_REGISTER(ADC_Test, LOG_LEVEL_INF);

static const struct adc_channel_cfg adc_cfg = {
    .gain = ADC_GAIN_1_6,
    .reference = ADC_REF_INTERNAL,
    .acquisition_time = ADC_ACQ_TIME_DEFAULT,
    .channel_id = 0,
    .differential = 0,
};

int main(void) {
    int err;
    uint16_t adc_value;

    const struct device *adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc));
    if (!device_is_ready(adc_dev)) {
        LOG_ERR("ADC device not ready");
        return 0;
    }

    err = adc_channel_setup(adc_dev, &adc_cfg);
    if (err < 0) {
        LOG_ERR("Could not set up ADC channel (%d)", err);
        return 0;
    }

    LOG_INF("ADC initialized");

    while (1) {
        struct adc_sequence sequence = {
            .channels = BIT(0),
            .buffer = &adc_value,
            .buffer_size = sizeof(adc_value),
            .resolution = 12,
        };

        err = adc_read(adc_dev, &sequence);
        if (err < 0) {
            LOG_ERR("ADC read error (%d)", err);
        } else {
            LOG_INF("ADC Value: %d", adc_value);
        }

        k_sleep(K_MSEC(1000 / SAMPLE_RATE));
    }

    return 0;
}
