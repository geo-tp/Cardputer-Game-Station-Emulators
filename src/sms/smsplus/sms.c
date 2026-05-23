
#include "shared.h"
void ym2413_write(int chip, int offset, int data);

/* SMS context */
t_sms sms;

static uint8 coleco_pio_mode = 1; /* 1=joystick mode, 0=keypad mode */
static const uint8 coleco_keymask[12] =
{
    0x7A, /* 0 */
    0x7D, /* 1 */
    0x77, /* 2 */
    0x7C, /* 3 */
    0x72, /* 4 */
    0x73, /* 5 */
    0x7E, /* 6 */
    0x75, /* 7 */
    0x71, /* 8 */
    0x7B, /* 9 */
    0x79, /* * */
    0x76  /* # */
};

/* Allocate SMS RAM */
int sms_init_ram(void)
{
    if (sms.ram) return 1;

    uint8 *wram = (uint8*)malloc(0x2000);
    if (!wram) return 0;

    memset(wram, 0, 0x2000);
    sms.ram = wram;

    return 1;
}

void sms_shutdown_ram(void)
{
    free(sms.ram);
    sms.ram = NULL;
}

/* Run the virtual console emulation for one frame */
void sms_frame(int skip_render)
{
    /* Take care of hard resets */
    if(input.system & INPUT_HARD_RESET)
    {
        system_reset();
    }

    /* Debounce pause key */
    if(input.system & INPUT_PAUSE)
    {
        if(!sms.paused)
        {
            sms.paused = 1;

            z80_set_nmi_line(ASSERT_LINE);
            z80_set_nmi_line(CLEAR_LINE);
        }
    }
    else
    {
         sms.paused = 0;
    }

    if(snd.log) snd.callback(0x00);

    for(vdp.line = 0; vdp.line < vdp.lpf; vdp.line += 1)
    {
        /* Handle VDP line events */
        vdp_run();

        /* Draw the current frame */
        if(!skip_render) render_line(vdp.line);

        /* Run the Z80 for a line */
        z80_execute(227);
    }

    /* Update the emulated sound stream */
    if(snd.enabled) 
    {
/*
        int count;

        SN76496Update(0, snd.psg_buffer, snd.bufsize, sms.psg_mask);

//        if(sms.use_fm)
//        {
//            int i;
//            for(i = 0; i < snd.bufsize; i++)
//            {
//                snd.fm_buffer[i] = OPLL_calc(opll);
//            }
//        }

        for(count = 0; count < snd.bufsize; count += 1)
        {
            signed short left   = 0;
            signed short right  = 0;
//            left = right = snd.fm_buffer[count];
			left=0;
            left  += snd.psg_buffer[0][count];
            right += snd.psg_buffer[1][count];
            snd.buffer[0][count] = left;
            snd.buffer[1][count] = right;
        }
*/
        SN76496Update(0, snd.buffer, snd.bufsize, sms.psg_mask);
    }
}


void sms_init(void)
{   
    cpu_reset();
    sms_reset();
}


