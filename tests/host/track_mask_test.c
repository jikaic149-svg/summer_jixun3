#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "track.h"

static int failures = 0;

#define CHECK(condition, message)                                      \
    do {                                                               \
        if (!(condition)) {                                            \
            printf("FAIL: %s (line %d)\n", (message), __LINE__);      \
            failures++;                                                \
        }                                                              \
    } while (0)

static void send_unused(uint8_t *data, uint16_t len)
{
    (void)data;
    (void)len;
}

static void parse_and_expect(Track_Instance *track,
                             const char *packet,
                             uint8_t expected_mask,
                             uint8_t expected_count,
                             uint32_t expected_frame)
{
    Track_Snapshot_t snapshot;

    Track_ParsePacket(track, (const uint8_t *)packet,
                      (uint16_t)strlen(packet));
    CHECK(Track_GetSnapshot(track, &snapshot) != 0U,
          "snapshot read must succeed");
    CHECK(snapshot.black_mask == expected_mask,
          "x1..x8 must map to black-mask bit0..bit7");
    CHECK(snapshot.black_count == expected_count,
          "black count must be derived from the same mask");
    CHECK(snapshot.frame_count == expected_frame,
          "valid frame must publish exactly once");
}

int main(void)
{
    Track_Instance track;

    Track_Init(&track, send_unused);
    parse_and_expect(
        &track,
        "$A,x1:2783,x2:2857,x3:2718,x4:2773,"
        "x5:993,x6:1051,x7:1326,x8:1079#",
        0x0FU, 4U, 1U);
    parse_and_expect(
        &track,
        "$A,x1:1286,x2:1463,x3:2718,x4:2773,"
        "x5:2681,x6:2738,x7:2836,x8:1079#",
        0x7CU, 5U, 2U);
    parse_and_expect(
        &track,
        "$A,x1:1286,x2:1463,x3:2718,x4:2773,"
        "x5:2681,x6:2738,x7:2836,x8:2657#",
        0xFCU, 6U, 3U);

    if (failures != 0) {
        printf("%d track mask assertion(s) failed\n", failures);
        return 1;
    }
    printf("track mask tests passed\n");
    return 0;
}
