#ifndef BARH
#define BARH

#include "ewm.h"

struct fcft_font;
extern struct fcft_font* fcft_font;

bool set_font_scale(int scale);
void render_bar(WlOutput* output);

#endif
