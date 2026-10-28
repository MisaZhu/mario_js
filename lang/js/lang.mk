MARIO_LANG = js
lang_OBJS = $(MARIO_VM)/lang/$(MARIO_LANG)/compiler.o 

NATIVE_PATH = $(MARIO_VM)/lang/$(MARIO_LANG)/native
NATIVES_PATH_BUILTIN = $(NATIVE_PATH)/builtin
NATIVES_PATH = $(NATIVE_PATH)/natives

NATIVE_OBJS= \
	$(NATIVE_PATH)/natives_all.o  \
	\
	$(NATIVES_PATH_BUILTIN)/natives_builtin.o  \
	$(NATIVES_PATH_BUILTIN)/Object/native_Object.o  \
	$(NATIVES_PATH_BUILTIN)/Function/native_Function.o  \
	$(NATIVES_PATH_BUILTIN)/Error/native_Error.o  \
	$(NATIVES_PATH_BUILTIN)/Number/native_Number.o  \
	$(NATIVES_PATH_BUILTIN)/BigInt/native_BigInt.o  \
	$(NATIVES_PATH_BUILTIN)/ArrayBuffer/native_ArrayBuffer.o  \
	$(NATIVES_PATH_BUILTIN)/DataView/native_DataView.o  \
	$(NATIVES_PATH_BUILTIN)/TypedArray/native_TypedArray.o  \
	$(NATIVES_PATH_BUILTIN)/String/native_String.o \
	$(NATIVES_PATH_BUILTIN)/Array/native_Array.o \
	$(NATIVES_PATH_BUILTIN)/Promise/native_Promise.o \
	$(NATIVES_PATH_BUILTIN)/Map/native_Map.o \
	$(NATIVES_PATH_BUILTIN)/Set/native_Set.o \
	$(NATIVES_PATH_BUILTIN)/Symbol/native_Symbol.o \
	$(NATIVES_PATH_BUILTIN)/Proxy/native_Proxy.o \
	$(NATIVES_PATH_BUILTIN)/Reflect/native_Reflect.o \
	$(NATIVES_PATH_BUILTIN)/WeakRef/native_WeakRef.o \
	$(NATIVES_PATH_BUILTIN)/FinalizationRegistry/native_FinalizationRegistry.o \
	$(NATIVES_PATH_BUILTIN)/SharedArrayBuffer/native_SharedArrayBuffer.o \
	$(NATIVES_PATH_BUILTIN)/Atomics/native_Atomics.o \
	$(NATIVES_PATH_BUILTIN)/RegExp/native_RegExp.o \
	$(NATIVES_PATH_BUILTIN)/Math/native_Math.o \
	$(NATIVES_PATH_BUILTIN)/Date/native_Date.o \
	$(NATIVES_PATH_BUILTIN)/JSON/JSON.o \
	$(NATIVES_PATH_BUILTIN)/JSON/native_JSON.o \
	$(NATIVES_PATH_BUILTIN)/Intl/native_Intl.o \
	\
	$(NATIVES_PATH)/natives.o  \
	$(NATIVES_PATH)/Console/native_Console.o \
	$(NATIVES_PATH)/TextEncoder/native_TextEncoder.o \
	$(NATIVES_PATH)/Stream/native_Stream.o \
	$(NATIVES_PATH)/Process/native_Process.o \
	$(NATIVES_PATH)/Buffer/native_Buffer.o \
	$(NATIVES_PATH)/URL/native_URL.o \
	$(NATIVES_PATH)/EventTarget/native_EventTarget.o \
	$(NATIVES_PATH)/AbortController/native_AbortController.o \
	$(NATIVES_PATH)/EventEmitter/native_EventEmitter.o \
	$(NATIVES_PATH)/Web/native_Web.o
