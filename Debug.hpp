/**
 * Copyright 2014 - Joseph Lewis III <joseph@josephlewis.net>
 * All Rights Reserved
 *
 *
 * A simple printf replacement for debugging purposes.
**/

#ifndef DEBUG_HPP
#define DEBUG_HPP

#define LOG(level, message) log(level, "%s (%s:%s %d)\n", message, __FILE__, __FUNCTION__, __LINE__ )

#include <cstdarg>
#include <stdio.h>

enum LOGLEVEL {
    TRACE,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    OFF
};


void setLoggingLevel(LOGLEVEL lvl);
int log(LOGLEVEL lev, const char* format, ...);

#endif
