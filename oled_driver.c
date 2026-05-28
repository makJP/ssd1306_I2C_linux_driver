/******************************************************************************
 * oled_driver.c
 *
 * Linux I2C kernel driver for SSD1306 OLED display (128x64)
 * Tested on Raspberry Pi - Linux 5.4.51-v7l+
 *
 *****************************************************************************/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/kernel.h>

/* -------------------------------------------------------------------------
 * Macros / Constants
 * ---------------------------------------------------------------------- */
#define I2C_BUS_NUM             1               /* I2C bus index on RPi     */
#define OLED_DEVICE_NAME        "my_oled"       /* driver/device name       */
#define OLED_I2C_ADDR           0x3C            /* SSD1306 slave address    */
#define OLED_COLS               128             /* display width in pixels  */
#define OLED_PAGES              7               /* last page index (0..7)   */
#define FONT_W                  5               /* glyph width in bytes     */

/* -------------------------------------------------------------------------
 * Forward declarations
 * ---------------------------------------------------------------------- */
static int  oled_i2c_read(unsigned char *buf, unsigned int len);
static int  oled_i2c_write(unsigned char *buf, unsigned int len);
static void oled_send(bool cmd_mode, unsigned char byte);
static void oled_set_pos(uint8_t page, uint8_t col);
static void oled_newline(void);
static void oled_putchar(unsigned char ch);
static void oled_puts(unsigned char *str);
static void oled_invert(bool invert);
static void oled_set_contrast(uint8_t val);
static void oled_scroll_h(bool go_left, uint8_t pg_start, uint8_t pg_end);
static void oled_scroll_vh(bool go_left, uint8_t pg_start, uint8_t pg_end,
                           uint8_t v_area, uint8_t row_offset);
static int  oled_init_display(void);
static void oled_fill(unsigned char fill_byte);

/* -------------------------------------------------------------------------
 * I2C handles
 * ---------------------------------------------------------------------- */
static struct i2c_adapter *oled_adapter = NULL;
static struct i2c_client  *oled_client  = NULL;

/* -------------------------------------------------------------------------
 * Display state
 * ---------------------------------------------------------------------- */
static uint8_t cur_page   = 0;
static uint8_t cur_col    = 0;
static uint8_t glyph_w    = FONT_W;

/* -------------------------------------------------------------------------
 * 5x7 font table  (ASCII 0x20 .. 0x7E)
 * ---------------------------------------------------------------------- */
