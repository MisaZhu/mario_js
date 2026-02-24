#include "native_builtin.h"
#include "object/native_object.h"
#include "number/native_number.h"
#include "array/native_array.h"
#include "string/native_string.h"
#include "promise/native_promise.h"
#include "math/native_math.h"
#include "console/native_console.h"
#include "json/native_json.h"
#include "date/native_date.h"
#include "error/native_error.h"

#ifdef __cplusplus /* __cplusplus */
extern "C" {
#endif

void reg_basic_natives(vm_t* vm) {
	reg_native_object(vm);
	reg_native_error(vm);
	reg_native_array(vm);
	reg_native_string(vm);
	reg_native_console(vm);
	reg_native_number(vm);
	reg_native_promise(vm);
	reg_native_math(vm);
	reg_native_json(vm);
	reg_native_date(vm);
}

#ifdef __cplusplus /* __cplusplus */
}
#endif