void sms_reset(void)
{
    uint8* dummy = sms.dummy ? sms.dummy : sms.ram;

    /* Clear SMS context */
    if (sms.ram) memset(sms.ram, 0, 0x2000);
    if (dummy) memset(dummy, 0, 0x2000);
    //memset(sms.sram, 0, 0x8000);
    sms.paused = sms.save = sms.port_3F = sms.port_F2 = sms.irq = 0x00;
    sms.psg_mask = 0xFF;
    coleco_pio_mode = 1;

    /* Load memory maps with default values */
    if (cart.type == TYPE_SG1000)
    {
        /* 0000-7FFF ROM, 8000-FFFF optional external RAM */
        cpu_readmap[0] = cart.rom + 0x0000;
        cpu_readmap[1] = cart.rom + 0x2000;
        cpu_readmap[2] = cart.rom + 0x4000;
        cpu_readmap[3] = cart.rom + 0x6000;
        cpu_readmap[4] = sms.sram ? sms.sram + 0x0000 : dummy;
        cpu_readmap[5] = sms.sram ? sms.sram + 0x2000 : dummy;
        cpu_readmap[6] = sms.sram ? sms.sram + 0x4000 : (sms.ram ? sms.ram : dummy);
        cpu_readmap[7] = sms.sram ? sms.sram + 0x6000 : (sms.ram ? sms.ram : dummy);

        cpu_writemap[0] = dummy;
        cpu_writemap[1] = dummy;
        cpu_writemap[2] = dummy;
        cpu_writemap[3] = dummy;
        cpu_writemap[4] = sms.sram ? sms.sram + 0x0000 : dummy;
        cpu_writemap[5] = sms.sram ? sms.sram + 0x2000 : dummy;
        cpu_writemap[6] = sms.sram ? sms.sram + 0x4000 : (sms.ram ? sms.ram : dummy);
        cpu_writemap[7] = sms.sram ? sms.sram + 0x6000 : (sms.ram ? sms.ram : dummy);
    }
    else if (cart.type == TYPE_COLECO)
    {
        int p = cart.pages ? cart.pages : 1;

        /* 0000-1FFF BIOS, 2000-5FFF expansion/open bus, 6000-7FFF RAM, 8000-FFFF CART */
        cpu_readmap[0] = sms.coleco_bios ? sms.coleco_bios + 0x0000 : dummy;
        cpu_readmap[1] = dummy;
        cpu_readmap[2] = dummy;
        cpu_readmap[3] = sms.ram ? sms.ram : dummy;
        cpu_readmap[4] = cart.rom + (((0 % p) << 13));
        cpu_readmap[5] = cart.rom + (((1 % p) << 13));
        cpu_readmap[6] = cart.rom + (((2 % p) << 13));
        cpu_readmap[7] = cart.rom + (((3 % p) << 13));

        cpu_writemap[0] = dummy;
        cpu_writemap[1] = dummy;
        cpu_writemap[2] = dummy;
        cpu_writemap[3] = sms.ram ? sms.ram : dummy;
        cpu_writemap[4] = dummy;
        cpu_writemap[5] = dummy;
        cpu_writemap[6] = dummy;
        cpu_writemap[7] = dummy;
    }
    else
    {
        cpu_readmap[0] = cart.rom + 0x0000;
        cpu_readmap[1] = cart.rom + 0x2000;
        cpu_readmap[2] = cart.rom + 0x4000;
        cpu_readmap[3] = cart.rom + 0x6000;
        cpu_readmap[4] = cart.rom + 0x0000;
        cpu_readmap[5] = cart.rom + 0x2000;
        cpu_readmap[6] = sms.ram;
        cpu_readmap[7] = sms.ram;

        cpu_writemap[0] = dummy;
        cpu_writemap[1] = dummy;
        cpu_writemap[2] = dummy;
        cpu_writemap[3] = dummy;
        cpu_writemap[4] = dummy;
        cpu_writemap[5] = dummy;
        cpu_writemap[6] = sms.ram;
        cpu_writemap[7] = sms.ram;
    }

    sms.fcr[0] = 0x00;
    sms.fcr[1] = 0x00;
    sms.fcr[2] = 0x01;
    sms.fcr[3] = 0x00;
}


/* Reset Z80 emulator */
void cpu_reset(void)
{
    z80_reset(0);
    z80_set_irq_callback(sms_irq_callback);
}


/* Write to memory */
void cpu_writemem16(int address, int data)
{
    cpu_writemap[(address >> 13)][(address & 0x1FFF)] = data;
    if(address >= 0xFFFC) sms_mapper_w(address & 3, data);
}

