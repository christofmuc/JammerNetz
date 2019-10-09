#include "StreamLogger.h"

StreamLogger::~StreamLogger() {
}

void StreamLogger::flushBuffer(bool force /* = false */)
{
	std::string endl = "\n";
	std::string toEmit = buffer.str();
	// If the whole string ends on a newline, just write it. Else, we must split it and call writeToLog twice
	if (force || (toEmit.size() > 0 && toEmit.at(toEmit.length() - 1) == '\n')) {
		Logger::getCurrentLogger()->writeToLog(toEmit);
		buffer.str(std::string());
	}
	else if (toEmit.find(endl) != std::string::npos) {
		//TODO - this is wrong and slow. What I really want is to reverse search for the endl, and send this off
		size_t start = 0;
		size_t end;
		while ((end = toEmit.find(endl, start)) != std::string::npos) {
			Logger::getCurrentLogger()->writeToLog(toEmit.substr(start, end - start));
			start = end + endl.length();
		}
		buffer.str(std::string());
	}
}

StreamLogger &StreamLogger::instance()
{
	static StreamLogger instance_;
	return instance_;
}

StreamLogger & StreamLogger::operator<<(double value)
{
	buffer << value;
	return *this;
}

StreamLogger & StreamLogger::operator<<(int value)
{
	buffer << std::fixed << std::setprecision(2) << value;
	return *this;
}

StreamLogger & StreamLogger::operator<<(const char *string)
{
	buffer << string;
	flushBuffer();
	return *this;
}

StreamLogger & StreamLogger::operator<<(juce::String const &string)
{
	buffer << string;
	flushBuffer();
	return *this;
}

StreamLogger &StreamLogger::operator<<(std::string const &string)
{
	buffer << string;
	flushBuffer();
	return *this;
}

StreamLogger & StreamLogger::operator<<(StandardEndLine manip)
{
	buffer << manip;
	flushBuffer();
	return *this;
}

StreamLogger & StreamLogger::operator<<(uint64 value)
{
	buffer << value;
	flushBuffer();
	return *this;
}

