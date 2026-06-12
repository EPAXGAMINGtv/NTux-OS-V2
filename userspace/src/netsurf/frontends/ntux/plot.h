#ifndef NTUX_PLOT_H
#define NTUX_PLOT_H

#include <stdint.h>

extern struct plotter_table *ntux_plotters_table;
void ntux_plot_set_buffer(uint32_t *buf, int w, int h);

#endif