static const unsigned char font5x7[][FONT_W] =
{
    {0x00, 0x00, 0x00, 0x00, 0x00},   /* (space) */
    {0x00, 0x00, 0x2f, 0x00, 0x00},   /* !  */
    {0x00, 0x07, 0x00, 0x07, 0x00},   /* "  */
    {0x14, 0x7f, 0x14, 0x7f, 0x14},   /* #  */
    {0x24, 0x2a, 0x7f, 0x2a, 0x12},   /* $  */
    {0x23, 0x13, 0x08, 0x64, 0x62},   /* %  */
    {0x36, 0x49, 0x55, 0x22, 0x50},   /* &  */
    {0x00, 0x05, 0x03, 0x00, 0x00},   /* '  */
    {0x00, 0x1c, 0x22, 0x41, 0x00},   /* (  */
    {0x00, 0x41, 0x22, 0x1c, 0x00},   /* )  */
    {0x14, 0x08, 0x3E, 0x08, 0x14},   /* *  */
    {0x08, 0x08, 0x3E, 0x08, 0x08},   /* +  */
    {0x00, 0x00, 0xA0, 0x60, 0x00},   /* ,  */
    {0x08, 0x08, 0x08, 0x08, 0x08},   /* -  */
    {0x00, 0x60, 0x60, 0x00, 0x00},   /* .  */
    {0x20, 0x10, 0x08, 0x04, 0x02},   /* /  */

    {0x3E, 0x51, 0x49, 0x45, 0x3E},   /* 0  */
    {0x00, 0x42, 0x7F, 0x40, 0x00},   /* 1  */
    {0x42, 0x61, 0x51, 0x49, 0x46},   /* 2  */
    {0x21, 0x41, 0x45, 0x4B, 0x31},   /* 3  */
    {0x18, 0x14, 0x12, 0x7F, 0x10},   /* 4  */
    {0x27, 0x45, 0x45, 0x45, 0x39},   /* 5  */
    {0x3C, 0x4A, 0x49, 0x49, 0x30},   /* 6  */
    {0x01, 0x71, 0x09, 0x05, 0x03},   /* 7  */
    {0x36, 0x49, 0x49, 0x49, 0x36},   /* 8  */
    {0x06, 0x49, 0x49, 0x29, 0x1E},   /* 9  */

    {0x00, 0x36, 0x36, 0x00, 0x00},   /* :  */
    {0x00, 0x56, 0x36, 0x00, 0x00},   /* ;  */
    {0x08, 0x14, 0x22, 0x41, 0x00},   /* <  */
    {0x14, 0x14, 0x14, 0x14, 0x14},   /* =  */
    {0x00, 0x41, 0x22, 0x14, 0x08},   /* >  */
    {0x02, 0x01, 0x51, 0x09, 0x06},   /* ?  */
    {0x32, 0x49, 0x59, 0x51, 0x3E},   /* @  */

    {0x7C, 0x12, 0x11, 0x12, 0x7C},   /* A  */
    {0x7F, 0x49, 0x49, 0x49, 0x36},   /* B  */
    {0x3E, 0x41, 0x41, 0x41, 0x22},   /* C  */
    {0x7F, 0x41, 0x41, 0x22, 0x1C},   /* D  */
    {0x7F, 0x49, 0x49, 0x49, 0x41},   /* E  */
    {0x7F, 0x09, 0x09, 0x09, 0x01},   /* F  */
    {0x3E, 0x41, 0x49, 0x49, 0x7A},   /* G  */
    {0x7F, 0x08, 0x08, 0x08, 0x7F},   /* H  */
    {0x00, 0x41, 0x7F, 0x41, 0x00},   /* I  */
    {0x20, 0x40, 0x41, 0x3F, 0x01},   /* J  */
    {0x7F, 0x08, 0x14, 0x22, 0x41},   /* K  */
    {0x7F, 0x40, 0x40, 0x40, 0x40},   /* L  */
    {0x7F, 0x02, 0x0C, 0x02, 0x7F},   /* M  */
    {0x7F, 0x04, 0x08, 0x10, 0x7F},   /* N  */
    {0x3E, 0x41, 0x41, 0x41, 0x3E},   /* O  */
    {0x7F, 0x09, 0x09, 0x09, 0x06},   /* P  */
    {0x3E, 0x41, 0x51, 0x21, 0x5E},   /* Q  */
    {0x7F, 0x09, 0x19, 0x29, 0x46},   /* R  */
    {0x46, 0x49, 0x49, 0x49, 0x31},   /* S  */
    {0x01, 0x01, 0x7F, 0x01, 0x01},   /* T  */
    {0x3F, 0x40, 0x40, 0x40, 0x3F},   /* U  */
    {0x1F, 0x20, 0x40, 0x20, 0x1F},   /* V  */
    {0x3F, 0x40, 0x38, 0x40, 0x3F},   /* W  */
    {0x63, 0x14, 0x08, 0x14, 0x63},   /* X  */
    {0x07, 0x08, 0x70, 0x08, 0x07},   /* Y  */
    {0x61, 0x51, 0x49, 0x45, 0x43},   /* Z  */

    {0x00, 0x7F, 0x41, 0x41, 0x00},   /* [  */
    {0x55, 0xAA, 0x55, 0xAA, 0x55},   /* \  (checker) */
    {0x00, 0x41, 0x41, 0x7F, 0x00},   /* ]  */
    {0x04, 0x02, 0x01, 0x02, 0x04},   /* ^  */
    {0x40, 0x40, 0x40, 0x40, 0x40},   /* _  */
    {0x00, 0x03, 0x05, 0x00, 0x00},   /* `  */

    {0x20, 0x54, 0x54, 0x54, 0x78},   /* a  */
    {0x7F, 0x48, 0x44, 0x44, 0x38},   /* b  */
    {0x38, 0x44, 0x44, 0x44, 0x20},   /* c  */
    {0x38, 0x44, 0x44, 0x48, 0x7F},   /* d  */
    {0x38, 0x54, 0x54, 0x54, 0x18},   /* e  */
    {0x08, 0x7E, 0x09, 0x01, 0x02},   /* f  */
    {0x18, 0xA4, 0xA4, 0xA4, 0x7C},   /* g  */
    {0x7F, 0x08, 0x04, 0x04, 0x78},   /* h  */
    {0x00, 0x44, 0x7D, 0x40, 0x00},   /* i  */
    {0x40, 0x80, 0x84, 0x7D, 0x00},   /* j  */
    {0x7F, 0x10, 0x28, 0x44, 0x00},   /* k  */
    {0x00, 0x41, 0x7F, 0x40, 0x00},   /* l  */
    {0x7C, 0x04, 0x18, 0x04, 0x78},   /* m  */
    {0x7C, 0x08, 0x04, 0x04, 0x78},   /* n  */
    {0x38, 0x44, 0x44, 0x44, 0x38},   /* o  */
    {0xFC, 0x24, 0x24, 0x24, 0x18},   /* p  */
    {0x18, 0x24, 0x24, 0x18, 0xFC},   /* q  */
    {0x7C, 0x08, 0x04, 0x04, 0x08},   /* r  */
    {0x48, 0x54, 0x54, 0x54, 0x20},   /* s  */
    {0x04, 0x3F, 0x44, 0x40, 0x20},   /* t  */
    {0x3C, 0x40, 0x40, 0x20, 0x7C},   /* u  */
    {0x1C, 0x20, 0x40, 0x20, 0x1C},   /* v  */
    {0x3C, 0x40, 0x30, 0x40, 0x3C},   /* w  */
    {0x44, 0x28, 0x10, 0x28, 0x44},   /* x  */
    {0x1C, 0xA0, 0xA0, 0xA0, 0x7C},   /* y  */
    {0x44, 0x64, 0x54, 0x4C, 0x44},   /* z  */

    {0x00, 0x10, 0x7C, 0x82, 0x00},   /* {  */
    {0x00, 0x00, 0xFF, 0x00, 0x00},   /* |  */
    {0x00, 0x82, 0x7C, 0x10, 0x00},   /* }  */
    {0x00, 0x06, 0x09, 0x09, 0x06},   /* ~  (degree symbol) */
};

