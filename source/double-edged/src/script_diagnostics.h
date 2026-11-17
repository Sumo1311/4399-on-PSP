#ifndef DOUBLE_EDGED_SCRIPT_DIAGNOSTICS_H
#define DOUBLE_EDGED_SCRIPT_DIAGNOSTICS_H
#include "quickjs.h"
#include <stdio.h>
/* Flash catches script errors at event/frame boundaries. Retain their details
 * separately so an interpreter/API failure cannot become a silent QA pass. */
static unsigned de_script_error_records;
static JSValue de_script_error(JSContext* context, JSValueConst self, int argc, JSValueConst* argv) {
    (void)self;
    if (++de_script_error_records > 1024) return JS_UNDEFINED;
    const char* record=JS_ToCString(context,argc?argv[0]:JS_UNDEFINED);
    if (!record) return JS_EXCEPTION;
    FILE* file=fopen("SCRIPT_ERRORS.JSONL","a");
    if(file){fprintf(file,"%s\n",record);fclose(file);}
    JS_FreeCString(context,record);
    return JS_UNDEFINED;
}
static void de_script_diagnostics_bind(JSContext* context) {
    de_script_error_records=0;
#if defined(__PSP__) && !defined(DE_RENDER_PROBE)
    FILE* marker=fopen("TRACE.QA","rb");if(!marker)return;fclose(marker);
#endif
    FILE* file=fopen("SCRIPT_ERRORS.JSONL","w");if(file)fclose(file);
    JSValue global=JS_GetGlobalObject(context);
    JS_SetPropertyStr(context,global,"nativeScriptError",JS_NewCFunction(context,de_script_error,"nativeScriptError",1));
    JS_FreeValue(context,global);
}
#endif
