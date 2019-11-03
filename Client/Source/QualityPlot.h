/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "JuceHeader.h"

class QualityPlot : public Component {
public:
	QualityPlot();

	virtual void resized() override;
	virtual void paint(Graphics& g) override;

};