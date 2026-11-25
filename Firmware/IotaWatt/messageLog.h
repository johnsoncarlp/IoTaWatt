#pragma once
#include <Arduino.h>

class messageLog: public Print {

    public:

        messageLog();

        size_t      write(const uint8_t);
        size_t      write(const uint8_t*, const size_t);
        void        endMsg();

    protected:

        File        msgFile;
        bool        newMsg;
        bool        restart;
        uint8_t*    buf;
        uint8_t     bufLen;
        uint8_t     bufPos;
};

// One statement (safe as the body of an unbraced if). The dead printf() lets the compiler check the
// format against its arguments (-Wformat), which it can't through PSTR(); it generates no code.
#define log(format,...)  do { if(0) printf((format),##__VA_ARGS__); \
                              Message_log.printf_P(PSTR(format),##__VA_ARGS__); Message_log.endMsg(); } while(0)