/* Write to an I/O port */
void cpu_writeport(int port, int data)
{
    if (cart.type == TYPE_SG1000)
    {
        switch(port & 0xC0)
        {
            case 0x40: /* SN76489 PSG */
                if(snd.enabled) SN76496Write(0, data);
                break;

            case 0x80: /* TMS VDP */
                if (port & 1) vdp_ctrl_w(data);
                else          vdp_data_w(data);
                break;
        }
        return;
    }

    if (cart.type == TYPE_COLECO)
    {
        switch(port & 0xE0)
        {
            case 0x80: /* Coleco keypad mode select */
                coleco_pio_mode = 0;
                break;

            case 0xA0: /* TMS VDP */
                if (port & 1) vdp_ctrl_w(data);
                else          vdp_data_w(data);
                break;

            case 0xC0: /* Coleco joystick mode select */
                coleco_pio_mode = 1;
                break;

            case 0xE0: /* SN76489 PSG */
                if(snd.enabled) SN76496Write(0, data);
                break;

            default:
                break;
        }
        return;
    }

    switch(port & 0xFF)
    {
        case 0x01: /* GG SIO */
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x05:
            break;

        case 0x06: /* GG STEREO */
            if(snd.log) {
            snd.callback(0x04);
            snd.callback(data);
            }
            sms.psg_mask = (data & 0xFF);
            break;

        case 0x7E: /* SN76489 PSG */
        case 0x7F:
            if(snd.log) {
            snd.callback(0x03);
            snd.callback(data);
            }
            if(snd.enabled) SN76496Write(0, data);
            break;

        case 0xBE: /* VDP DATA */
            vdp_data_w(data);
            break;

        case 0xBD: /* VDP CTRL */ 
        case 0xBF:
            vdp_ctrl_w(data);
            break;

        case 0xF0: /* YM2413 */
        case 0xF1:
            if(snd.log) {
            snd.callback((port & 1) ? 0x06 : 0x05);
            snd.callback(data);
            }
            if(snd.enabled && sms.use_fm) ym2413_write(0, port & 1, data);
            break;

        case 0xF2: /* YM2413 DETECT */
            if(sms.use_fm) sms.port_F2 = (data & 1);
            break;

        case 0x3F: /* TERRITORY CTRL. */
             sms.port_3F = ((data & 0x80) | (data & 0x20) << 1) & 0xC0;
            if(sms.country == TYPE_DOMESTIC) sms.port_3F ^= 0xC0;
            break;
    }
}


/* Read from an I/O port */
int cpu_readport(int port)
{
    uint8 temp = 0xFF;

    if (cart.type == TYPE_SG1000)
    {
        switch(port & 0xC0)
        {
            case 0x80:
                return (port & 1) ? vdp_ctrl_r() : vdp_data_r();

            case 0xC0:
                temp = 0xFF;
                if(input.pad[0] & INPUT_UP)      temp &= ~0x01;
                if(input.pad[0] & INPUT_DOWN)    temp &= ~0x02;
                if(input.pad[0] & INPUT_LEFT)    temp &= ~0x04;
                if(input.pad[0] & INPUT_RIGHT)   temp &= ~0x08;
                if(input.pad[0] & INPUT_BUTTON1) temp &= ~0x10;
                if(input.pad[0] & INPUT_BUTTON2) temp &= ~0x20;
                return temp;
        }
        return 0xFF;
    }

    if (cart.type == TYPE_COLECO)
    {
        switch(port & 0xE0)
        {
            case 0xA0:
                return (port & 1) ? vdp_ctrl_r() : vdp_data_r();

            case 0xE0:
                if (coleco_pio_mode)
                {
                    /* Joystick mode */
                    temp = 0x7F;
                    if(input.pad[0] & INPUT_UP)      temp &= ~0x01;
                    else if(input.pad[0] & INPUT_DOWN)  temp &= ~0x04;
                    if(input.pad[0] & INPUT_LEFT)    temp &= ~0x08;
                    else if(input.pad[0] & INPUT_RIGHT) temp &= ~0x02;
                    if(input.pad[0] & INPUT_BUTTON1) temp &= ~0x40;
                }
                else
                {
                    /* Keypad mode */
                    temp = 0x7F;
                    int key = (input.system & INPUT_COLECO_KEYPAD_MASK) >> INPUT_COLECO_KEYPAD_SHIFT;
                    if (key >= 0 && key < 12)
                    {
                        temp = coleco_keymask[key];
                    }

                    /* Keep right button available if mapped. */
                    if(input.pad[0] & INPUT_BUTTON2) temp &= ~0x40;
                }
                return temp;
        }
        return 0xFF;
    }

    switch(port & 0xFF)
    {
        case 0x01: /* GG SIO */
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x05:
            return (0x00);
    
        case 0x7E: /* V COUNTER */
            return (vdp_vcounter_r());
            break;
    
        case 0x7F: /* H COUNTER */
            return (vdp_hcounter_r());
            break;
    
        case 0x00: /* INPUT #2 */
            temp = 0xFF;
            if(input.system & INPUT_START) temp &= ~0x80;
            if(sms.country == TYPE_DOMESTIC) temp &= ~0x40;
           // if(sms.display == DISPLAY_NTSC)  temp &= ~0x20; // TODO PAL
            return (temp);
    
        case 0xC0: /* INPUT #0 */  
        case 0xDC:
            temp = 0xFF;
            if(input.pad[0] & INPUT_UP)      temp &= ~0x01;
            if(input.pad[0] & INPUT_DOWN)    temp &= ~0x02;
            if(input.pad[0] & INPUT_LEFT)    temp &= ~0x04;
            if(input.pad[0] & INPUT_RIGHT)   temp &= ~0x08;
            if(input.pad[0] & INPUT_BUTTON2) temp &= ~0x10;
            if(input.pad[0] & INPUT_BUTTON1) temp &= ~0x20;
            if(input.pad[1] & INPUT_UP)      temp &= ~0x40;
            if(input.pad[1] & INPUT_DOWN)    temp &= ~0x80;
            return (temp);
    
        case 0xC1: /* INPUT #1 */
        case 0xDD:
            temp = 0xFF;
            if(input.pad[1] & INPUT_LEFT)    temp &= ~0x01;
            if(input.pad[1] & INPUT_RIGHT)   temp &= ~0x02;
            if(input.pad[1] & INPUT_BUTTON2) temp &= ~0x04;
            if(input.pad[1] & INPUT_BUTTON1) temp &= ~0x08;
            if(input.system & INPUT_SOFT_RESET) temp &= ~0x10;
            return ((temp & 0x3F) | (sms.port_3F & 0xC0));

        case 0xBE: /* VDP DATA */
            return (vdp_data_r());
    
        case 0xBD:
        case 0xBF: /* VDP CTRL */
            return (vdp_ctrl_r());

        case 0xF2: /* YM2413 DETECT */
            if(sms.use_fm) return (sms.port_F2);
            break;
    }
    return (0xFF);     
}


