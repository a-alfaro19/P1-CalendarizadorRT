#include "calibracion.h"

uint32_t calib_s_n(uint32_t ut_contadas, uint32_t us_medidos, uint32_t tick_us)
{
    if (us_medidos == 0 || tick_us == 0)
        return 0;

    uint64_t ut_por_tick = (uint64_t)ut_contadas * tick_us / us_medidos;
    if (ut_por_tick > UINT32_MAX)
        ut_por_tick = UINT32_MAX;

    uint64_t s_n = ut_por_tick * CAL_MARGEN_PCT / 100u;
    return s_n == 0 ? 1u : (uint32_t)s_n;
}
