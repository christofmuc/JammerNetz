#pragma once

#include "JuceHeader.h"

class StreamLogger {
public:
	// See https://stackoverflow.com/questions/1134388/stdendl-is-of-unknown-type-when-overloading-operator
	typedef std::basic_ostream<char, std::char_traits<char> > CoutType;
	typedef CoutType& (*StandardEndLine)(CoutType&);

	// Singleton implementation
	static StreamLogger &instance();
	~StreamLogger();

	StreamLogger &operator <<(const char *string);
	StreamLogger &operator <<(std::string const &string);
	StreamLogger &operator <<(juce::String const &string);
	StreamLogger &operator <<(StandardEndLine manip);
	StreamLogger &operator <<(int value);
	StreamLogger &operator <<(uint64 value);
	StreamLogger &operator <<(double value);

	void flushBuffer(bool force = false);

private:	
	static ScopedPointer<StreamLogger> instance_;

	StreamLogger();

	std::stringstream buffer;
};