void sms_mapper_w(int address, int data)
{
    if (cart.type != TYPE_SMS && cart.type != TYPE_GG) return;

    /* Calculate ROM page index */
    uint8 page = (data % cart.pages);

    /* Save frame control register data */
    sms.fcr[address] = data;

    switch(address)
    {
        case 0:
            if(data & 8)
            {
                sms.save = 1;
                /* Page in ROM */
                cpu_readmap[4]  = &sms.sram[(data & 4) ? 0x4000 : 0x0000];
                cpu_readmap[5]  = &sms.sram[(data & 4) ? 0x6000 : 0x2000];
                cpu_writemap[4] = &sms.sram[(data & 4) ? 0x4000 : 0x0000];
                cpu_writemap[5] = &sms.sram[(data & 4) ? 0x6000 : 0x2000];
            }
            else
            {
                /* Page in RAM */
                cpu_readmap[4]  = &cart.rom[((sms.fcr[3] % cart.pages) << 14) + 0x0000];
                cpu_readmap[5]  = &cart.rom[((sms.fcr[3] % cart.pages) << 14) + 0x2000];
                cpu_writemap[4] = sms.dummy;
                cpu_writemap[5] = sms.dummy;
            }
            break;

        case 1:
            cpu_readmap[0] = &cart.rom[(page << 14) + 0x0000];
            cpu_readmap[1] = &cart.rom[(page << 14) + 0x2000];
            break;

        case 2:
            cpu_readmap[2] = &cart.rom[(page << 14) + 0x0000];
            cpu_readmap[3] = &cart.rom[(page << 14) + 0x2000];
            break;

        case 3:
            if(!(sms.fcr[0] & 0x08))
            {
                cpu_readmap[4] = &cart.rom[(page << 14) + 0x0000];
                cpu_readmap[5] = &cart.rom[(page << 14) + 0x2000];
            }
            break;
    }
}


int sms_irq_callback(int param)
{
    return (0xFF);
}

