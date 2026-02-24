#include "native_builtin.h"
#include "Object/native_Object.h"
#include "Number/native_Number.h"
#include "Array/native_Array.h"
#include "String/native_String.h"
#include "Promise/native_Promise.h"
#include "Math/native_Math.h"
#include "Console/native_Console.h"
#include "JSON/native_JSON.h"
#include "Date/native_Date.h"
#include "Error/native_Error.h"

#ifdef __cplusplus /* __cplusplus */
extern "C" {
#endif

void reg_basic_natives(vm_t* vm) {
	reg_native_Object(vm);
	reg_native_Error(vm);
	reg_native_Array(vm);
	reg_native_String(vm);
	reg_native_Console(vm);
	reg_native_Number(vm);
	reg_native_Promise(vm);
	reg_native_Math(vm);
	reg_native_JSON(vm);
	reg_native_Date(vm);
}

#ifdef __cplusplus /* __cplusplus */
}
#endif
