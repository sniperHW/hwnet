#ifndef _TEMP_FAILURE_RETRY_H
#define _TEMP_FAILURE_RETRY_H

#ifndef TEMP_FAILURE_RETRY
#define TEMP_FAILURE_RETRY(expression)                                      \
    ({ long int __result;                                                   \
    do __result = (long int)(expression);                                   \
    while(__result == -1L&& errno == EINTR);                                \
    __result;})
#endif 

#endif