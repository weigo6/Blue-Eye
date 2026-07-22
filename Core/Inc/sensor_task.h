#ifndef __SENSOR_TASK_H__
#define __SENSOR_TASK_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef uint8_t SensorTaskEvent_t;

#define SENSOR_TASK_EVENT_NONE             0x00U
#define SENSOR_TASK_EVENT_REQUEST_STARTED  0x01U
#define SENSOR_TASK_EVENT_DATA_UPDATED     0x02U
#define SENSOR_TASK_EVENT_STATUS_CHANGED   0x04U
#define SENSOR_TASK_EVENT_UI_MASK          (SENSOR_TASK_EVENT_DATA_UPDATED | SENSOR_TASK_EVENT_STATUS_CHANGED)

#ifdef __cplusplus
}
#endif

#endif /* __SENSOR_TASK_H__ */
