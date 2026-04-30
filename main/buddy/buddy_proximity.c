#include "buddy_proximity.h"
#include <string.h>

static int8_t s_samples[BUDDY_RSSI_SAMPLES] = {-85, -85, -85, -85, -85};
static uint8_t s_sample_idx = 0;
static uint8_t s_sample_count = 0;

/* Quick median of up to 5 int8_t values (insertion sort, fixed size) */
static int8_t median5(int8_t *a, int n)
{
    for (int i = 1; i < n; i++) {
        int8_t key = a[i];
        int j = i - 1;
        while (j >= 0 && a[j] > key) {
            a[j + 1] = a[j];
            j--;
        }
        a[j + 1] = key;
    }
    return a[n / 2];
}

void buddy_proximity_feed(int8_t rssi)
{
    s_samples[s_sample_idx % BUDDY_RSSI_SAMPLES] = rssi;
    s_sample_idx++;
    if (s_sample_count < BUDDY_RSSI_SAMPLES) s_sample_count++;
}

int8_t buddy_proximity_rssi(void)
{
    if (s_sample_count == 0) return BUDDY_PROX_FAR;
    int8_t sorted[5];
    memcpy(sorted, s_samples, sizeof(sorted));
    return median5(sorted, s_sample_count);
}

buddy_proximity_t buddy_proximity_classify(void)
{
    int8_t rssi = buddy_proximity_rssi();
    if (rssi >= BUDDY_PROXIMITY_NEAR) return BUDDY_PROX_NEAR;
    if (rssi >= BUDDY_PROXIMITY_MID)  return BUDDY_PROX_MID;
    if (rssi >= BUDDY_PROXIMITY_FAR)  return BUDDY_PROX_FAR;
    return BUDDY_PROX_UNKNOWN;
}

const char *buddy_proximity_str(buddy_proximity_t p)
{
    switch (p) {
    case BUDDY_PROX_NEAR: return "NEAR";
    case BUDDY_PROX_MID:  return "MID";
    case BUDDY_PROX_FAR:  return "FAR";
    default: return "UNKNOWN";
    }
}

void buddy_proximity_reset(void)
{
    memset(s_samples, (int)(BUDDY_PROXIMITY_FAR), sizeof(s_samples));
    s_sample_idx = 0;
    s_sample_count = 0;
}
