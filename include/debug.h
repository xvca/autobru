#ifndef DEBUG_H
#define DEBUG_H

#ifdef DEBUG_BUILD
#define DEBUG_PRINT(x) Serial.println(x)
#define DEBUG_PRINTF(format, ...) Serial.printf(format, ##__VA_ARGS__)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTF(x, ...)
#endif

#endif // DEBUG_H