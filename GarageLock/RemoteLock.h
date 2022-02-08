#ifndef RemoteLock_h
#define RemoteLock_h
#include "Arduino.h" 
#include "MCP23S08.h"

// 0 - unlocked
// 1 - locked
// 2 - unlocking
// 3 - awaiting open

#define R_LOCKING 0
#define R_LOCKED 1
#define R_UNLOCKING 2
#define R_READYTOOPEN 3
#define R_UNLOCKED 4

#define A_STOP 1
#define A_UNLOCKING 2
#define A_LOCKING 3

class Pin {
    public :
        virtual void setDirection(int direction) = 0;
        virtual void write(bool value) = 0;
        virtual bool read() = 0;
        virtual void toggle() = 0;
        bool getWrittenState();
    public :
        bool _written;
};

class LocalPin : public Pin {
    public:
        LocalPin(int pin, bool flip);
        void setDirection(int direction);
        void write(bool value);
        bool read();
        void toggle();
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
        void toggle();
    private:
        int _pin;
        bool _flip;
        MCP23S08 *_spi;
};

class RemoteLock {
    public:
        RemoteLock(Pin *close, Pin *locklimit, Pin *unlocklimit, Pin *actuator1, Pin *actuator2, Pin *relay1, Pin *relay2);
        void init();
        void poll();
        void unlock();
        void trylock();
        bool isLocked();
        Pin* closePin();
        Pin* unlockLimitPin();
        Pin* lockLimitPin();
        Pin* relay1Pin();
        Pin* act1Pin();
        Pin* relay2Pin();
        Pin* act2Pin();
    private:
        RemoteLock();
        int _state;
        Pin  *_close, *_unlocklimit, *_locklimit, *_act1, *_act2, *_relay1, *_relay2;
        void _unlock();
        void _lock();
        void _stop();
        bool _lockedLimitState();
        bool _unlockedLimitState();
        bool _closeState();
        int _action;
};
#endif