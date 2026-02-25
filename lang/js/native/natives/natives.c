#include "natives.h"
#include "Math/native_Math.h"
#include "Date/native_Date.h"
#include "JSON/native_JSON.h"

#ifdef __cplusplus /* __cplusplus */
extern "C" {
#endif

void reg_natives(vm_t* vm) {
	//reg_native_Math(vm);
	reg_native_Date(vm);
	reg_native_JSON(vm);
}

#ifdef __cplusplus /* __cplusplus */
}
#endif
