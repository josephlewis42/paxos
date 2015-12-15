#include "Debug.hpp"

int logging_level = DEBUG;

int log(LOGLEVEL lev, const char* format, ...)
{
    if(lev < logging_level || lev == OFF)
        return 0;

    switch(lev)
    {
    case WARN:
        printf("\x1b[33;1m");
        break;
    case ERROR:
        printf("\x1b[31;1m");
        break;
    case DEBUG:
        printf("\x1b[37m");
        break;
    case INFO:
        printf("\x1b[36;1m");
        break;
    default:
        break;
    }

    switch(lev)
    {
        case TRACE:
            printf("TRACE: ");
            break;
        case DEBUG:
            printf("DEBUG: ");
            break;
        case INFO:
            printf("INFO:  ");
            break;
        case WARN:
            printf("WARN:  ");
            break;
        case ERROR:
            printf("ERROR: ");
            break;
        default:
            break;
    }

    va_list args;
    va_start(args, format);
    int ret = vprintf(format, args);
    va_end(args);

    printf("\x1b[0m");

    return ret;
}


void setLoggingLevel(LOGLEVEL lvl)
{
    logging_level = lvl;
}
