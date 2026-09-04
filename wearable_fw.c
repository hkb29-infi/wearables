"""
Virtual Wearable Firmware with condition signaling for high heart beat or drastic change in heart rate
"""
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#define RING_BUF_SIZE 16    // size
#define EMA_NUM 0.3       // smoothing data using EMA
//threshold for anomaly detection
#define SPIKE_THRESHOLD 80  
#define IMPACT_DELTA 25     

// states of the device (not used yet)
typedef enum {
    SYSTEM_STATE_SLEEP,      
    SYSTEM_STATE_SAMPLING,  
    SYSTEM_STATE_BLE_SYNC   
} SystemState_t;

// data buffer structure
typedef struct {
    uint32_t bpm_data[RING_BUF_SIZE];
    uint8_t head;
    uint8_t tail;
    pthread_mutex_t lock; // protects against race condition
    pthread_cond_t alert_signal; // emergency alert signal
} SensorBuffer_t;

SensorBuffer_t g_sensor_buffer;
SystemState_t g_current_state = SYSTEM_STATE_SLEEP;
bool g_emergency_flag = false;

// smoothening out the data as per practice
uint32_t apply_ema_filter(uint32_t raw_sample, float *prev_ema) {
    *prev_ema = (EMA_NUM * (float)raw_sample) + ((1.0 - EMA_NUM) * (*prev_ema));
    return (uint32_t)(*prev_ema);
}

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

// Simulating data creation and data processing
void* task_sensor_isr(void* arg) {
    float prev_ema_val = 70.0;
    uint32_t prev_filtered_val = 70;
    uint32_t time_counter = 0;

    while (1) {
        time_counter += 100;

        // make an artificial spike every 2.5 seconds
        uint32_t simulated_bpm = 70 + (rand() % 15);
        if (time_counter % 2500 == 0) {
            simulated_bpm = 135; // Artificial anomaly spike
        }

        // normalize the readings
        uint32_t filtered_bpm = apply_ema_filter(simulated_bpm, &prev_ema_val);
        buffer_push(&g_sensor_buffer, filtered_bpm);
        g_current_state = SYSTEM_STATE_SAMPLING;

        printf("[Sensor Detected heart beat] t=%u | Raw: %u BPM | Refined: %u BPM\n", 
                time_counter, simulated_bpm, filtered_bpm);

        // get teh delta and then comapre the thresholds
        uint32_t delta = (filtered_bpm > prev_filtered_val) ? 
                         (filtered_bpm - prev_filtered_val) : 
                         (prev_filtered_val - filtered_bpm);

        if (filtered_bpm > SPIKE_THRESHOLD || delta > IMPACT_DELTA) {
            pthread_mutex_lock(&g_sensor_buffer.lock);
            g_emergency_flag = true;
            printf("Anomaly Spike Triggered!");
            pthread_cond_signal(&g_sensor_buffer.alert_signal); // alert the data consumer
            pthread_mutex_unlock(&g_sensor_buffer.lock);
        }

        prev_filtered_val = filtered_bpm;
        usleep(100000); // wait 0.1 sec
    }
    return NULL;
}

// the data consumer replicating the BLE sync task
void* task_ble_sync(void* arg) {
    uint32_t sample = 0;
    uint32_t sum = 0;
    uint32_t count = 0;

    while (1) {
        pthread_mutex_lock(&g_sensor_buffer.lock);
        
        // set 1 second timer
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;

        // get up after 1 second passes or emergency condition signal 
        pthread_cond_timedwait(&g_sensor_buffer.alert_signal, &g_sensor_buffer.lock, &ts);
        
        bool is_emergency = g_emergency_flag;
        g_emergency_flag = false; // reset emergency state
        pthread_mutex_unlock(&g_sensor_buffer.lock);

        g_current_state = SYSTEM_STATE_BLE_SYNC;
        
        if (is_emergency) {
            printf("EMERGENCY condition\n");
        } else {
            printf("\n[Normal]\n");
        }

        sum = 0;
        count = 0;
        while (buffer_pop(&g_sensor_buffer, &sample)) {
            sum += sample;
            count++;
        }

        if (count > 0) {
            uint32_t avg = sum / count;
            printf("[Processed %u samples. Avg = %u BPM\n", count, avg);
            if (is_emergency) {
                printf("[Critical data sent to app]\n");
            }
        }

        g_current_state = SYSTEM_STATE_SLEEP;
        printf("entering low power mode\n");
    }
    return NULL;
}


int main(void) {
    pthread_t thread_sensor, thread_ble;

    // initialize
    pthread_mutex_init(&g_sensor_buffer.lock, NULL);
    pthread_cond_init(&g_sensor_buffer.alert_signal, NULL);
    g_sensor_buffer.head = 0;
    g_sensor_buffer.tail = 0;

    printf("Starting Virtual Wearable Firmware\n");

    // create
    pthread_create(&thread_sensor, NULL, task_sensor_isr, NULL);
    pthread_create(&thread_ble, NULL, task_ble_sync, NULL);

    // let main wait
    pthread_join(thread_sensor, NULL);
    pthread_join(thread_ble, NULL);

    pthread_cond_destroy(&g_sensor_buffer.alert_signal);
    pthread_mutex_destroy(&g_sensor_buffer.lock);
    return 0;
}