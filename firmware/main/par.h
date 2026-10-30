#pragma once
#include "fx.h"

// Помощник на ядре 0: тяжёлые проходы эффектов делятся пополам между ядрами.
void par_init(void);
void par_run(fx_rows_fn fn, void *ctx, int h);
