#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>

#define RING_BUF_SIZE 16

// Device States
typedef enum {
    SYSTEM_STATE_SLEEP,
    SYSTEM_STATE_SAMPLING,
    SYSTEM_STATE_BLE_SYNC
} SystemState_t;

// Ring Buffer Data Structure
typedef struct {
    uint32_t bpm_data[RING_BUF_SIZE];
    uint8_t head;
    uint8_t tail;
    pthread_mutex_t lock; // Protects against concurrent access
} SensorBuffer_t;

// Global System Shared Objects
SensorBuffer_t g_sensor_buffer;
SystemState_t g_current_state = SYSTEM_STATE_SLEEP;

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
    while (1) {
        // Simulate heart rate readings between 60 BPM and 120 BPM
        uint32_t simulated_bpm = 60 + (rand() % 61);
        buffer_push(&g_sensor_buffer, simulated_bpm);
        
        g_current_state = SYSTEM_STATE_SAMPLING;
        usleep(100000); // 100 milliseconds
    }
    return NULL;
}

// TASK 2: High-Priority Processing & BLE Sync Task
void* task_ble_sync(void* arg) {
    uint32_t sample = 0;
    uint32_t sum = 0;
    uint32_t count = 0;

    while (1) {
        usleep(1000000); // 1 Second Sleep Window
        
        g_current_state = SYSTEM_STATE_BLE_SYNC;
        printf("\n--- [BLE SYNC TASK WAKEUP] State: BLE_SYNC ---\n");

        sum = 0;
        count = 0;
        while (buffer_pop(&g_sensor_buffer, &sample)) {
            sum += sample;
            count++;
        }

        if (count > 0) {
            uint32_t avg_bpm = sum / count;
            printf("[BLE LOG] Transmitting Packet over BLE: %u Samples processed. Avg HR = %u BPM\n", count, avg_bpm);
            if (avg_bpm > 100) {
                printf("[ALERT] High Heart Rate Detected: %u BPM!\n", avg_bpm);
            }
            else{
                printf("[BLE LOG] Consistent Heart Rate Detected: %u BPM\n", avg_bpm);
            }
        } else {
            printf("[BLE LOG] No new samples found in Ring Buffer.\n");
        }

        g_current_state = SYSTEM_STATE_SLEEP;
        printf("--- [SYSTEM ENTERING LOW POWER SLEEP] ---\n");
    }
    return NULL;
}

int main(void) {
    pthread_t thread_sensor, thread_ble;

    // Initialize mutex
    pthread_mutex_init(&g_sensor_buffer.lock, NULL);
    g_sensor_buffer.head = 0;
    g_sensor_buffer.tail = 0;

    printf("Booting Virtual Wearable Firmware Target...\n");

    // Spawn Firmware Tasks
    pthread_create(&thread_sensor, NULL, task_sensor_isr, NULL);
    pthread_create(&thread_ble, NULL, task_ble_sync, NULL);

    // Keep running main thread
    pthread_join(thread_sensor, NULL);
    pthread_join(thread_ble, NULL);

    pthread_mutex_destroy(&g_sensor_buffer.lock);
    return 0;
}