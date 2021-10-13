/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "ApplicationState.h"

bool ValueTreeUtils::isChildOf(Identifier searchFor, ValueTree child)
{
	if (!child.isValid())
		return false;
	if (child.getType() == searchFor)
		return true;
	return isChildOf(searchFor, child.getParent());
}

ValueListener::ValueListener(Value value, std::function<void(Value& newValue)> onChanged) : value_(value), onChanged_(onChanged)
{
	value_.addListener(this);
}

ValueListener::~ValueListener()
{
	value_.removeListener(this);
}

void ValueListener::valueChanged(Value& value)
{
	if (onChanged_) {
		onChanged_(value);
	}
}

void ValueListener::triggerOnChanged()
{
	if (onChanged_)
		onChanged_(value_);
}