/* =========================================================================
 * Low-level I2C helpers
 * ====================================================================== */

/*
 * oled_i2c_write - push 'len' bytes from 'buf' to the OLED over I2C.
 * Returns number of bytes sent, or a negative errno on failure.
 */
static int oled_i2c_write(unsigned char *buf, unsigned int len)
{
    return i2c_master_send(oled_client, buf, len);
}

/*
 * oled_i2c_read - read 'len' bytes from the OLED into 'buf'.
 * Returns number of bytes received, or a negative errno on failure.
 */
static int oled_i2c_read(unsigned char *buf, unsigned int len)
{
    return i2c_master_recv(oled_client, buf, len);
}

/* =========================================================================
 * SSD1306 protocol layer
 * ====================================================================== */

/*
 * oled_send - send a single command byte or data byte to the SSD1306.
 *
 * The SSD1306 I2C protocol prefixes every transfer with a control byte:
 *   0x00 -> next byte is a command
 *   0x40 -> next byte is display RAM data
 */
static void oled_send(bool cmd_mode, unsigned char byte)
{
    unsigned char pkt[2];

    pkt[0] = cmd_mode ? 0x00 : 0x40;
    pkt[1] = byte;

    oled_i2c_write(pkt, 2);
}

/* =========================================================================
 * Cursor / positioning
 * ====================================================================== */

/*
 * oled_set_pos - move the hardware pointer to (page, col).
 * page : 0 .. OLED_PAGES
 * col  : 0 .. OLED_COLS-1
 */
static void oled_set_pos(uint8_t page, uint8_t col)
{
    if (page > OLED_PAGES || col >= OLED_COLS)
        return;

    cur_page = page;
    cur_col  = col;

    oled_send(true, 0x21);           /* set column address      */
    oled_send(true, col);            /* column start            */
    oled_send(true, OLED_COLS - 1);  /* column end              */

    oled_send(true, 0x22);           /* set page address        */
    oled_send(true, page);           /* page start              */
    oled_send(true, OLED_PAGES);     /* page end                */
}

/*
 * oled_newline - advance to the start of the next page, wrapping around.
 */
static void oled_newline(void)
{
    cur_page = (cur_page + 1) & OLED_PAGES;
    oled_set_pos(cur_page, 0);
}

/* =========================================================================
 * Text rendering
 * ====================================================================== */

/*
 * oled_putchar - render one ASCII character at the current cursor position.
 * Automatically wraps to the next line when the column would overflow,
 * and handles '\n' as an explicit line-feed.
 */
