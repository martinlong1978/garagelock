#ifndef RemoteLock_h
#define RemoteLock_h
#include "Arduino.h" 
#include "MCP23S08.h"

// 0 - unlocked
// 1 - locked
// 2 - unlocking
// 3 - awaiting open

#define R_UNLOCKED 0
#define R_LOCKED 1
#define R_UNLOCKING 2
#define R_READYTOOPEN 3

class Pin {
    public :
        virtual void setDirection(int direction) = 0;
        virtual void write(bool value) = 0;
        virtual bool read() = 0;
};

class LocalPin : public Pin {
    public:
        LocalPin(int pin, bool flip);
        void setDirection(int direction);
        void write(bool value);
        bool read();
    private:
        int _pin;
        bool _flip;
};

class SpiPin : public Pin {
    public:
        SpiPin(MCP23S08 *spi, int pin, bool flip);
        void setDirection(int direction);
        void write(bool value);
        bool read();
    private:
        int _pin;
        bool _flip;
        MCP23S08 *_spi;
};

class RemoteLock {
    public:
        RemoteLock(Pin *close, Pin *limit, Pin *actuator1, Pin *actuator2);
        void poll();
        void unlock();
        void trylock();
        bool isLocked();
    private:
        RemoteLock();
        int _state;
        Pin  *_close, *_limit, *_act1, *_act2;
        void _unlock();
        void _lock();
        void _stop();
        bool _limitState();
        bool _closeState();
};
#endif