#include "natives.h"
#include "Console/native_Console.h"
#include "TextEncoder/native_TextEncoder.h"
#include "Stream/native_Stream.h"
#include "Process/native_Process.h"
#include "Buffer/native_Buffer.h"
#include "URL/native_URL.h"
#include "EventTarget/native_EventTarget.h"
#include "AbortController/native_AbortController.h"
#include "EventEmitter/native_EventEmitter.h"
#include "Web/native_Web.h"

#ifdef __cplusplus /* __cplusplus */
extern "C" {
#endif

void reg_natives(vm_t* vm) {
	reg_native_Console(vm);
	reg_native_TextEncoder(vm);
	/* ReadableStream/WritableStream are standard browser Web Streams globals,
	 * so Stream stays in the shared (CLI + browser) set. */
	reg_native_Stream(vm);
	/* NOTE: the Node.js-only globals `process`, `Buffer` and `EventEmitter` are
	 * deliberately NOT registered here. A real browser page sees
	 * `typeof process === "undefined"`, `typeof Buffer === "undefined"` and
	 * `typeof EventEmitter === "undefined"`. Because reg_all_natives (and thus
	 * this) runs for BOTH the CLI and the browser embedder, installing them here
	 * leaked a non-browser environment fingerprint into the page context. Page-
	 * side anti-bot VMPs (e.g. taobao's baxia et_f) probe exactly these typeofs
	 * alongside "VM2"/"lwvm"/"eval" sandbox checks; detecting a Node global
	 * drives the VMP down its degraded/abort path (its register-init phase never
	 * runs, leaving r[q]=="" -> al("") -> fatal ReferenceError), so no bx-ua
	 * token is produced and the guarded data APIs stay rejected. The CLI installs
	 * all three itself (see init_args in bin/mario/mario.c), keeping Node-bundle
	 * feature detection working while the browser matches Chrome. */
	reg_native_URL(vm);
	reg_native_EventTarget(vm);
	reg_native_AbortController(vm);
	reg_native_Web(vm);
}

#ifdef __cplusplus /* __cplusplus */
}
#endif
