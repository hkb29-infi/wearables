#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#define RING_BUF_SIZE 16    // Ring buffer size
#define ALPHA_EMA 0.3f       // Low-pass filter smoothing coefficient
#define SPIKE_THRESHOLD 110  // Elevated HR threshold
#define IMPACT_DELTA 25      // Sudden jump threshold indicating potential fall/anomaly

// Device States
typedef enum {
    SYSTEM_STATE_SLEEP,      // Sleep state
    SYSTEM_STATE_SAMPLING,   // Sampling state
    SYSTEM_STATE_BLE_SYNC    // BLE sync state
} SystemState_t;

// Ring Buffer Data Structure
typedef struct {
    uint32_t bpm_data[RING_BUF_SIZE];
    uint8_t head;
    uint8_t tail;
    pthread_mutex_t lock; // Protects against concurrent access
    pthread_cond_t alert_signal; // Alert signal for high heart rate
} SensorBuffer_t;

// Global System Shared Objects
SensorBuffer_t g_sensor_buffer;
SystemState_t g_current_state = SYSTEM_STATE_SLEEP;
bool g_emergency_flag = false;

// Buffer Push Function
void buffer_push(SensorBuffer_t *buf, uint32_t val) {
    pthread_mutex_lock(&buf->lock);
    buf->bpm_data[buf->head] = val;
    buf->head = (buf->head + 1) % RING_BUF_SIZE;
    pthread_mutex_unlock(&buf->lock);
}

// Buffer Pop Function
bool buffer_pop(SensorBuffer_t *buf, uint32_t *out_val) {
    bool success = false;
    pthread_mutex_lock(&buf->lock);
    if (buf->head != buf->tail) {
        *out_val = buf->bpm_data[buf->tail];
        buf->tail = (buf->tail + 1) % RING_BUF_SIZE;
        success = true;
    }
    pthread_mutex_unlock(&buf->lock);
    return success;
}

// TASK 1: Simulated Hardware Interrupt Routine (100ms Heart Rate Sampling)
void* task_sensor_isr(void* arg) {
    float prev_ema_val = 70.0f;
    uint32_t prev_filtered_val = 70;
    uint32_t time_counter = 0;

    while (1) {
        time_counter += 100;

        // 1. Generate raw reading with an artificial anomaly spike every 2.5 seconds
        uint32_t simulated_bpm = 70 + (rand() % 15);
        if (time_counter % 2500 == 0) {
            simulated_bpm = 135; // Artificial anomaly spike
        }

        // 2. Apply DSP Low-Pass Filter
        uint32_t filtered_bpm = apply_ema_filter(simulated_bpm, &prev_ema_val);
        buffer_push(&g_sensor_buffer, filtered_bpm);
        g_current_state = SYSTEM_STATE_SAMPLING;

        printf("[SENSOR ISR] t=%4ums | Raw: %3u BPM | DSP Filtered: %3u BPM\n", 
                time_counter, simulated_bpm, filtered_bpm);

        // 3. Dynamic Peak & Delta Anomaly Detection
        uint32_t delta = (filtered_bpm > prev_filtered_val) ? 
                         (filtered_bpm - prev_filtered_val) : 
                         (prev_filtered_val - filtered_bpm);

        if (filtered_bpm > SPIKE_THRESHOLD || delta > IMPACT_DELTA) {
            pthread_mutex_lock(&g_sensor_buffer.lock);
            g_emergency_flag = true;
            printf("   >>> [HARDWARE INTERRUPT] Anomaly Spike Triggered! Firing GPIO Signal <<<\n");
            pthread_cond_signal(&g_sensor_buffer.alert_signal); // Instant wake-up signal
            pthread_mutex_unlock(&g_sensor_buffer.lock);
        }

        prev_filtered_val = filtered_bpm;
        usleep(100000); // 100ms sample tick
    }
    return NULL;
}

// TASK 2: High-Priority Processing & BLE Sync Task
void* task_ble_sync(void* arg) {
    uint32_t sample = 0;
    uint32_t sum = 0;
    uint32_t count = 0;

    while (1) {
        pthread_mutex_lock(&g_sensor_buffer.lock);
        
        // Setup 1-second timeout for periodic wake-up
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;

        // Sleep until 1 second passes OR condition signal wakes it up early
        pthread_cond_timedwait(&g_sensor_buffer.alert_signal, &g_sensor_buffer.lock, &ts);
        
        bool is_emergency = g_emergency_flag;
        g_emergency_flag = false; // Reset emergency state
        pthread_mutex_unlock(&g_sensor_buffer.lock);

        g_current_state = SYSTEM_STATE_BLE_SYNC;
        
        if (is_emergency) {
            printf("\n!!! [BLE SYNC WAKEUP: HIGH-PRIORITY EMERGENCY INTERRUPT] !!!\n");
        } else {
            printf("\n--- [BLE SYNC WAKEUP: ROUTINE PERIODIC SYNC] ---\n");
        }

        sum = 0;
        count = 0;
        while (buffer_pop(&g_sensor_buffer, &sample)) {
            sum += sample;
            count++;
        }

        if (count > 0) {
            uint32_t avg_bpm = sum / count;
            printf("[BLE LOG] Processed %u samples. Batch Avg = %u BPM\n", count, avg_bpm);
            if (is_emergency) {
                printf("[ALERT TRANSMITTED] Emergency Packet Sent to App over BLE!\n");
            }
        }

        g_current_state = SYSTEM_STATE_SLEEP;
        printf("--- [SYSTEM ENTERING LOW POWER SLEEP] ---\n");
    }
    return NULL;
}

// DSP Filter: Exponential Moving Average (EMA) Low-Pass Filter
uint32_t apply_ema_filter(uint32_t raw_sample, float *prev_ema) {
    *prev_ema = (ALPHA_EMA * (float)raw_sample) + ((1.0f - ALPHA_EMA) * (*prev_ema));
    return (uint32_t)(*prev_ema);
}

int main(void) {
    pthread_t thread_sensor, thread_ble;

    // Initialize mutex and condition variable
    pthread_mutex_init(&g_sensor_buffer.lock, NULL);
    pthread_cond_init(&g_sensor_buffer.alert_signal, NULL);
    g_sensor_buffer.head = 0;
    g_sensor_buffer.tail = 0;

    printf("Booting Virtual Wearable Firmware Target with DSP & Condition Signaling...\n");

    // Spawn Firmware Tasks
    pthread_create(&thread_sensor, NULL, task_sensor_isr, NULL);
    pthread_create(&thread_ble, NULL, task_ble_sync, NULL);

    // Keep running main thread
    pthread_join(thread_sensor, NULL);
    pthread_join(thread_ble, NULL);

    pthread_cond_destroy(&g_sensor_buffer.alert_signal);
    pthread_mutex_destroy(&g_sensor_buffer.lock);
    return 0;
}