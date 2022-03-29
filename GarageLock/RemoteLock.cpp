#include <Arduino.h>
#include "RemoteLock.h"

bool Pin::getWrittenState()
{
    return _written ^ _flip;
}

LocalPin::LocalPin(int pin, bool flip)
{
    _pin = pin;
    _flip = flip;
}

void LocalPin::setDirection(int direction)
{
    _direction = direction;
    pinMode(_pin, direction);
};

void LocalPin::write(bool value)
{
    _written = value;
    digitalWrite(_pin, value == _flip ? LOW : HIGH);
};

bool LocalPin::read()
{
    int val = digitalRead(_pin);
    // Serial.printf("Reading pin %d value %d\n", _pin, val);
    return (val == HIGH) != _flip;
}

void LocalPin::toggle()
{
    _flip = !_flip;
    if(_direction == OUTPUT)
    {
        write(_written);
    }
}

SpiPin::SpiPin(MCP23S08 *spi, int pin, bool flip)
{
    _pin = pin;
    _flip = flip;
    _spi = spi;
}

void SpiPin::setDirection(int direction)
{
    _direction = direction;
    _spi->pinMode(_pin, direction);
};

void SpiPin::write(bool value)
{
    _written = value;
    _spi->digitalWrite(_pin, value == _flip ? LOW : HIGH);
};

bool SpiPin::read()
{
    int val = _spi->digitalRead(_pin);
    // Serial.printf("Reading pin %d value %d\n", _pin, val);
    return (val == HIGH) != _flip;
}

void SpiPin::toggle()
{
    _flip = !_flip;
    if(_direction == OUTPUT)
    {
        write(_written);
    }
}

RemoteLock::RemoteLock(Pin *close, Pin *locklimit, Pin *unlocklimit, Pin *actuator1, Pin *actuator2, Pin *relay1, Pin *relay2)
{
    _close = close;
    _locklimit = locklimit;
    _unlocklimit = unlocklimit;
    _act1 = actuator1;
    _act2 = actuator2;
    _relay1 = relay1;
    _relay2 = relay2;
}

void RemoteLock::init()
{
    _close->setDirection(INPUT);
    _locklimit->setDirection(INPUT);
    _unlocklimit->setDirection(INPUT);
    _act1->setDirection(OUTPUT);
    _act2->setDirection(OUTPUT);
    _relay1->setDirection(OUTPUT);
    _relay2->setDirection(OUTPUT);
}

void RemoteLock::poll()
{
    if (_state == R_UNLOCKED && _closeState())
    {
        Serial.printf("State %d close %d limit %d ", _state, _closeState(), _lockedLimitState());
        Serial.println("// door closed, start locking");
        _lock();
        _state = R_LOCKING;
    }
    if (_state == R_LOCKING && !_closeState())
    {
        Serial.printf("State %d close %d limit %d ", _state, _closeState(), _lockedLimitState());
        Serial.println("// reopened before fully locked, wind the lock back");
        _state = R_UNLOCKING;
    }
    if (_state == R_LOCKING && _lockedLimitState())
    {
        Serial.printf("State %d close %d limit %d ", _state, _closeState(), _lockedLimitState());
        Serial.println("// fully locked, stop and set us locked");
        _stop();
        _state = R_LOCKED;
    }
    if (_state == R_UNLOCKING && _action != A_UNLOCKING)
    {
        // these states prevent the close switch from relocking
        Serial.printf("State %d close %d limit %d ", _state, _closeState(), _lockedLimitState());
        Serial.println("// unlock requested, start unlock");
        _unlock();
    }
    if (_state == R_UNLOCKING && _unlockedLimitState())
    {
        Serial.printf("State %d close %d limit %d ", _state, _closeState(), _lockedLimitState());
        Serial.println("// It's unlocking. Now... wait for the garage door to be opened.");
        _stop();
        _state = R_READYTOOPEN;
    }
    if (_state == R_READYTOOPEN && !_closeState())
    {
        Serial.printf("State %d close %d limit %d ", _state, _closeState(), _lockedLimitState());
        Serial.println("//It's open.  Then a close will relock");
        _state = R_UNLOCKED;
    }
};

Pin *RemoteLock::closePin()
{
    return _close;
}

Pin *RemoteLock::lockLimitPin()
{
    return _locklimit;
}

Pin *RemoteLock::unlockLimitPin()
{
    return _unlocklimit;
}

Pin *RemoteLock::relay1Pin()
{
    return _relay1;
}

Pin *RemoteLock::relay2Pin()
{
    return _relay2;
}

Pin *RemoteLock::act1Pin()
{
    return _act1;
}

Pin *RemoteLock::act2Pin()
{
    return _act2;
}

bool RemoteLock::_closeState()
{
    // Serial.printf("Closed state %i\n", _close->read());
    return _close->read();
}

bool RemoteLock::_lockedLimitState()
{
    // Serial.printf("Limit state %i\n", _limit->read());
    return _locklimit->read();
}

bool RemoteLock::_unlockedLimitState()
{
    // Serial.printf("Limit state %i\n", _limit->read());
    return _unlocklimit->read();
}

void RemoteLock::_stop()
{
    if (_action == A_STOP)
        return;
    _action = A_STOP;
    Serial.println("Stopping");
    _act1->write(false);
    _act2->write(false);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    _relay1->write(false);
    _relay2->write(false);
    Serial.println("sent");
}

void RemoteLock::_unlock()
{
    if (_action == A_UNLOCKING)
        return;
    _action = 2;
    Serial.println("UnLocking");
    _act1->write(false);
    _act2->write(false);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    _relay1->write(false);
    _relay2->write(true);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    _act1->write(true);
    _act2->write(false);
    Serial.println("sent");
}

void RemoteLock::_lock()
{
    if (_action == A_LOCKING)
        return;
    _action = A_LOCKING;
    Serial.println("Locking");
    _act1->write(false);
    _act2->write(false);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    _relay1->write(true);
    _relay2->write(false);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    _act1->write(false);
    _act2->write(true);
    Serial.println("sent");
}

void RemoteLock::unlock()
{
    Serial.println("Setting unlocked");
    _state = R_UNLOCKING;
}

void RemoteLock::trylock()
{
    Serial.println("Trying relock");

    if (_state == R_UNLOCKING || _state == R_READYTOOPEN)
        _state = R_LOCKED;
}

bool RemoteLock::isLocked()
{
    return _state == R_LOCKED;
}