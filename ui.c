#include "tweak.h"


int current_detail, cutoff_detail, describe_failures, describe_successes, describe_trivia;


void describe_failure(char *fmt, ...) {
    if(!describe_failures) return;
    if(current_detail > cutoff_detail) return;
    
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}


void describe_success(char *fmt, ...) {
    if(!describe_successes) return;
    if(current_detail > cutoff_detail) return;
    
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}


void describe_trivium(char *fmt, ...) {
    if(!describe_trivia) return;
    if(current_detail > cutoff_detail) return;
    
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}
