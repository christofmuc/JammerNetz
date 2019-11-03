/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "QualityPlot.h"

#pragma warning( push )
#pragma warning( disable : 4458)
#include <complex.h> // for cabs()

// This shouldn't be necessary, but it seems to me that the 2.4.2.1 to 2.4.3 update to define.h of mathgl removed this macro without replacing it with something else...
#define mgl_abs(x)     cabs(x)

#include "mgl2/mgl.h"
#pragma warning( pop )

QualityPlot::QualityPlot()
{
}

void QualityPlot::resized()
{
}

void QualityPlot::paint(Graphics& g)
{
	mglGraph gr(0, 200, 200);
	gr.FPlot("sin(pi*x)");

	Image blitImage(Image::PixelFormat::RGB, 200, 200, false);
	{
		// Restrict lifetime of bitmapData object, because it might write back into the image on deletion only
		Image::BitmapData bitmapData(blitImage, Image::BitmapData::writeOnly);
		const unsigned char *source = gr.GetRGBA();
		uint8 *lineStart = bitmapData.data;
		for (int y = 0; y < 200; y++) {
			auto dest = lineStart;
			for (int x = 0; x < 200; x++) {
				// Copy pixel
				*dest = *source;
				*(dest + 1) = *(source + 1);
				*(dest + 2) = *(source + 2);

				// Advance pointers to next pixel
				source += 4;
				dest += bitmapData.pixelStride;
			}
			lineStart += bitmapData.lineStride;
		}
	}
	
	// Now we want to display that image!
	g.drawImage(blitImage, 0, 0, getWidth(), getHeight(), 0, 0, 200, 200);
}
