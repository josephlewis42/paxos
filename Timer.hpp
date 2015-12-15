/*
 * Copyright 2014 Joseph Lewis <joseph@josephlewis.net>
 *
 * This file is part of University of Denver Autopilot.
 * Dual licensed under the GPL v 3 and the Apache 2.0 License
 */


#ifndef TIMER_HPP
#define TIMER_HPP

#include <chrono>
#include <mutex>

/** Provides a basic timing mechanism for various classes, as well as
an alarm feature.

**/
class Timer
{
    private:
        /// Locks the start time
        mutable std::mutex start_time_lock;
        /// Keeps the time that the timer was initiated
        std::chrono::time_point<std::chrono::high_resolution_clock> _timerInit;

        /// Tells us if the alarm is set.
        bool _alarmSet;

        /// The time the alarm is set for
        std::chrono::time_point<std::chrono::high_resolution_clock> _alarmTime;

    public:
        Timer()
        :_timerInit(std::chrono::high_resolution_clock::now()),
        _alarmSet(false)
        {}

    /// set the start_time to the current time
    void set_start_time()
    {
        std::lock_guard<std::mutex> lock(start_time_lock);
        _timerInit = std::chrono::high_resolution_clock::now();
    }
    /// get the start_Time
    std::chrono::time_point<std::chrono::high_resolution_clock> get_start_time() const
    {
        std::lock_guard<std::mutex> lock(start_time_lock);
        return _timerInit;
    }

    /// Get the number of milliseconds since this timer was started or the set_start_time was called
    long getMsSinceInit() const
    {
        std::lock_guard<std::mutex> lock(start_time_lock);
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - _timerInit).count();
    }

    /// Acts like a stopwatch button, reutrns the number of milliseconds since the last "click" and inits the timer again
    long click()
    {
        std::lock_guard<std::mutex> lock(start_time_lock);
        auto now = std::chrono::high_resolution_clock::now();
        long ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - _timerInit).count();
        _timerInit = now;
        return ms;
    }

    /// Sets an alarm for a given number of ms in the future.
    void setAlarm(long msInFuture)
    {
        _alarmTime = std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(msInFuture);
        _alarmSet = true;
    }

    /// Returns true if the alarm is past due, false otherwise.
    bool alarmIsRinging()
    {
        if(! _alarmSet)
            return false;

        if(std::chrono::high_resolution_clock::now() >= _alarmTime)
            return true;

        return false;
    }

    /// Unsets the alarm.
    void stopAlarm()
    {
        _alarmSet = false;
    }

    /// Returns if the alarm is set or not.
    bool alarmSet()
    {
        return _alarmSet;
    }

    /// Gets the number of ms until the alarm rings, will be negative if the alarm is ringinging
    /// behavior is undefined if the alarm is not set.
    long getMsUntilAlarm()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(_alarmTime - std::chrono::high_resolution_clock::now()).count();
    }
};

#endif //TIMER_HPP
