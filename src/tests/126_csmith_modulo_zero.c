/* Regression from csmith-like code: expression evaluates to modulo by zero.
 * Picoc must report "modulo by zero" (or "division by zero") instead of
 * crashing the process with SIGFPE.
 *
 * Reduced from a larger csmith sample that used struct S0, 3D arrays and
 * nested pointer expressions in func_2. */

#include <stdio.h>
#include <stdint.h>

struct S0 {
   int16_t  f0;
   uint16_t  f1;
   int64_t  f2;
   uint64_t  f3;
   uint32_t  f4;
   int64_t  f5;
   uint16_t  f6;
   int32_t  f7;
   uint32_t  f8;
};

static int32_t g_15[10] = {
    0xE4E91208,0xE4E91208,0xE4E91208,0xE4E91208,0xE4E91208,
    0xE4E91208,0xE4E91208,0xE4E91208,0xE4E91208,0xE4E91208
};
static int32_t *g_29 = (void*)0;
static uint16_t g_32 = 0x07AD;
static int32_t g_35 = 0xD5D51814;
static struct S0 g_48 = {
    -5, 0, 0x183D96456CFC4FF2LL, 1UL, 1UL,
    0x635300547507AF9CLL, 0x3EAA, 6, 4294967295UL
};
static int32_t g_52 = 0xA038AA13;
static uint16_t g_102 = 65535;
static int32_t g_126[9][4][7];
static int16_t g_144 = -1;
static int64_t g_200 = 0x6F43F8A5E6A3A859LL;
static int32_t g_204 = -8;
static int32_t **g_237 = (void*)0;
static int32_t *g_274;
static int32_t **g_273;
static uint16_t g_293 = 0xCD62;
static uint64_t *g_309;
static int16_t g_374 = 1;
static int32_t g_379 = 2;

static uint64_t func_2(uint16_t p_3, int32_t p_4, int32_t p_5, uint64_t p_6);
static int64_t  func_16(struct S0 p_17, int32_t *p_18, int32_t *p_19);
static int32_t *func_21(int32_t *p_22, int32_t p_23, int32_t *p_24);
static int32_t *func_25(int32_t *p_26, int32_t *p_27, uint32_t p_28);

static uint32_t func_1(void)
{
    uint32_t l_7 = 4294967293UL;
    int32_t *l_14 = &g_15[7];
    struct S0 l_20 = {
        0xB489, 0x7A9D, 0xE819D3388A019320LL, 0xAF066B7A7BC6C895LL, 0xBB35B689,
        0x635300547507AF9CLL, 0x3EAA, 6, 4294967295UL
    };
    int32_t *l_30[5] = {0};
    uint64_t l_31 = 0xDAAFD55BE9843C6BLL;
    int32_t **l_39 = &l_30[0];
    uint32_t l_491 = 4294967293UL;
    int64_t l_748 = -10;
    int16_t l_749 = -4;

    l_749 = (func_2(l_7,
                    ((*g_274) = (l_7 && ((uint16_t)((int32_t)0xF6802998 - (int32_t)((*l_14) = ((uint16_t)65535 << (uint16_t)9))) >> (uint16_t)(func_16(l_20, ((*l_39) = func_21(func_25(g_29, l_30[0], l_31), g_35, &g_35)), l_14) != g_204)))),
                    l_491,
                    (*g_309)) , l_748);
    return 0;
}

static uint64_t func_2(uint16_t p_3, int32_t p_4, int32_t p_5, uint64_t p_6)
{
    int32_t ***l_521[5][5] = {
        {&g_273,&g_237,&g_237,&g_237,&g_237},
        {&g_273,&g_273,&g_237,&g_273,&g_273},
        {&g_237,&g_237,&g_237,&g_237,&g_273},
        {&g_273,&g_237,&g_237,&g_273,&g_237},
        {&g_273,&g_237,(void*)0,&g_237,&g_273}
    };
    int32_t ***l_522[10] = {
        &g_237,&g_237,&g_237,&g_237,&g_237,
        &g_237,&g_237,&g_237,&g_237,&g_237
    };
    int64_t l_526 = 0x14F0EA11100AF2B3LL;
    int16_t l_527 = 0;
    uint64_t l_528 = 1UL;
    int32_t l_532 = -1;

    if (((uint64_t)(((((uint16_t)g_102 * (uint16_t)(g_32 = (((((l_532 = (((uint16_t)((((uint16_t)(--p_3) << (uint16_t)g_48.f6) >= g_32) <= ((int32_t)((((uint16_t)((g_200 = (((g_48.f1 ^= ((1L ^ (((l_528 = (l_527 = ((int16_t)((((((int16_t)p_5 * (int16_t)((uint32_t)((int32_t)(l_521[3][2] != (l_522[4] = l_521[3][2])) % (int32_t)((uint16_t)(-(uint16_t)l_526) % (uint16_t)p_6)) - (uint32_t)p_4)) != p_6) <= 0x15E5) , &g_144) != &g_144) / (int16_t)g_52))) & 8UL) == 9L)) & 0x31F7)) > 0L) , g_48.f8)) <= 0xE38AB67088FAEE93LL) << (uint16_t)p_6) || 4294967292UL) , 0xF6C187DE) + (int32_t)p_4)) << (uint16_t)g_48.f6) , 0xDB8C250E7B4AEB2BLL)) | 0xCA817D1E21DFAE80LL) >= g_48.f0) || g_374) != g_48.f5))) ^ g_293) , 0x48E2) <= 0x4D04) + (uint64_t)p_5))
    {
    }
    return 0;
}

static int64_t func_16(struct S0 p_17, int32_t *p_18, int32_t *p_19)
{
    return g_379;
}

static int32_t *func_21(int32_t *p_22, int32_t p_23, int32_t *p_24)
{
    int32_t *l_38 = &g_35;
    return l_38;
}

static int32_t *func_25(int32_t *p_26, int32_t *p_27, uint32_t p_28)
{
    return g_29;
}

int main(void)
{
    int i, j, k;
    /* fill 3D array so g_274 has a valid target */
    for (i = 0; i < 9; i++)
        for (j = 0; j < 4; j++)
            for (k = 0; k < 7; k++)
                g_126[i][j][k] = 1;
    g_274 = &g_126[1][3][2];
    g_273 = &g_274;
    g_309 = &g_48.f3;

    func_1();
    printf("should not reach\n");
    return 0;
}