static void oled_putchar(unsigned char ch)
{
    uint8_t col_data;
    uint8_t idx = 0;

    /* wrap or explicit newline */
    if ((cur_col + glyph_w >= OLED_COLS) || (ch == '\n'))
        oled_newline();

    if (ch == '\n')
        return;

    /* map ASCII value to font table index (table starts at 0x20 / space) */
    ch -= 0x20;

    do {
        col_data = font5x7[ch][idx];
        oled_send(false, col_data);
        cur_col++;
        idx++;
    } while (idx < glyph_w);

    /* one blank column spacer between glyphs */
    oled_send(false, 0x00);
    cur_col++;
}

/*
 * oled_puts - write a null-terminated string to the display.
 */
static void oled_puts(unsigned char *str)
{
    while (*str)
        oled_putchar(*str++);
}

/* =========================================================================
 * Display-level controls
 * ====================================================================== */

/*
 * oled_invert - flip every pixel on the display (or restore normal mode).
 */
static void oled_invert(bool invert)
{
    oled_send(true, invert ? 0xA7 : 0xA6);
}

/*
 * oled_set_contrast - adjust display brightness (0x00 = dim, 0xFF = bright).
 */
static void oled_set_contrast(uint8_t val)
{
    oled_send(true, 0x81);  /* contrast command */
    oled_send(true, val);   /* contrast value   */
}

/*
 * oled_scroll_h - start a continuous horizontal scroll on a page range.
 * go_left  : true = scroll left, false = scroll right
 * pg_start : first page to scroll
 * pg_end   : last  page to scroll
 */
static void oled_scroll_h(bool go_left,
                          uint8_t pg_start,
                          uint8_t pg_end)
{
    oled_send(true, go_left ? 0x27 : 0x26);  /* scroll direction      */
    oled_send(true, 0x00);                    /* dummy byte            */
    oled_send(true, pg_start);               /* start page            */
    oled_send(true, 0x00);                   /* frame interval: 5f    */
    oled_send(true, pg_end);                 /* end page              */
    oled_send(true, 0x00);                   /* dummy byte            */
    oled_send(true, 0xFF);                   /* dummy byte            */
    oled_send(true, 0x2F);                   /* activate scroll       */
}

/*
 * oled_scroll_vh - diagonal (vertical + horizontal) scroll on a page range.
 * go_left     : true = left+up, false = right+up
 * pg_start    : first page to scroll
 * pg_end      : last  page to scroll
 * v_area      : number of rows in vertical scroll area (0..63)
 * row_offset  : vertical offset per step
 */
static void oled_scroll_vh(bool go_left,
                           uint8_t pg_start,
                           uint8_t pg_end,
                           uint8_t v_area,
                           uint8_t row_offset)
{
    oled_send(true, 0xA3);       /* set vertical scroll area  */
    oled_send(true, 0x00);       /* fixed rows (top)          */
    oled_send(true, v_area);     /* scrolling rows            */

    oled_send(true, go_left ? 0x2A : 0x29);  /* direction    */
    oled_send(true, 0x00);                    /* dummy byte   */
    oled_send(true, pg_start);               /* start page   */
    oled_send(true, 0x00);                   /* 5f interval  */
    oled_send(true, pg_end);                 /* end page     */
    oled_send(true, row_offset);             /* v offset     */
    oled_send(true, 0x2F);                   /* activate     */
}

/* =========================================================================
 * Initialisation & fill
 * ====================================================================== */

/*
 * oled_fill - flood every pixel with the same byte value.
 * Use 0x00 to clear, 0xFF for all-on.
 */
static void oled_fill(unsigned char fill_byte)
{
    unsigned int px;
    unsigned int total_px = OLED_COLS * (OLED_PAGES + 1); /* 128 * 8 = 1024 */

    for (px = 0; px < total_px; px++)
        oled_send(false, fill_byte);
}

/*
 * oled_init_display - send the standard SSD1306 startup command sequence
 * and clear the framebuffer.
 */
