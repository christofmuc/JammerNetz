#include "ServerPort.h"

#include "gtest/gtest.h"

TEST(ServerPort, AcceptsMaximumPort)
{
	EXPECT_EQ(parseServerPort("65535"), 65535);
}

TEST(ServerPort, RejectsPortAboveMaximum)
{
	EXPECT_FALSE(parseServerPort("65536").has_value());
}

TEST(ServerPort, RejectsOverflow)
{
	EXPECT_FALSE(parseServerPort("4294967297").has_value());
}
