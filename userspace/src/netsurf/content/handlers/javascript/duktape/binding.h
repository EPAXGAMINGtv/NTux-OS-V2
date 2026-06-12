#ifndef DUKTAPE_BINDING_H
#define DUKTAPE_BINDING_H

#include "duktape.h"

typedef struct jsvm jsvm;

duk_ret_t dukky_init(jsvm *ctx);
duk_ret_t dukky_do_not_use_me(duk_context *duk_ctx);

#endif