static int oled_init_display(void)
{
    msleep(100);  /* let the display power rail settle */

    oled_send(true, 0xAE);  /* display off                                  */
    oled_send(true, 0xD5);  /* set display clock divide / oscillator freq   */
    oled_send(true, 0x80);  /* recommended default                          */
    oled_send(true, 0xA8);  /* set multiplex ratio                          */
    oled_send(true, 0x3F);  /* 64 COM lines                                 */
    oled_send(true, 0xD3);  /* set display offset                           */
    oled_send(true, 0x00);  /* no offset                                    */
    oled_send(true, 0x40);  /* set start line to 0                          */
    oled_send(true, 0x8D);  /* charge pump setting                          */
    oled_send(true, 0x14);  /* enable charge pump                           */
    oled_send(true, 0x20);  /* memory addressing mode                       */
    oled_send(true, 0x00);  /* horizontal addressing mode                   */
    oled_send(true, 0xA1);  /* col 127 -> SEG0 (mirror horizontally)        */
    oled_send(true, 0xC8);  /* COM scan: COM63 -> COM0 (mirror vertically)  */
    oled_send(true, 0xDA);  /* COM pins hardware config                     */
    oled_send(true, 0x12);  /* alternative config, no left/right remap      */
    oled_send(true, 0x81);  /* set contrast                                 */
    oled_send(true, 0x80);  /* contrast = 128                               */
    oled_send(true, 0xD9);  /* set pre-charge period                        */
    oled_send(true, 0xF1);  /* phase1 = 15 DCLK, phase2 = 1 DCLK           */
    oled_send(true, 0xDB);  /* set VCOMH deselect level                     */
    oled_send(true, 0x20);  /* ~0.77 × Vcc                                  */
    oled_send(true, 0xA4);  /* resume display from RAM                      */
    oled_send(true, 0xA6);  /* normal display (1 = lit)                     */
    oled_send(true, 0x2E);  /* stop any active scroll                       */
    oled_send(true, 0xAF);  /* display on                                   */

    oled_fill(0x00);  /* blank screen */

    return 0;
}

/* =========================================================================
 * I2C driver callbacks
 * ====================================================================== */

/*
 * oled_probe - called once by the I2C core when the device is matched.
 * Initialises the display and shows a welcome message.
 */
static int oled_probe(struct i2c_client *client,
                      const struct i2c_device_id *id)
{
    oled_init_display();

    oled_set_pos(0, 0);
    oled_scroll_h(true, 0, 2);
    oled_puts("Hello\nWorld\nDriver\n\n");

    pr_info("oled_driver: display probed OK\n");
    return 0;
}

/*
 * oled_remove - called once when the driver is unloaded.
 * Shows a goodbye message, then powers the display off.
 */
static int oled_remove(struct i2c_client *client)
{
    oled_puts("Goodbye!");

    msleep(1000);

    oled_set_pos(0, 0);
    oled_fill(0x00);

    oled_send(true, 0xAE);  /* display off */

    pr_info("oled_driver: display removed\n");
    return 0;
}

/* =========================================================================
 * I2C driver registration
 * ====================================================================== */

static const struct i2c_device_id oled_id_table[] = {
    { OLED_DEVICE_NAME, 0 },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, oled_id_table);

static struct i2c_driver oled_i2c_driver = {
    .driver = {
        .name  = OLED_DEVICE_NAME,
        .owner = THIS_MODULE,
    },
    .probe    = oled_probe,
    .remove   = oled_remove,
    .id_table = oled_id_table,
};

static struct i2c_board_info oled_board_info = {
    I2C_BOARD_INFO(OLED_DEVICE_NAME, OLED_I2C_ADDR)
};

/* =========================================================================
 * Module init / exit
 * ====================================================================== */

static int __init oled_driver_init(void)
{
    int rc = -1;

    oled_adapter = i2c_get_adapter(I2C_BUS_NUM);
    if (!oled_adapter) {
        pr_err("oled_driver: could not get I2C adapter %d\n", I2C_BUS_NUM);
        return rc;
    }

    oled_client = i2c_new_device(oled_adapter, &oled_board_info);
    if (oled_client) {
        i2c_add_driver(&oled_i2c_driver);
        rc = 0;
    }

    i2c_put_adapter(oled_adapter);

    pr_info("oled_driver: loaded\n");
    return rc;
}

static void __exit oled_driver_exit(void)
{
    i2c_unregister_device(oled_client);
    i2c_del_driver(&oled_i2c_driver);
    pr_info("oled_driver: unloaded\n");
}

module_init(oled_driver_init);
module_exit(oled_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name <you@example.com>");
MODULE_DESCRIPTION("SSD1306 128x64 OLED I2C driver");
MODULE_VERSION("1.0");
