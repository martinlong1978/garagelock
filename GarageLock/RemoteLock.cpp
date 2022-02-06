#include <Arduino.h>
#include "RemoteLock.h"

LocalPin::LocalPin(int pin, bool flip)
{
    _pin = pin;
    _flip = flip;
}

void LocalPin::setDirection(int direction)
{
    pinMode(_pin, direction);
};

void LocalPin::write(bool value)
{
    digitalWrite(_pin, value == _flip ? LOW : HIGH);
};

bool LocalPin::read()
{
    int val = digitalRead(_pin);
    // Serial.printf("Reading pin %d value %d\n", _pin, val);
    return (val == HIGH) != _flip;
}

SpiPin::SpiPin(MCP23S08 *spi, int pin, bool flip)
{
    _pin = pin;
    _flip = flip;
    _spi = spi;
}

void SpiPin::setDirection(int direction)
{
    _spi->pinMode(_pin, direction);
};

void SpiPin::write(bool value)
{
    _spi->digitalWrite(_pin, value == _flip ? LOW : HIGH);
};

bool SpiPin::read()
{
    int val = _spi->digitalRead(_pin);
    // Serial.printf("Reading pin %d value %d\n", _pin, val);
    return (val == HIGH) != _flip;
}

RemoteLock::RemoteLock(Pin *close, Pin *limit, Pin *actuator1, Pin *actuator2, Pin *relay)
{
    _close = close;
    _limit = limit;
    _act1 = actuator1;
    _act2 = actuator2;
    _relay = relay;
    close->setDirection(INPUT);
    limit->setDirection(INPUT);
    actuator1->setDirection(OUTPUT);
    actuator2->setDirection(OUTPUT);
    relay->setDirection(OUTPUT);
}

void RemoteLock::poll()
{
    if (_state == R_UNLOCKED && _closeState() && _action != 3)
    {
        Serial.printf("State %d close %d limit %d\n", _state, _closeState(), _limitState());
        Serial.println("// door closed, start locking");
        _lock();
    }
    if (_state == R_UNLOCKED && !_closeState() && _action != 2)
    {
        Serial.printf("State %d close %d limit %d\n", _state, _closeState(), _limitState());
        Serial.println("// reopened before fully locked, wind the lock back");
        _unlock();
    }
    if (_state == R_UNLOCKED && _limitState() && _action != 1)
    {
        Serial.printf("State %d close %d limit %d\n", _state, _closeState(), _limitState());
        Serial.println("// fully locked, stop and set us locked");
        _stop();
        _state = R_LOCKED;
    }
    if (_state == R_UNLOCKING  && _action != 2)
    {
        Serial.printf("State %d close %d limit %d\n", _state, _closeState(), _limitState());
        Serial.println("// unlock requested, start unlock (these states prevent the close switch from relocking)");
        _unlock();
    }
    if (_state == R_UNLOCKING && !_limitState())
    {
        Serial.printf("State %d close %d limit %d\n", _state, _closeState(), _limitState());
        Serial.println("// It's unlocking. Now... wait for the garage door to be opened.");
        _state = R_READYTOOPEN;
    }
    if (_state == R_READYTOOPEN && !_closeState())
    {
        Serial.printf("State %d close %d limit %d\n", _state, _closeState(), _limitState());
        Serial.println("//....  Then a close will relock");
        _state = R_UNLOCKED;
    }
};

bool RemoteLock::_closeState()
{
    // Serial.printf("Closed state %i\n", _close->read());
    return _close->read();
}

bool RemoteLock::_limitState()
{
    // Serial.printf("Limit state %i\n", _limit->read());
    return _limit->read();
}

void RemoteLock::_stop()
{
    if(_action == 1)return;
    _action = 1;
    Serial.println("Stopping");
    _act1->write(false);
    _act2->write(false);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    _relay->write(false);
    Serial.println("sent");
}

void RemoteLock::_unlock()
{
    if(_action == 2)return;
    _action = 2;
    Serial.println("UnLocking");
    _act1->write(false);
    _act2->write(false);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    _relay->write(false);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    _act1->write(true);
    _act2->write(false);
    Serial.println("sent");
}

void RemoteLock::_lock()
{
    if(_action == 3)return;
    _action = 3;
    Serial.println("Locking");
    _act1->write(false);
    _act2->write(false);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    _relay->write(true);
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