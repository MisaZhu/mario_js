#include "natives_builtin.h"
#include "Object/native_Object.h"
#include "Function/native_Function.h"
#include "Number/native_Number.h"
#include "BigInt/native_BigInt.h"
#include "ArrayBuffer/native_ArrayBuffer.h"
#include "DataView/native_DataView.h"
#include "TypedArray/native_TypedArray.h"
#include "Array/native_Array.h"
#include "String/native_String.h"
#include "Promise/native_Promise.h"
#include "Error/native_Error.h"
#include "Map/native_Map.h"
#include "Set/native_Set.h"
#include "Symbol/native_Symbol.h"
#include "Proxy/native_Proxy.h"
#include "Reflect/native_Reflect.h"
#include "WeakRef/native_WeakRef.h"
#include "FinalizationRegistry/native_FinalizationRegistry.h"
#include "SharedArrayBuffer/native_SharedArrayBuffer.h"
#include "Atomics/native_Atomics.h"
#include "RegExp/native_RegExp.h"
#include "Math/native_Math.h"
#include "Date/native_Date.h"
#include "JSON/native_JSON.h"
#include "Intl/native_Intl.h"

#ifdef __cplusplus /* __cplusplus */
extern "C" {
#endif

void reg_builtin_natives(vm_t* vm) {
	reg_native_Object(vm);
	reg_native_Function(vm);
	reg_native_Error(vm);
	reg_native_Array(vm);
	reg_native_String(vm);
	reg_native_Number(vm);
	reg_native_BigInt(vm);
	reg_native_ArrayBuffer(vm);
	reg_native_DataView(vm);
	reg_native_TypedArray(vm);
	reg_native_Promise(vm);
	reg_native_Map(vm);
	reg_native_Set(vm);
	reg_native_Symbol(vm);
	reg_native_Proxy(vm);
	reg_native_Reflect(vm);
	reg_native_WeakRef(vm);
	reg_native_FinalizationRegistry(vm);
	reg_native_SharedArrayBuffer(vm);
	reg_native_Atomics(vm);
	reg_native_RegExp(vm);
	reg_native_Math(vm);
	reg_native_Date(vm);
	reg_native_JSON(vm);
	reg_native_Intl(vm);
}

#ifdef __cplusplus /* __cplusplus */
}
#endif
