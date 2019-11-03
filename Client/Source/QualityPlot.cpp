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

class QualityPlot::QualityPlotImpl: public Component {
public:
	QualityPlotImpl() {
		// 0 is the "default" kind, 1 would select OpenGL for rendering, but that wouldn't work in this context
		gr = std::make_unique<mglGraph>(0, 256, 256);
	}

	virtual void resized() override {
		auto newSize = getLocalBounds();
		gr->SetSize(newSize.getWidth(), newSize.getHeight(), true);
		repaint();
	}

	virtual void paint(Graphics& g) override {
		jassert(gr->GetWidth() == getWidth());
		jassert(gr->GetHeight() == getHeight());
		const auto &luf = getLookAndFeel();
		auto bg = luf.findColour(ComboBox::backgroundColourId);
		gr->Clf(bg.getRed()/255.0, bg.getGreen() / 255.0, bg.getBlue() / 255.0);
		gr->FPlot("sin(pi*x)");
		gr->Axis("xy");
		gr->Finish();

		Image blitImage(Image::PixelFormat::ARGB, getWidth(), getHeight(), false);
		{
			// Restrict lifetime of bitmapData object, because it might write back into the image on deletion only
			Image::BitmapData bitmapData(blitImage, Image::BitmapData::writeOnly);
			const unsigned char *source = gr->GetRGBA();
			uint8 *lineStart = bitmapData.data;
			for (int y = 0; y < getHeight(); y++) {
				auto dest = lineStart;
				for (int x = 0; x < getWidth(); x++) {
					//*(dest) = *(source + 3);
					*(dest) = *(source+2);
					*(dest + 1) = *(source + 1);
					*(dest + 2) = *(source);

					// Advance pointers to next pixel
					source += 4;
					dest += bitmapData.pixelStride;
				}
				lineStart += bitmapData.lineStride;
			}
		}

		// Now we want to display that image!
		g.drawImage(blitImage, 0, 0, getWidth(), getHeight(), 0, 0, gr->GetWidth(), gr->GetHeight());
	}

private:
	std::unique_ptr<mglGraph> gr;
};

QualityPlot::QualityPlot()
{
	impl = std::make_unique<QualityPlotImpl>();
	addAndMakeVisible(*impl);
}

void QualityPlot::resized()
{
	auto area = getLocalBounds();
	if (impl) impl->setBounds(area);
}

