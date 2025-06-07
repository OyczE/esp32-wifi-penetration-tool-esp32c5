#ifndef GLOBAL_H
#define GLOBAL_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define MAX_STRINGS 10

//extern portMUX_TYPE dataMutex;

extern char *globalData[MAX_STRINGS];
extern int globalDataCount;
extern int framesPerSecond;

#endif // GLOBAL_H
