#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vision.h"

static int failures = 0;

#define CHECK(condition, message)                                      \
    do {                                                               \
        if (!(condition)) {                                            \
            printf("FAIL: %s (line %d)\n", (message), __LINE__);      \
            failures++;                                                \
        }                                                              \
    } while (0)

static Vision_ParseResult_t parse(Vision_Instance_t *vision,
                                  const char *text)
{
    return Vision_ParseLine(vision, (const uint8_t *)text,
                            (uint16_t)strlen(text));
}

int main(void)
{
    Vision_Instance_t vision;
    Vision_TaskSnapshot_t snapshot;

    Vision_Init(&vision);
    CHECK(parse(&vision, "12,-8,1") == VISION_PARSE_VALID,
          "legacy vision frame must remain valid");
    (void)Vision_GetTaskSnapshot(&vision, &snapshot);
    CHECK(snapshot.dx == 12 && snapshot.dy == -8 &&
              snapshot.valid == 1U && snapshot.locked == 1U &&
              snapshot.aim_ready == 0U && snapshot.move_epoch == 0U,
          "legacy frame must map to a conservative task snapshot");

    CHECK(parse(&vision, "V,-4,2,1,1,1,4294967295") ==
              VISION_PARSE_VALID,
          "extended task vision frame must parse");
    (void)Vision_GetTaskSnapshot(&vision, &snapshot);
    CHECK(snapshot.frame_count == 2U && snapshot.dx == -4 &&
              snapshot.dy == 2 && snapshot.valid == 1U &&
              snapshot.locked == 1U && snapshot.aim_ready == 1U &&
              snapshot.move_epoch == 4294967295U,
          "extended frame must publish all task fields coherently");

    CHECK(parse(&vision, "V,0,0,1,2,1,3") == VISION_PARSE_INVALID,
          "invalid lock flag must be consumed as vision data");
    CHECK(parse(&vision, "V,0,0,1,1,1,4294967296") ==
              VISION_PARSE_INVALID,
          "overflow move epoch must be rejected");
    CHECK(parse(&vision, "task2") == VISION_PARSE_NOT_VISION,
          "task command must remain outside the vision parser");
    (void)Vision_GetTaskSnapshot(&vision, &snapshot);
    CHECK(snapshot.frame_count == 2U && snapshot.move_epoch == 4294967295U,
          "invalid frames must preserve the last coherent snapshot");

    if (failures != 0) {
        printf("%d vision-protocol assertion(s) failed\n", failures);
        return 1;
    }
    printf("vision protocol tests passed\n");
    return 0;
}
