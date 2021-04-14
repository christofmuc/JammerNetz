/*
   Copyright (c) 2021 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "ServerLogger.h"

#include <curses.h>

static SCREEN *terminal;

void ServerLogger::init()
{
	// We're good to good, init screen if possible
	char *buffer;
#if WIN32
	size_t len;
	_dupenv_s(&buffer, &len, "TERM");
#else
	buffer = getenv("TERM");
#endif
	terminal = newterm(buffer, stdout, stdin);
}

void ServerLogger::deinit() {
	if (terminal) {
		endwin();
	}
}

void ServerLogger::errorln(String const &message)
{
	if (message != lastMessage) {
		std::cerr << message << std::endl;
		lastMessage = message;
	}
}

void ServerLogger::printAtPosition(int x, int y, std::string text)
{
	if (terminal) {
		move(x, y);
		printw(text.c_str());
		refresh();
	}
	else {
		std::cout << text << std::endl;
	}
}

std::vector<std::pair<int, std::string>> kColumnHeaders = { {0, "Client"}, {20, "Len" }, { 26, "ooO" }, { 32, "span" } , { 38, "dup" } , { 44, "heal" } , { 50, "late" } , { 56, "drop" } , { 62, "gap" },
	{70, "Jitter ms"}, { 80, "Jitter SD" }, { 90, "Clock delta (s)"} };

std::map<std::string, int> sClientRows;
int kRowsInTable = 0;

int yForClient(std::string const &clientID) {
	if (sClientRows.find(clientID) == sClientRows.end()) {
		sClientRows[clientID] = kRowsInTable++;
	}
	return sClientRows[clientID];
}

void ServerLogger::printColumnHeader(int row) {
	if (terminal) {
		int y = row;
		for (const auto& col : kColumnHeaders) {
			char buffer[200];
			snprintf(buffer, 200, "%6s", col.second.c_str());
			mvprintw(y, col.first, buffer);
		}
		refresh();
	}
}

void ServerLogger::printStatistics(int row, std::string const &clientID, JammerNetzStreamQualityInfo quality) {
	if (terminal) {
		// Find y 
		int y = row + yForClient(clientID);

		move(y, kColumnHeaders[0].first);
		clrtoeol();
		move(y, kColumnHeaders[0].first);
		printw(clientID.c_str());

		// Print columns into table
		char buffer[200];
		//snprintf(buffer, 200, "%6d", (int)quality.packagesPushed);	mvprintw(y, 20, buffer);
		//snprintf(buffer, 200, "%6d", (int)quality.packagesPopped);	mvprintw(y, 28, buffer);
#pragma warning( push )
#pragma warning( disable : 4996 )
		sprintf(buffer, "%*d", 6, (int)(quality.packagesPushed - quality.packagesPopped));	mvprintw(y, kColumnHeaders[1].first, buffer);
		sprintf(buffer, "%*d", 6, (int)quality.outOfOrderPacketCounter);	mvprintw(y, kColumnHeaders[2].first, buffer);
		sprintf(buffer, "%*d", 6, (int)quality.maxWrongOrderSpan);	mvprintw(y, kColumnHeaders[3].first, buffer);
		sprintf(buffer, "%*d", 6, (int)quality.duplicatePacketCounter);	mvprintw(y, kColumnHeaders[4].first, buffer);
		sprintf(buffer, "%*d", 6, (int)quality.dropsHealed);	mvprintw(y, kColumnHeaders[5].first, buffer);
		sprintf(buffer, "%*d", 6, (int)quality.tooLateOrDuplicate);	mvprintw(y, kColumnHeaders[6].first, buffer);
		sprintf(buffer, "%*d", 6, (int)quality.droppedPacketCounter);	mvprintw(y, kColumnHeaders[7].first, buffer);
		sprintf(buffer, "%*d", 6, (int)quality.maxLengthOfGap);	mvprintw(y, kColumnHeaders[8].first, buffer);
		sprintf(buffer, "%2.1f", quality.jitterMeanMillis);	mvprintw(y, kColumnHeaders[9].first, buffer);
		sprintf(buffer, "%2.1f", quality.jitterSDMillis);	mvprintw(y, kColumnHeaders[10].first, buffer);
		sprintf(buffer, "%2.1f", quality.wallClockDelta / 1000.0); mvprintw(y, kColumnHeaders[11].first, buffer);
#pragma warning( pop )
		refresh();
	}
}

void ServerLogger::printServerStatus(std::string const &text)
{
	if (terminal) {
		move(1, 0);
		clrtoeol();
		move(1, 0);
		printw(text.c_str());
		refresh();
	}
	else {
		std::cout << text << std::endl;
	}
}

void ServerLogger::printServerStatistics(int row, std::string const &text)
{
	if (terminal) {
		if (((counter++) % 500) == 0) {
			int y = row + (int) sClientRows.size() + 2;
			move(y, 0);
			clrtoeol();
			printw(text.c_str());
			refresh();
		}
	}
}

void ServerLogger::printClientStatus(int row, std::string const &clientID, std::string const &text)
{
	if (terminal) {
		int y = row + yForClient(clientID);
		move(y, kColumnHeaders[0].first);
		clrtoeol();
		move(y, kColumnHeaders[0].first);
		printw(clientID.c_str());
		printw(": ");
		printw(text.c_str());
		refresh();
	}
	else {
		std::cout << "Client " << clientID << ": " << text << std::endl;
	}
}

juce::String ServerLogger::lastMessage;

long long ServerLogger::counter = 0;

