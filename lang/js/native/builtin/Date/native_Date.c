#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "mario.h"
#include <time.h>


/*=====date native functions=========*/
var_t* native_date_now(vm_t* vm, var_t* env, void* data) {
    (void)vm; (void)env; (void)data;
    
    // Get current time in milliseconds since epoch
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long long timestamp = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
    
    return var_new_int(vm, (int)timestamp);
}

var_t* native_date_get_time(vm_t* vm, var_t* env, void* data) {
    (void)vm; (void)env; (void)data;
    
    // Same as now() for static call
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long long timestamp = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
    
    return var_new_int(vm, (int)timestamp);
}

#define CLS_DATE "Date"
void reg_native_Date(vm_t* vm) {
    var_t* cls = vm_new_class(vm, CLS_DATE);
    vm_reg_static(vm, cls, "now()", native_date_now, NULL);
    vm_reg_static(vm, cls, "getTime()", native_date_get_time, NULL);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */