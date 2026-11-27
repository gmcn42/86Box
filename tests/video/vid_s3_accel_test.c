/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Tests of the S3 graphics engine, through its port and MMIO
 *          registers, on every chip vid_s3.c emulates.
 *
 *          The engine is included as C, for its private functions. The
 *          FIFO thread runs on the test's own thread. Every test runs on
 *          every chip, but is only asserted on the chips it lists: those
 *          whose S3 databook (or S3.TXT, below) states its rule, with the
 *          page or line cited at the test. -v lists every result per chip.
 *
 *          The Vision868, Vision968 and Trio64V2 books describe their
 *          changes against the Vision864, Vision964 and Trio64V+ books
 *          (Vision868 p. 1-1, Vision968 p. 1-1, Trio64V2 p. 1-5): a page
 *          cited from the older book also covers the newer chip, unless
 *          its own book changes the rule. For the 86C911, 86C924, 86C801,
 *          86C805 and 86C805i, which have no databook here, the rules come
 *          from S3.TXT in Finn Thoegersen's VGADOC, cited by line; it has
 *          the 86C928PCI as the PCI version of the 86C928 (line 14):
 *          https://pdos.csail.mit.edu/6.828/2018/readings/hardware/vgadoc/S3.TXT
 *
 * Authors: Andreas Zdziarstek, <https://github.com/gmcn42>
 *
 *          Copyright 2026 Andreas Zdziarstek.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/video/vid_s3.c"

#define VRAM_SIZE (2 << 20)

/* One bit per chip, for the chips a test is asserted on. */
#define C_911      (1 << 0)
#define C_924      (1 << 1)
#define C_928      (1 << 2)
#define C_928PCI   (1 << 3)
#define C_801      (1 << 4)
#define C_805      (1 << 5)
#define C_805I     (1 << 6)
#define C_964      (1 << 7)
#define C_968      (1 << 8)
#define C_864      (1 << 9)
#define C_868      (1 << 10)
#define C_TRIO32   (1 << 11)
#define C_TRIO64   (1 << 12)
#define C_TRIO64V  (1 << 13)
#define C_TRIO64V2 (1 << 14)
#define C_ALL      ((1 << 15) - 1)

typedef struct {
    int         chip;
    int         bit;
    const char *name;
    int         packed_mmio; /* As s3_init() sets it */
    int         max_bpp;     /* CR50 pixel length (S3.TXT lines 428, 433-435): none on the 911/924 */
} chip_t;

static const chip_t chips[] = {
    { S3_86C911,    C_911,      "911",       0, 8  },
    { S3_86C924,    C_924,      "924",       0, 8  },
    { S3_86C928,    C_928,      "928",       0, 32 },
    { S3_86C928PCI, C_928PCI,   "928PCI",    0, 32 },
    { S3_86C801,    C_801,      "801",       0, 16 },
    { S3_86C805,    C_805,      "805",       0, 16 },
    { S3_86C805I,   C_805I,     "805i",      0, 16 },
    { S3_VISION964, C_964,      "Vision964", 1, 32 },
    { S3_VISION968, C_968,      "Vision968", 1, 32 },
    { S3_VISION864, C_864,      "Vision864", 0, 32 },
    { S3_VISION868, C_868,      "Vision868", 1, 32 },
    { S3_TRIO32,    C_TRIO32,   "Trio32",    1, 32 },
    { S3_TRIO64,    C_TRIO64,   "Trio64",    1, 32 },
    { S3_TRIO64V,   C_TRIO64V,  "Trio64V+",  1, 32 },
    { S3_TRIO64V2,  C_TRIO64V2, "Trio64V2",  1, 32 }
};

#define NUM_CHIPS (sizeof(chips) / sizeof(chips[0]))
#define MAX_TESTS 32

double    cpuclock = 50000000.0;
monitor_t monitors[MONITORS_NUM];
int       monitor_index_global;
uint32_t *video_15to32, *video_16to32;
int       xga_active;

static s3_t      s3;
static uint8_t   vram[VRAM_SIZE];
static uint8_t   changed[VRAM_SIZE >> 12];
static monitor_t monitor;
static int       fifo_waits;
static char      wake_event, not_full_event;

static int           verbose;
static int           failures;
static const chip_t *cur_chip;
static int           cur_bpp;
static int           asserted; /* The current test lists the current chip */
static int           cur_failed;
static int           num_tests;
static const char   *test_names[MAX_TESTS];
static char          results[MAX_TESTS][NUM_CHIPS];

// clang-format off
/* Outside the tested paths. */
void bt48x_hwcursor_draw(svga_t *svga, int displine) { abort(); }
void bt48x_ramdac_out(uint16_t addr, int rs2, int rs3, uint8_t val, void *priv, svga_t *svga) { abort(); }
uint8_t bt48x_ramdac_in(uint16_t addr, int rs2, int rs3, void *priv, svga_t *svga) { abort(); }
void bt48x_recalctimings(void *priv, svga_t *svga) { abort(); }
void att49x_ramdac_out(uint16_t addr, int rs2, uint8_t val, void *priv, svga_t *svga) { abort(); }
uint8_t att49x_ramdac_in(uint16_t addr, int rs2, void *priv, svga_t *svga) { abort(); }
void att498_ramdac_out(uint16_t addr, int rs2, uint8_t val, void *priv, svga_t *svga) { abort(); }
uint8_t att498_ramdac_in(uint16_t addr, int rs2, void *priv, svga_t *svga) { abort(); }
void ibm_rgb528_ramdac_out(uint16_t addr, int rs2, uint8_t val, void *priv, svga_t *svga) { abort(); }
uint8_t ibm_rgb528_ramdac_in(uint16_t addr, int rs2, void *priv, svga_t *svga) { abort(); }
void ibm_rgb528_recalctimings(void *priv, svga_t *svga) { abort(); }
void mu9c1880_ramdac_out(uint16_t addr, int rs2, uint8_t val, void *priv, svga_t *svga) { abort(); }
uint8_t mu9c1880_ramdac_in(uint16_t addr, int rs2, void *priv, svga_t *svga) { abort(); }
void sc1148x_ramdac_out(uint16_t addr, int rs2, uint8_t val, void *priv, svga_t *svga) { abort(); }
uint8_t sc1148x_ramdac_in(uint16_t addr, int rs2, void *priv, svga_t *svga) { abort(); }
void sc1502x_ramdac_out(uint16_t addr, uint8_t val, void *priv, svga_t *svga) { abort(); }
uint8_t sc1502x_ramdac_in(uint16_t addr, void *priv, svga_t *svga) { abort(); }
void sdac_ramdac_out(uint16_t addr, int rs2, uint8_t val, void *priv, svga_t *svga) { abort(); }
uint8_t sdac_ramdac_in(uint16_t addr, int rs2, void *priv, svga_t *svga) { abort(); }
void stg_ramdac_out(uint16_t addr, uint8_t val, void *priv, svga_t *svga) { abort(); }
uint8_t stg_ramdac_in(uint16_t addr, void *priv, svga_t *svga) { abort(); }
void tvp3026_ramdac_out(uint16_t addr, int rs2, int rs3, uint8_t val, void *priv, svga_t *svga) { abort(); }
uint8_t tvp3026_ramdac_in(uint16_t addr, int rs2, int rs3, void *priv, svga_t *svga) { abort(); }
void tvp3026_recalctimings(void *priv, svga_t *svga) { abort(); }
bool nmc93cxx_eeprom_read(nmc93cxx_eeprom_t *dev) { abort(); }
void nmc93cxx_eeprom_write(nmc93cxx_eeprom_t *dev, bool eecs, bool eesk, bool eedi) { abort(); }
void i2c_gpio_set(void *dev_handle, uint8_t scl, uint8_t sda) { abort(); }
uint8_t i2c_gpio_get_sda(void *dev_handle) { abort(); }
uint8_t i2c_gpio_get_scl(void *dev_handle) { abort(); }
uint8_t svga_in(uint16_t addr, void *priv) { abort(); }
uint8_t svga_read_linear(uint32_t addr, void *priv) { abort(); }
uint16_t svga_readw_linear(uint32_t addr, void *priv) { abort(); }
uint32_t svga_readl_linear(uint32_t addr, void *priv) { abort(); }
void svga_write_linear(uint32_t addr, uint8_t val, void *priv) { abort(); }
void svga_writew_linear(uint32_t addr, uint16_t val, void *priv) { abort(); }
void svga_writel_linear(uint32_t addr, uint32_t val, void *priv) { abort(); }
uint8_t xga_read_test(uint32_t addr, void *priv) { abort(); }
void xga_write_test(uint32_t addr, uint8_t val, void *priv) { abort(); }

/* Mappings only need their state, for the MMIO window; IRQs and timings don't matter here. */
void mem_mapping_enable(mem_mapping_t *map) { map->enable = 1; }
void mem_mapping_disable(mem_mapping_t *map) { map->enable = 0; }
void mem_mapping_set_addr(mem_mapping_t *map, uint32_t base, uint32_t size) { map->base = base; map->size = size; map->enable = 1; }
void mem_mapping_set_handler(mem_mapping_t *map, uint8_t (*read_b)(uint32_t addr, void *priv),
                             uint16_t (*read_w)(uint32_t addr, void *priv),
                             uint32_t (*read_l)(uint32_t addr, void *priv),
                             void (*write_b)(uint32_t addr, uint8_t val, void *priv),
                             void (*write_w)(uint32_t addr, uint16_t val, void *priv),
                             void (*write_l)(uint32_t addr, uint32_t val, void *priv)) { }
void pci_irq(uint8_t slot, uint8_t pci_int, int level, int set, uint8_t *irq_state) { }
void svga_out(uint16_t addr, uint8_t val, void *priv) { }
void svga_recalctimings(svga_t *svga) { }
void svga_vert_int_crtc_write(svga_t *svga) { }
int svga_vert_int_active(const svga_t *svga) { return 0; }
uint64_t plat_timer_read(void) { return 0; }
// clang-format on

/* The ports as vid_s3.c registers them, per chip. */
static struct {
    uint8_t (*inb)(uint16_t port, void *priv);
    uint16_t (*inw)(uint16_t port, void *priv);
    void (*outb)(uint16_t port, uint8_t val, void *priv);
    void (*outw)(uint16_t port, uint16_t val, void *priv);
    void (*outl)(uint16_t port, uint32_t val, void *priv);
} io[0x10000];

void
io_sethandler(uint16_t base, uint16_t size, uint8_t (*inb)(uint16_t port, void *priv),
              uint16_t (*inw)(uint16_t port, void *priv), uint32_t (*inl)(uint16_t port, void *priv),
              void (*outb)(uint16_t port, uint8_t val, void *priv),
              void (*outw)(uint16_t port, uint16_t val, void *priv),
              void (*outl)(uint16_t port, uint32_t val, void *priv), void *priv)
{
    for (int i = 0; i < size; i++) {
        io[base + i].inb  = inb;
        io[base + i].inw  = inw;
        io[base + i].outb = outb;
        io[base + i].outw = outw;
        io[base + i].outl = outl;
    }
}

void
io_removehandler(uint16_t base, uint16_t size, uint8_t (*inb)(uint16_t port, void *priv),
                 uint16_t (*inw)(uint16_t port, void *priv), uint32_t (*inl)(uint16_t port, void *priv),
                 void (*outb)(uint16_t port, uint8_t val, void *priv),
                 void (*outw)(uint16_t port, uint16_t val, void *priv),
                 void (*outl)(uint16_t port, uint32_t val, void *priv), void *priv)
{
    memset(&io[base], 0, size * sizeof(io[0]));
}

static float
test_clock(int clock, void *priv)
{
    return 40000000.0f;
}

/* Run the FIFO thread's loop once: its second wait for work ends it. */
static void
drain(void)
{
    fifo_waits         = 0;
    s3.fifo_thread_run = 1;
    mach64_fifo_thread(&s3);
}

void
thread_set_event(event_t *event)
{
}
void
thread_reset_event(event_t *event)
{
}

int
thread_wait_event(event_t *event, int timeout)
{
    if (event == s3.wake_fifo_thread) {
        if (++fifo_waits > 1)
            s3.fifo_thread_run = 0;
    } else
        drain(); /* Waiting for room or for the engine to go idle. */
    return 0;
}

static void
check(int cond, const char *fmt, ...)
{
    va_list ap;

    if (cond)
        return;
    cur_failed = 1;
    if (!asserted && !verbose)
        return;
    fprintf(stderr, "%s: %s, %d bpp: ", asserted ? "FAIL" : "differs", cur_chip->name, cur_bpp);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    if (asserted)
        failures++;
}

static void
crtc_w(uint8_t index, uint8_t val)
{
    s3_out(0x3d4, index, &s3);
    s3_out(0x3d5, val, &s3);
}

/* As the I/O bus does it: a port without a word handler takes two byte accesses. */
static void
port_w(uint16_t port, uint16_t val)
{
    if (io[port].outw)
        io[port].outw(port, val, &s3);
    else if (io[port].outb) {
        io[port].outb(port, val, &s3);
        if (io[port + 1].outb)
            io[port + 1].outb(port + 1, val >> 8, &s3);
    }
}

static void
port_l(uint16_t port, uint32_t val)
{
    if (io[port].outl)
        io[port].outl(port, val, &s3);
    else {
        port_w(port, val);
        port_w(port + 2, val >> 16);
    }
}

static uint16_t
port_r(uint16_t port)
{
    uint16_t val = 0xffff;

    drain();
    if (io[port].inw)
        val = io[port].inw(port, &s3);
    else if (io[port].inb)
        val = io[port].inb(port, &s3) | ((io[port + 1].inb ? io[port + 1].inb(port + 1, &s3) : 0xff) << 8);
    return val;
}

/* A doubleword store into the MMIO window that vid_s3.c maps for the chip. */
static void
mmio_l(uint32_t addr, uint32_t val)
{
    const mem_mapping_t *map = &s3.mmio_mapping;

    check(map->enable && (addr >= map->base) && (addr < map->base + map->size), "no MMIO at %05X", addr);
    if (map->enable)
        s3_accel_write_l(addr, val, &s3);
}

static void
mmio_w(uint32_t addr, uint16_t val)
{
    const mem_mapping_t *map = &s3.mmio_mapping;

    check(map->enable && (addr >= map->base) && (addr < map->base + map->size), "no MMIO at %05X", addr);
    if (map->enable)
        s3_accel_write_w(addr, val, &s3);
}

static uint32_t
pix(int x, int y)
{
    uint32_t addr = (y * s3.width + x) * (cur_bpp / 8);

    drain();
    switch (cur_bpp) {
        case 8:
            return vram[addr];
        case 16:
            return vram[addr] | (vram[addr + 1] << 8);
        default:
            return vram[addr] | (vram[addr + 1] << 8) | (vram[addr + 2] << 16) | ((uint32_t) vram[addr + 3] << 24);
    }
}

/* In 32 bpp a colour takes two word writes, low half first (RSF, BEE8h index E bit 4). */
static void
color_w(uint16_t port, uint32_t val)
{
    port_w(port, val);
    if (cur_bpp == 32)
        port_w(port, val >> 16);
}

/* The bits of a pixel at the current depth. */
static uint32_t
depth_mask(void)
{
    return (cur_bpp == 32) ? 0xffffffff : ((1 << cur_bpp) - 1);
}

/* A colour that fits the depth. */
static uint32_t
color(void)
{
    return 0x00c3a5e1 & depth_mask();
}

/* Rectangle fill solid as in Trio32/64 13.3.3.3 (p. 13-12), up to the height and the command. */
static void
rect_start(int x, int y, int width)
{
    port_w(0xbae8, 0x0027); /* FRGD_MIX: FRGD_COLOR, NEW */
    color_w(0xa6e8, color());
    port_w(0xbee8, 0xa000); /* PIX_CNTL: FRGD_MIX selects */
    port_w(0x86e8, x);
    port_w(0x82e8, y);
    port_w(0x96e8, width - 1); /* MAJ_AXIS_PCNT */
}

/* A 1024-pixel-wide enhanced mode as a driver sets it, with standard MMIO and open scissors. */
static void
card_setup(const chip_t *c, int bpp)
{
    svga_t *svga = &s3.svga;

    memset(&s3, 0, sizeof(s3));
    memset(vram, 0, sizeof(vram));
    monitor.mon_changeframecount = 2;
    svga->priv                   = &s3;
    svga->monitor                = &monitor;
    svga->vram                   = vram;
    svga->changedvram            = changed;
    svga->vram_max               = VRAM_SIZE;
    svga->decode_mask            = VRAM_SIZE - 1;
    svga->getclock               = test_clock;
    svga->miscout                = 0x01; /* CRTC at 3D4h */
    svga->bpp                    = bpp;
    svga->hdisp_old              = 1024;
    svga->dispend                = 768;
    s3.chip                      = c->chip;
    s3.packed_mmio               = c->packed_mmio;
    s3.vram_mask                 = VRAM_SIZE - 1;
    s3.accel_start               = s3_accel_start;
    s3.wake_fifo_thread          = &wake_event;
    s3.fifo_not_full_event       = &not_full_event;
    s3.mmio_mapping.base         = 0xa0000; /* As s3_init() adds it */
    s3.mmio_mapping.size         = 0x10000;

    memset(io, 0, sizeof(io));
    s3_io_set(&s3);
    crtc_w(0x38, 0x48); /* Unlock the S3 registers */
    crtc_w(0x39, 0xa5);
    crtc_w(0x40, 0x01); /* Enhanced register access */
    crtc_w(0x50, (bpp == 32) ? 0x30 : ((bpp == 16) ? 0x10 : 0x00));
    crtc_w(0x53, 0x10); /* Standard MMIO */
    s3_accel_out(0x4ae8, 0x01, &s3);
    s3_recalctimings(svga);

    port_w(0xbee8, 0x1000); /* SCISSORS_T */
    port_w(0xbee8, 0x2000); /* SCISSORS_L */
    port_w(0xbee8, 0x3000 | 767);
    port_w(0xbee8, 0x4000 | 1023);
    port_w(0xbee8, 0xe000); /* MULT_MISC */
    port_w(0xaae8, 0xffff); /* WRT_MASK */
    if (bpp == 32)
        port_w(0xaae8, 0xffff);
    drain();

    cur_chip = c;
    cur_bpp  = bpp;
}

/* Run a test on every chip at each depth it has; assert it on the chips in `asserted_on`. */
static void
run(const char *name, void (*test)(void), int asserted_on)
{
    static const int depths[] = { 8, 16, 32 };

    test_names[num_tests] = name;
    for (size_t i = 0; i < NUM_CHIPS; i++) {
        char result = '-';

        for (size_t d = 0; d < sizeof(depths) / sizeof(depths[0]); d++) {
            if (depths[d] > chips[i].max_bpp)
                continue;
            card_setup(&chips[i], depths[d]);
            asserted   = !!(asserted_on & chips[i].bit);
            cur_failed = 0;
            test();
            if (cur_failed)
                result = asserted ? 'F' : 'x';
            else if (result == '-')
                result = 'o';
        }
        results[num_tests][i] = result;
    }
    num_tests++;
}

static void
print_matrix(void)
{
    printf("%-24s", "");
    for (size_t i = 0; i < NUM_CHIPS; i++)
        printf(" %-9s", chips[i].name);
    printf("\n");
    for (int t = 0; t < num_tests; t++) {
        printf("%-24s", test_names[t]);
        for (size_t i = 0; i < NUM_CHIPS; i++)
            printf(" %-9c", results[t][i]);
        printf("\n");
    }
    printf("o = passes, F = fails, x = fails on a chip the test isn't asserted on\n");
}

/* CUR_X and CUR_Y (86E8h, 82E8h) are read/write: 86C928 p. 10-5, Vision864 p. 18-4,
   Vision964 p. 18-4, Trio32/64 pp. 18-4 and 18-5, Trio64V+ p. 20-4, S3.TXT lines 844-848. */
static void
test_cur_xy(void)
{
    port_w(0x86e8, 123);
    port_w(0x82e8, 456);
    check(port_r(0x86e8) == 123, "CUR_X reads %u", port_r(0x86e8));
    check(port_r(0x82e8) == 456, "CUR_Y reads %u", port_r(0x82e8));
}

/* Rows filled by a rectangle at (10, 20), 8 wide, after its height was set. */
static void
check_rows(int rows)
{
    for (int y = 20; y < 20 + rows; y++)
        check((pix(10, y) == color()) && (pix(17, y) == color()), "row %d not filled", y - 20);
    check((pix(10, 20 + rows) == 0) && (pix(18, 20) == 0), "drew outside %dx8", rows);
}

/* MULTIFUNC_CNTL (BEE8h) index 0 is MIN_AXIS_PCNT, the rectangle height - 1: 86C928 p. 10-15,
   Vision864 p. 18-15, Vision964 p. 18-15, Trio32/64 p. 18-20, Trio64V+ p. 20-15, S3.TXT
   lines 1109-1112. */
static void
test_bee8_port(void)
{
    rect_start(10, 20, 8);
    port_w(0xbee8, 0x0000 | 7);
    port_w(0x9ae8, 0x40b1);
    check_rows(8);
}

/* A doubleword store to BEE8h in the standard MMIO window writes one 16-bit register:
   there is no register at BEEAh (86C928 table 3-1, p. 3-4; Vision864 and Vision964 table 9-5,
   p. 9-8; Vision868 and Vision968 table 5-1, p. 5-2; Trio32/64 table 13-1, p. 13-5; Trio64V+
   table 15-1, p. 15-5). XFree86 3.3's S3 server sets the height this way. S3.TXT puts the
   86C801/805 MMIO at A0000h-A7FFFh (lines 477-478), not at A8000h-AFFFFh like the 86C928's
   command registers (lines 479-482), so those chips aren't asserted. */
static void
test_bee8_mmio32(void)
{
    rect_start(10, 20, 8);
    mmio_l(0xabee8, 0x00000007);
    port_w(0x9ae8, 0x40b1);
    check_rows(8);
}

/* Packed MMIO: a doubleword store from A8100h writes two registers, the first in the low half
   (Vision868 and Vision968 table 5-1, p. 5-2; Trio32/64 table 13-1, p. 13-5; Trio64V+ table 15-1,
   p. 15-5). The Vision864 and Vision964 books have no packed MMIO (table 9-5, p. 9-8). */
static void
test_packed_mmio(void)
{
    mmio_l(0xa8100, (123 << 16) | 456); /* ALT_CURXY: CUR_Y, CUR_X */
    check((port_r(0x86e8) == 123) && (port_r(0x82e8) == 456), "ALT_CURXY");

    /* An 8x8 rectangle at (10, 20) that only the packed FRGD_MIX lets draw. */
    rect_start(0, 0, 1);
    port_w(0xbae8, 0x0023);                   /* FRGD_MIX: leave the destination */
    mmio_l(0xa8100, (10 << 16) | 20);         /* ALT_CURXY */
    mmio_l(0xa8148, (7 << 16) | 7);           /* ALT_PCNT: MIN_AXIS_PCNT, MAJ_AXIS_PCNT */
    mmio_l(0xa8134, (0x0027 << 16) | 0x0003); /* ALT_MIX: BKGD_MIX, FRGD_MIX */
    mmio_l(0xa8138, (12 << 16) | 22);         /* SCISSORS_T, SCISSORS_L */
    mmio_l(0xa813c, (1023 << 16) | 767);      /* SCISSORS_B, SCISSORS_R */
    port_w(0x9ae8, 0x40b1);
    check((pix(12, 22) == color()) && (pix(17, 27) == color()), "rectangle not drawn");
    check((pix(11, 22) == 0) && (pix(12, 21) == 0), "scissors not applied");
}

/* The pixels from (x0, y0) to (x1, y1) have the colour and the ring around them is untouched. */
static void
check_area(int x0, int y0, int x1, int y1)
{
    int bad = 0;

    for (int y = y0 - 1; y <= y1 + 1; y++) {
        for (int x = x0 - 1; x <= x1 + 1; x++) {
            int inside = (x >= x0) && (x <= x1) && (y >= y0) && (y <= y1);

            if (pix(x, y) != (inside ? color() : pix(0, 0)) && !bad++)
                check(0, "pixel (%d, %d) is %X", x, y, pix(x, y));
        }
    }
}

/* Rectangle fill solid (CMD 010: 86C928 p. 10-10, Vision864 p. 18-9, Vision964 p. 18-9;
   Vision868 and Vision968 5.4.3.3, p. 5-14; Trio32/64 13.3.3.3, p. 13-12; Trio64V+ 15.4.3.3,
   p. 15-12; S3.TXT lines 977-981). The scissors hold the lowest and highest X and Y that are
   drawn (86C928 pp. 10-16 and 10-17, Vision864 pp. 18-15 and 18-16, Vision964 pp. 18-15 and
   18-16, Trio32/64 pp. 18-20 and 18-21, Trio64V+ pp. 20-15 and 20-16), the lowest and highest
   coordinates of the clipping rectangle (S3.TXT lines 1115-1136). */
static void
test_rect_scissors(void)
{
    memset(vram, 0x5a, sizeof(vram));
    rect_start(100, 50, 20);
    port_w(0xbee8, 0x0000 | 9);
    port_w(0x9ae8, 0x40b1);
    check_area(100, 50, 119, 59);

    memset(vram, 0x5a, sizeof(vram));
    port_w(0xbee8, 0x1000 | 52);  /* SCISSORS_T */
    port_w(0xbee8, 0x2000 | 105); /* SCISSORS_L */
    port_w(0xbee8, 0x3000 | 55);  /* SCISSORS_B */
    port_w(0xbee8, 0x4000 | 110); /* SCISSORS_R */
    rect_start(100, 50, 20);
    port_w(0xbee8, 0x0000 | 9);
    port_w(0x9ae8, 0x40b1);
    check_area(105, 52, 110, 55);
}

/* The 16 mixes of BKGD_MIX and FRGD_MIX bits 3-0, as the books list them, between the current
   bitmap colour c and the new colour n (86C928 p. 10-14, Vision864 p. 18-14, Vision964 p. 18-14,
   Trio32/64 p. 18-19, Trio64V+ p. 20-14, S3.TXT lines 1060-1079). */
static uint32_t
mix_result(int mix, uint32_t c, uint32_t n)
{
    switch (mix) {
        case 0x0:
            return ~c;
        case 0x1:
            return 0;
        case 0x2:
            return ~0;
        case 0x3:
            return c;
        case 0x4:
            return ~n;
        case 0x5:
            return c ^ n;
        case 0x6:
            return ~(c ^ n);
        case 0x7:
            return n;
        case 0x8:
            return ~c | ~n;
        case 0x9:
            return c | ~n;
        case 0xa:
            return ~c | n;
        case 0xb:
            return c | n;
        case 0xc:
            return c & n;
        case 0xd:
            return ~c & n;
        case 0xe:
            return c & ~n;
        default:
            return ~c & ~n;
    }
}

/* Fill a 4x2 rectangle at (10, 20) with the mix and colours set up, and check that `want` is
   drawn there and the pixels around it are still `old`. */
static void
check_small_rect(uint32_t want, uint32_t old, const char *what, int arg)
{
    port_w(0xbee8, 0x0000 | 1);
    port_w(0x9ae8, 0x40b1);
    check((pix(10, 20) == want) && (pix(13, 21) == want), "%s %X draws %X, not %X", what, arg, pix(10, 20), want);
    check((pix(9, 20) == old) && (pix(14, 21) == old) && (pix(10, 22) == old), "%s %X draws outside", what, arg);
}

/* Each mix over a bitmap of CCh bytes with a foreground colour of AAh bytes, so that every
   pixel holds all four pairs of current and new bits. FRGD_MIX bits 6-5 pick the colour
   source: 01b the foreground colour, 00b the background colour (86C928 p. 10-14,
   Vision864 p. 18-14, Vision964 p. 18-14, Trio32/64 p. 18-19, Trio64V+ p. 20-14, S3.TXT
   lines 1080-1093). */
static void
test_mixes(void)
{
    uint32_t old = 0xcccccccc & depth_mask();
    uint32_t bg  = ~color() & depth_mask();

    for (int mix = 0; mix < 16; mix++) {
        memset(vram, 0xcc, sizeof(vram));
        rect_start(10, 20, 4);
        port_w(0xbae8, 0x0020 | mix); /* FRGD_MIX: FRGD_COLOR */
        color_w(0xa6e8, 0xaaaaaaaa & depth_mask());
        check_small_rect(mix_result(mix, 0xcccccccc, 0xaaaaaaaa) & depth_mask(), old, "mix", mix);
    }

    memset(vram, 0xcc, sizeof(vram));
    rect_start(10, 20, 4);
    color_w(0xa2e8, bg);    /* BKGD_COLOR */
    port_w(0xbae8, 0x0007); /* FRGD_MIX: BKGD_COLOR, NEW */
    check_small_rect(bg, old, "FRGD_MIX", 0x0007);
}

/* WRT_MASK (AAE8h) leaves the bitplanes whose bits are 0 as they are. In 32 bpp it is two 16-bit
   registers, written low half first (86C928 p. 10-12, Vision864 p. 18-12, Vision964 p. 18-12,
   Trio32/64 p. 18-17, Trio64V+ p. 20-12, S3.TXT lines 1031-1037). */
static void
test_wrt_mask(void)
{
    uint32_t mask = 0x0ff0f03c & depth_mask();
    uint32_t old  = 0x5a5a5a5a & depth_mask();

    memset(vram, 0x5a, sizeof(vram));
    rect_start(10, 20, 4);
    color_w(0xaae8, mask);
    check_small_rect((color() & mask) | (old & ~mask), old, "WRT_MASK", mask);
}

/* Chips with a 32-bit PIX_TRANS (CMD bits 10-9 = 10b): Vision864 p. 18-9, Vision964 p. 18-9,
   Trio32/64 p. 18-12, Trio64V+ p. 20-9, S3.TXT lines 963-965. The 86C928 has bit 9 only, 8 or
   16 bits (p. 10-10). */
#define C_BUS32 (C_864 | C_964 | C_868 | C_968 | C_TRIO32 | C_TRIO64 | C_TRIO64V | C_TRIO64V2)
/* Chips that have the image transfer window at A0000h-A7FFFh (Trio32/64 13.3.1, p. 13-4), the
   ones test_bee8_mmio32() is asserted on. */
#define C_MMIO (C_928 | C_928PCI | C_BUS32)

/* Image transfer commands: rectangle fill, byte swap on (low byte first), wait for CPU data,
   +Y, X major, +X, draw (Trio32/64 13.3.3.4, p. 13-13). Bits 10-9 are the bus size. */
#define CMD_XFER  0x51b1
#define CMD_PX_MD 0x0002 /* Across the plane, one bit per pixel */

/* The images start at (XX, XY) and are XH lines high. */
#define XX 40
#define XY 30
#define XH 3

static uint32_t want[XH][16];

/* Send `len` bytes to PIX_TRANS, `unit` bytes per write, through E2E8h or the image transfer
   window. */
static void
pix_trans(const uint8_t *data, int len, int unit, int mmio)
{
    for (int i = 0; i < len; i += unit) {
        uint32_t val = 0;

        for (int b = 0; b < unit; b++)
            val |= (uint32_t) data[i + b] << (b * 8);
        if (mmio && (unit == 4))
            mmio_l(0xa0000 + i, val);
        else if (mmio)
            mmio_w(0xa0000 + i, val);
        else if (unit == 4)
            port_l(0xe2e8, val);
        else if (unit == 2)
            port_w(0xe2e8, val);
        else
            io[0xe2e8].outb(0xe2e8, val, &s3);
    }
}

/* The image is `want` and the ring around it still holds `old`. */
static void
check_image(int width, uint32_t old, const char *what, int unit, int mmio)
{
    int bad = 0;

    for (int y = XY - 1; y <= XY + XH; y++) {
        for (int x = XX - 1; x <= XX + width; x++) {
            int      inside = (x >= XX) && (x < XX + width) && (y >= XY) && (y < XY + XH);
            uint32_t w      = inside ? want[y - XY][x - XX] : old;

            if ((pix(x, y) != w) && !bad++)
                check(0, "%s, %d-bit %s: pixel (%d, %d) is %X, not %X", what, unit * 8, mmio ? "MMIO" : "port",
                      x - XX, y - XY, pix(x, y), w);
        }
    }
}

/* Start an image transfer of `width` pixels per line with the mixes set up. */
static void
xfer_start(int width, uint16_t cmd)
{
    port_w(0x86e8, XX);
    port_w(0x82e8, XY);
    port_w(0x96e8, width - 1);
    port_w(0xbee8, 0x0000 | (XH - 1));
    port_w(0x9ae8, cmd);
}

/* A colour image whose pixels differ. */
static uint32_t
image_pixel(int x, int y)
{
    return (0x9e3779b9 * (uint32_t) (y * 16 + x + 1)) & depth_mask();
}

/* Image transfer through the plane: CPU data is the colour, pixels in increasing addresses, each
   line padded with dummy pixels to the bus width (86C928 11.3.4, p. 11-6; Vision868 and Vision968
   5.4.3.4, p. 5-15; Trio32/64 13.3.3.4, pp. 13-13 and 13-14; Trio64V+ 15.4.3.4, p. 15-13). An
   8-bit bus isn't for 16 and 32 bpp (Trio32/64 p. 13-14). 5 pixels need dummies at 8 bpp with 16
   and 32 bits and at 16 bpp with 32 bits. */
static void
xfer_through(int unit, int mmio)
{
    uint8_t  data[256];
    int      len = 0;
    uint32_t old = 0x5a5a5a5a & depth_mask();

    for (int y = 0; y < XH; y++) {
        int start = len;

        for (int x = 0; x < 5; x++) {
            want[y][x] = image_pixel(x, y);
            for (int b = 0; b < cur_bpp / 8; b++)
                data[len++] = want[y][x] >> (b * 8);
        }
        while ((len - start) % unit)
            data[len++] = 0xee;
    }

    memset(vram, 0x5a, sizeof(vram));
    port_w(0xbae8, 0x0047); /* FRGD_MIX: CPU data, NEW */
    port_w(0xbee8, 0xa000); /* PIX_CNTL: FRGD_MIX selects */
    xfer_start(5, CMD_XFER | ((unit / 2) << 9));
    pix_trans(data, len, unit, mmio);
    check_image(5, old, "through", unit, mmio);
}

static void
test_xfer_through(void)
{
    for (int unit = 1; unit <= 4; unit *= 2) {
        if (((unit == 1) && (cur_bpp != 8)) || ((unit == 4) && !(cur_chip->bit & C_BUS32)))
            continue;
        xfer_through(unit, 0);
        if ((unit > 1) && (cur_chip->bit & C_MMIO))
            xfer_through(unit, 1);
    }
}

/* A 13-pixel-wide glyph. The bits past the width are 1s, which would draw if they weren't
   discarded. */
static const uint8_t glyph[XH][2] = {
    { 0xb5, 0x3f },
    { 0x0f, 0xaf },
    { 0xc3, 0x57 }
};

/* Image transfer across the plane: each CPU bit selects FRGD_MIX (1) or BKGD_MIX (0), here
   FRGD_COLOR with NEW and BKGD_COLOR with XOR as in the books' example (86C928 11.3.5, p. 11-7;
   Vision868 and Vision968 5.4.3.5, p. 5-17; Trio32/64 13.3.3.5, pp. 13-15 and 13-16; Trio64V+
   15.4.3.5, p. 15-15). With bus size 11b only the bits to the next byte are discarded at the end
   of a line, otherwise the rest of the write (Trio32/64 p. 18-12, Trio64V+ p. 20-9, Vision868 and
   Vision968 p. 2-3). The books don't say which bit is the leftmost pixel; as in the code, it is
   bit 7 of the first byte. */
static void
xfer_across(int unit, int mmio, int bus)
{
    uint8_t  data[64];
    int      len = 0;
    uint32_t old = 0x5a5a5a5a & depth_mask();
    uint32_t bg  = 0x9669a55a & depth_mask();

    for (int y = 0; y < XH; y++) {
        for (int x = 0; x < 13; x++)
            want[y][x] = ((glyph[y][x / 8] >> (7 - (x % 8))) & 1) ? color() : (old ^ bg);
        data[len++] = glyph[y][0];
        data[len++] = glyph[y][1];
        while ((bus != 3) && (len % unit))
            data[len++] = 0xff;
    }
    while (len % unit)
        data[len++] = 0xff;

    memset(vram, 0x5a, sizeof(vram));
    port_w(0xbae8, 0x0027); /* FRGD_MIX: FRGD_COLOR, NEW */
    port_w(0xb6e8, 0x0005); /* BKGD_MIX: BKGD_COLOR, XOR */
    color_w(0xa6e8, color());
    color_w(0xa2e8, bg);
    port_w(0xbee8, 0xa080); /* PIX_CNTL: CPU data selects the mix */
    xfer_start(13, CMD_XFER | CMD_PX_MD | (bus << 9));
    pix_trans(data, len, unit, mmio);
    check_image(13, old, (bus == 3) ? "across 11b" : "across", unit, mmio);
}

static void
test_xfer_across(void)
{
    for (int unit = 1; unit <= 4; unit *= 2) {
        if ((unit == 4) && !(cur_chip->bit & C_BUS32))
            continue;
        xfer_across(unit, 0, unit / 2);
        if ((unit > 1) && (cur_chip->bit & C_MMIO))
            xfer_across(unit, 1, unit / 2);
    }
}

static void
test_xfer_across_11b(void)
{
    xfer_across(4, 0, 3);
    if (cur_chip->bit & C_MMIO)
        xfer_across(4, 1, 3);
}

/* Where an image transfer leaves the drawing position: with bus size 10b "one pixel below the
   lower left hand corner of the image rectangle", with 11b "one pixel to the right of the top
   right corner" (Trio32/64 p. 18-12, Trio64V+ p. 20-9, Vision868 and Vision968 p. 2-3). The books
   don't say it for 8 and 16 bits; a real Trio64V+ ends 16-bit transfers like 10b ones. */
static void
check_xfer_end(int width, int bus, const char *what, int mmio)
{
    int x = (bus == 3) ? (XX + width) : XX;
    int y = (bus == 3) ? XY : (XY + XH);

    check((port_r(0x86e8) == x) && (port_r(0x82e8) == y), "%s %s: CUR_X/CUR_Y end at %u,%u, not %d,%d", what,
          mmio ? "MMIO" : "port", port_r(0x86e8), port_r(0x82e8), x, y);
}

static void
xfer_end(int unit, int bus)
{
    for (int mmio = 0; mmio <= !!(cur_chip->bit & C_MMIO); mmio++) {
        if (bus != 3) {
            xfer_through(unit, mmio);
            check_xfer_end(5, bus, "through", mmio);
        }
        xfer_across(unit, mmio, bus);
        check_xfer_end(13, bus, "across", mmio);
    }
}

static void
test_xfer_end_16(void)
{
    xfer_end(2, 1);
}

static void
test_xfer_end_10b(void)
{
    if (cur_chip->bit & C_BUS32)
        xfer_end(4, 2);
}

static void
test_xfer_end_11b(void)
{
    xfer_end(4, 3);
}

/* Chips with a databook here, whose BitBLT examples the BitBLT tests encode. */
#define C_BOOK (C_928 | C_928PCI | C_864 | C_964 | C_868 | C_968 | C_TRIO32 | C_TRIO64 | C_TRIO64V | C_TRIO64V2)

/* BitBLT, draw, write, X major (bit 6 = 0); bits 7 and 5 are the Y and X directions. */
#define CMD_BLT 0xc011

/* What the screen holds around the BitBLT sources. */
#define BLT_OLD 0x5a5a5a5a

/* The bitmap the BitBLT tests copy from. */
static uint32_t
blt_pixel(int x, int y)
{
    return ((0x2545f491 * (uint32_t) (y * 1024 + x + 7)) >> 3) & depth_mask();
}

static void
pix_w(int x, int y, uint32_t val)
{
    uint32_t addr = (y * s3.width + x) * (cur_bpp / 8);

    for (int b = 0; b < cur_bpp / 8; b++)
        vram[addr + b] = val >> (b * 8);
}

/* A w x h source of blt_pixel() at (x, y) on a screen of BLT_OLD. */
static void
blt_source(int x, int y, int w, int h)
{
    memset(vram, 0x5a, sizeof(vram));
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            pix_w(x + i, y + j, blt_pixel(x + i, y + j));
}

/* BitBLT as in the books' example: (x1, y1) and (x2, y2) are the top left corners of the source and
   the destination; for X negative both start at x + WIDTH - 1, for Y negative at y + HEIGHT - 1. */
static void
blt(int x1, int y1, int x2, int y2, int w, int h, int xpos, int ypos, uint16_t extra)
{
    if (!xpos) {
        x1 += w - 1;
        x2 += w - 1;
    }
    if (!ypos) {
        y1 += h - 1;
        y2 += h - 1;
    }
    port_w(0x86e8, x1); /* CUR_X: the source */
    port_w(0x82e8, y1); /* CUR_Y */
    port_w(0x8ee8, x2); /* DESTX_DISTP: the destination */
    port_w(0x8ae8, y2); /* DESTY_AXSTP */
    port_w(0x96e8, w - 1);
    port_w(0xbee8, 0x0000 | (h - 1));
    port_w(0x9ae8, CMD_BLT | (xpos ? 0x20 : 0) | (ypos ? 0x80 : 0) | extra);
}

/* The expected pixel after a copy of the w x h source at (x1, y1) to (x2, y2), with `dst` the value
   of a copied pixel: the destination holds the source as it was before, the rest of the source is
   unchanged, everything else still BLT_OLD. */
static uint32_t
blt_want(int x, int y, int x1, int y1, int x2, int y2, int w, int h, uint32_t dst)
{
    if ((x >= x2) && (x < x2 + w) && (y >= y2) && (y < y2 + h))
        return dst;
    if ((x >= x1) && (x < x1 + w) && (y >= y1) && (y < y1 + h))
        return blt_pixel(x, y);
    return BLT_OLD & depth_mask();
}

/* Check the copy over both rectangles and the ring around them. */
static void
check_blt(int x1, int y1, int x2, int y2, int w, int h, const char *what)
{
    int bad = 0;

    for (int y = MIN(y1, y2) - 1; y <= MAX(y1, y2) + h; y++) {
        for (int x = MIN(x1, x2) - 1; x <= MAX(x1, x2) + w; x++) {
            uint32_t want = blt_want(x, y, x1, y1, x2, y2, w, h, blt_pixel(x - x2 + x1, y - y2 + y1));

            if ((pix(x, y) != want) && !bad++)
                check(0, "%s: pixel (%d, %d) is %X, not %X", what, x, y, pix(x, y), want);
        }
    }
}

static const char *const dir_names[4] = { "-X-Y", "+X-Y", "-X+Y", "+X+Y" };

/* BitBLT through the plane: display memory as the colour source and FRGD_MIX NEW (0067h) copy the
   source rectangle (86C928 11.3.8, p. 11-10; Vision864 and Vision964 11.3.3.6, p. 11-15;
   Vision868 and Vision968 5.4.3.6, p. 5-19; Trio32/64 13.3.3.6, p. 13-17; Trio64V+ 15.4.3.6,
   p. 15-17). The 86C928 book writes the X negative start as x1 - WIDTH - 1, the later books as
   x1 + WIDTH - 1. With PIX_CNTL A000h FRGD_MIX always selects, so BKGD_MIX doesn't matter: each
   copy runs with it left at "leave" (0003h) and at NEW (0007h). */
static void
blt_copy(int x1, int y1, int x2, int y2, int w, int h, int xpos, int ypos, const char *what)
{
    char name[48];

    for (int i = 0; i < 2; i++) {
        uint16_t bkgd_mix = i ? 0x0007 : 0x0003;

        blt_source(x1, y1, w, h);
        port_w(0xbee8, 0xa000); /* PIX_CNTL: FRGD_MIX selects */
        port_w(0xbae8, 0x0067); /* FRGD_MIX: display memory, NEW */
        port_w(0xb6e8, bkgd_mix);
        blt(x1, y1, x2, y2, w, h, xpos, ypos, 0);
        snprintf(name, sizeof(name), "%s, BKGD_MIX %04X", what, bkgd_mix);
        check_blt(x1, y1, x2, y2, w, h, name);
    }
}

/* Disjoint rectangles, each direction. */
static void
test_blt_dirs(void)
{
    char what[32];

    for (int d = 0; d < 4; d++) {
        snprintf(what, sizeof(what), "disjoint %s", dir_names[d]);
        blt_copy(20, 10, 60, 30, 9, 5, d & 1, d & 2, what);
    }
}

/* Overlapping rectangles: "If x1 > x2 then X Positive, else X Negative", the same for Y (the books'
   case 2, same pages). Shifts by one pixel, by several and diagonally. */
static void
test_blt_overlap(void)
{
    static const int shifts[][2] = {
        { 3,  2  },
        { -3, 2  },
        { 3,  -2 },
        { -3, -2 },
        { 1,  0  },
        { -1, 0  },
        { 8,  0  },
        { -8, 0  },
        { 0,  1  },
        { 0,  -1 }
    };
    char what[32];

    for (size_t i = 0; i < sizeof(shifts) / sizeof(shifts[0]); i++) {
        int x2 = 30 + shifts[i][0];
        int y2 = 20 + shifts[i][1];

        snprintf(what, sizeof(what), "overlap %+d,%+d", shifts[i][0], shifts[i][1]);
        blt_copy(30, 20, x2, y2, 12, 5, 30 >= x2, 20 >= y2, what);
    }
}

/* A BitBLT goes through the mix and the write mask like any drawing (FRGD_MIX bits 3-0 and WRT_MASK,
   same pages as in test_mixes() and test_wrt_mask()): XOR with the destination, and NEW with a
   write mask in both directions. The scissors clip the destination, not the source (as in
   test_rect_scissors()). */
static void
test_blt_mix_mask(void)
{
    uint32_t mask = 0x0ff0f03c & depth_mask();
    uint32_t old  = BLT_OLD & depth_mask();
    int      bad  = 0;

    blt_source(20, 10, 9, 5);
    port_w(0xbee8, 0xa000);
    port_w(0xbae8, 0x0065); /* FRGD_MIX: display memory, XOR */
    blt(20, 10, 60, 30, 9, 5, 1, 1, 0);
    for (int y = 29; y <= 35; y++)
        for (int x = 59; x <= 69; x++) {
            uint32_t want = blt_want(x, y, 20, 10, 60, 30, 9, 5, blt_pixel(x - 40, y - 20) ^ old);

            if ((pix(x, y) != want) && !bad++)
                check(0, "XOR: pixel (%d, %d) is %X, not %X", x, y, pix(x, y), want);
        }

    for (int d = 0; d < 4; d += 3) {
        bad = 0;
        blt_source(20, 10, 9, 5);
        color_w(0xaae8, mask);
        port_w(0xbae8, 0x0067);
        port_w(0xb6e8, 0x0007); /* BKGD_MIX NEW too, as a driver may leave it */
        blt(20, 10, 60, 30, 9, 5, d & 1, d & 2, 0);
        for (int y = 29; y <= 35; y++)
            for (int x = 59; x <= 69; x++) {
                uint32_t src  = blt_pixel(x - 40, y - 20);
                uint32_t want = blt_want(x, y, 20, 10, 60, 30, 9, 5, (src & mask) | (old & ~mask));

                if ((pix(x, y) != want) && !bad++)
                    check(0, "WRT_MASK %s: pixel (%d, %d) is %X, not %X", dir_names[d], x, y, pix(x, y), want);
            }
    }

    bad = 0;
    blt_source(20, 10, 9, 5);
    color_w(0xaae8, 0xffffffff);
    port_w(0xbee8, 0x1000 | 31); /* SCISSORS_T */
    port_w(0xbee8, 0x2000 | 62); /* SCISSORS_L */
    port_w(0xbee8, 0x3000 | 32); /* SCISSORS_B */
    port_w(0xbee8, 0x4000 | 65); /* SCISSORS_R */
    blt(20, 10, 60, 30, 9, 5, 1, 1, 0);
    for (int y = 29; y <= 35; y++)
        for (int x = 59; x <= 69; x++) {
            int      in   = (x >= 62) && (x <= 65) && (y >= 31) && (y <= 32);
            uint32_t want = in ? blt_pixel(x - 40, y - 20) : old;

            if ((pix(x, y) != want) && !bad++)
                check(0, "scissors: pixel (%d, %d) is %X, not %X", x, y, pix(x, y), want);
        }
}

/* BitBLT across the plane: PIX_CNTL A0C0h lets display memory select the mix. A pixel of the source
   reads as 1 if all its bits enabled in RD_MASK are 1s (86C928 11.3.9, p. 11-11; Vision864 and
   Vision964 11.3.3.7, p. 11-16; Vision868 and Vision968 5.4.3.7, p. 5-20; Trio32/64 13.3.3.7,
   pp. 13-18 and 13-19; Trio64V+ 15.4.3.7, p. 15-18). First the books' example: plane 0 read, plane 2
   written, FRGD_MIX logical 1, BKGD_MIX logical 0. Then their note: a 2-colour image from the
   source bits, here read from planes 0 and 1. CMD bit 1 (multi-pixel) set as in the example. */
static void
blt_across(uint32_t rd, uint32_t wrt, uint16_t fmix, uint16_t bmix, uint32_t fg, uint32_t bg, const char *what)
{
    int bad = 0;

    blt_source(20, 10, 9, 5);
    color_w(0xa6e8, fg);
    color_w(0xa2e8, bg);
    color_w(0xaee8, rd);
    color_w(0xaae8, wrt);
    port_w(0xbee8, 0xa0c0); /* PIX_CNTL: display memory selects the mix */
    port_w(0xbae8, fmix);
    port_w(0xb6e8, bmix);
    blt(20, 10, 60, 30, 9, 5, 1, 1, CMD_PX_MD);
    for (int y = 29; y <= 35; y++)
        for (int x = 59; x <= 69; x++) {
            uint32_t bit  = (blt_pixel(x - 40, y - 20) & rd) == rd;
            uint32_t new  = (fmix == 0x0002) ? (bit ? ~0 : 0) : (bit ? fg : bg);
            uint32_t old  = BLT_OLD & depth_mask();
            uint32_t want = blt_want(x, y, 20, 10, 60, 30, 9, 5, ((new & wrt) | (old & ~wrt)) & depth_mask());

            if ((pix(x, y) != want) && !bad++)
                check(0, "%s: pixel (%d, %d) is %X, not %X", what, x, y, pix(x, y), want);
        }
}

static void
test_blt_across(void)
{
    blt_across(0x0001, 0x0004, 0x0002, 0x0001, 0, 0, "plane 0 to plane 2");
    blt_across(0x0003, 0xffffffff, 0x0027, 0x0007, color(), ~color() & depth_mask(), "planes 0+1, 2 colours");
}

/* Colour compare (MULT_MISC, BEE8h index E, bit 8): with SRC NE (bit 7) = 0 the destination isn't
   updated where the source equals COLOR_CMP (B2E8h), with SRC NE = 1 it isn't updated where they
   differ. In 32 bpp COLOR_CMP is two 16-bit registers, low half first (86C928 p. 10-18;
   Vision864 and Vision964 p. 18-18; Vision868 and Vision968 pp. 5-5 and 5-6; Trio32/64 pp. 18-23
   and 18-24, and 13.2, p. 13-2; Trio64V+ p. 20-18). A source of two colours over a destination
   that holds the bitmap. */
static void
test_blt_color_cmp(void)
{
    uint32_t a = 0x00123456 & depth_mask();
    uint32_t b = 0x00abcdef & depth_mask();

    for (int srcne = 0; srcne <= 1; srcne++) {
        int bad = 0;

        blt_source(60, 30, 9, 5);
        for (int y = 10; y < 15; y++)
            for (int x = 20; x < 29; x++)
                pix_w(x, y, ((x > 21) && (x < 26) && (y > 10) && (y < 14)) ? b : a);
        port_w(0xbee8, 0xe100 | (srcne << 7)); /* MULT_MISC: compare on, RSF = 0 */
        color_w(0xb2e8, a);                    /* COLOR_CMP */
        port_w(0xbee8, 0xa000);
        port_w(0xbae8, 0x0067);
        blt(20, 10, 60, 30, 9, 5, 1, 1, 0);
        for (int y = 30; y < 35; y++)
            for (int x = 60; x < 69; x++) {
                uint32_t src  = (((x - 40) > 21) && ((x - 40) < 26) && ((y - 20) > 10) && ((y - 20) < 14)) ? b : a;
                int      keep = srcne ? (src != a) : (src == a);
                uint32_t want = keep ? blt_pixel(x, y) : src;

                if ((pix(x, y) != want) && !bad++)
                    check(0, "SRC NE %d: pixel (%d, %d) is %X, not %X", srcne, x, y, pix(x, y), want);
            }
    }
}

/* Where a BitBLT leaves the position registers. The books don't say; a real Trio64V+ (rev 44) leaves
   CUR_X/CUR_Y at the source start, DESTX_DISTP at the destination start X and DESTY_AXSTP one line
   past the destination's last line, in each direction. */
static void
test_blt_end(void)
{
    for (int d = 0; d < 4; d++) {
        int xpos = d & 1, ypos = d & 2;
        int sx = xpos ? 20 : 28, sy = ypos ? 10 : 14;
        int dx = xpos ? 60 : 68, dy = ypos ? 30 : 34;

        blt_copy(20, 10, 60, 30, 9, 5, xpos, ypos, dir_names[d]);
        check((port_r(0x86e8) == sx) && (port_r(0x82e8) == sy), "%s: CUR_X/CUR_Y end at %u,%u, not %d,%d",
              dir_names[d], port_r(0x86e8), port_r(0x82e8), sx, sy);
        check((port_r(0x8ee8) == dx) && (port_r(0x8ae8) == (ypos ? dy + 5 : dy - 5)),
              "%s: DESTX/DESTY end at %u,%u, not %d,%d", dir_names[d], port_r(0x8ee8), port_r(0x8ae8), dx,
              ypos ? dy + 5 : dy - 5);
    }
}

/* RSF (MULT_MISC, BEE8h index E, bit 4) selects the half of a 32-bit register "in 32 bpp mode"
   (86C928 p. 10-18, Vision864 and Vision964 p. 18-18, Trio32/64 p. 18-23, Trio64V+ p. 20-18) and
   toggles with each 16-bit access in that mode (Trio32/64 p. 18-16). At 8 and 16 bpp it stays 0: a real
   Trio64V+ (rev 44) reads MULT_MISC back through READ_SEL with RSF 0 after each colour write there. */
static void
test_rsf(void)
{
    for (int n = 1; n <= 3; n++) {
        int want = (cur_bpp == 32) && (n & 1);
        int rsf;

        port_w(0xa6e8, 0x1234); /* One 16-bit FRGD_COLOR write */
        port_w(0xbee8, 0xf006); /* READ_SEL: MULT_MISC */
        rsf = !!(port_r(0xbee8) & 0x10);
        check(rsf == want, "RSF after %d colour writes is %d, not %d", n, rsf, want);
    }
}

int
main(int argc, char **argv)
{
    verbose = (argc > 1) && !strcmp(argv[1], "-v");

    run("cur_xy", test_cur_xy, C_ALL);
    run("bee8_port", test_bee8_port, C_ALL);
    run("bee8_mmio32", test_bee8_mmio32,
        C_928 | C_928PCI | C_864 | C_868 | C_964 | C_968 | C_TRIO32 | C_TRIO64 | C_TRIO64V | C_TRIO64V2);
    run("packed_mmio", test_packed_mmio, C_868 | C_968 | C_TRIO32 | C_TRIO64 | C_TRIO64V | C_TRIO64V2);
    run("rect_scissors", test_rect_scissors, C_ALL);
    run("mixes", test_mixes, C_ALL);
    run("wrt_mask", test_wrt_mask, C_ALL);
    run("xfer_through", test_xfer_through, C_ALL);
    run("xfer_across", test_xfer_across, C_ALL);
    run("xfer_across_11b", test_xfer_across_11b, C_TRIO32 | C_868 | C_968 | C_TRIO64V | C_TRIO64V2);
    run("xfer_end_16", test_xfer_end_16, C_TRIO64V);
    run("xfer_end_10b", test_xfer_end_10b, C_TRIO32 | C_TRIO64 | C_868 | C_968 | C_TRIO64V | C_TRIO64V2);
    run("xfer_end_11b", test_xfer_end_11b, C_TRIO32 | C_868 | C_968 | C_TRIO64V | C_TRIO64V2);
    run("blt_dirs", test_blt_dirs, C_BOOK);
    run("blt_overlap", test_blt_overlap, C_BOOK);
    run("blt_mix_mask", test_blt_mix_mask, C_BOOK);
    run("blt_across", test_blt_across, C_BOOK);
    run("blt_color_cmp", test_blt_color_cmp, C_BOOK);
    run("blt_end", test_blt_end, C_TRIO64V);
    run("rsf", test_rsf, C_BOOK);

    if (verbose)
        print_matrix();
    if (failures)
        fprintf(stderr, "%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
