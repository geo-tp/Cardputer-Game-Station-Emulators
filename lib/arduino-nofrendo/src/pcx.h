/*
** Nofrendo (c) 1998-2000 Matthew Conte (matt@conte.com)
**
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of version 2 of the GNU Library General 
** Public License as published by the Free Software Foundation.
**
** This program is distributed in the hope that it will be useful, 
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU 
** Library General Public License for more details.  To obtain a 
** copy of the GNU Library General Public License, write to the Free 
** Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
** Any permitted reproduction of these routines, in whole or in part,
** must bear this legend.
**
**
** pcx.h
**
** PCX format screen-saving routines
** $Id: pcx.h,v 1.2 2001/04/27 14:37:11 neil Exp $
*/

#ifndef _PCX_H_
#define _PCX_H_

#include "osd.h"
#include "bitmap.h"

/* Got these out of ZSoft's document */
typedef struct pcxheader_s
{
   uint8 Manufacturer;
   uint8 Version;
   uint8 Encoding;
   uint8 BitsPerPixel;
   uint16 Xmin;
   uint16 Ymin;
   uint16 Xmax;
   uint16 Ymax;
   uint16 HDpi;
   uint16 VDpi;
   uint8 Colormap[48];
   uint8 Reserved;
   uint8 NPlanes;
   uint16 BytesPerLine;
   uint16 PaletteInfo;
   uint16 HscreenSize;
   uint16 VscreenSize;
   uint8 Filler[54];
} __PACKED__ pcxheader_t;

extern int pcx_write(char *filename, bitmap_t *bmp, rgb_t *pal);

#endif /* _PCX_H_ */

/*
** $Log: pcx.h,v $
** Revision 1.2  2001/04/27 14:37:11  neil
** wheeee
**
** Revision 1.1.1.1  2001/04/27 07:03:54  neil
** initial
**
** Revision 1.8  2000/11/05 16:37:18  matt
** rolled rgb.h into bitmap.h
**
** Revision 1.7  2000/07/25 02:21:56  matt
** had forgotten some includes
**
** Revision 1.6  2000/07/17 01:52:27  matt
** made sure last line of all source files is a newline
**
** Revision 1.5  2000/06/26 04:55:13  matt
** changed routine name, big whoop
**
** Revision 1.4  2000/06/09 15:12:25  matt
** initial revision
**
*/
