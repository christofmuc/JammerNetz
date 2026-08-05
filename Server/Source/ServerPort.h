#pragma once

#include <charconv>
#include <optional>
#include <string_view>
#include <system_error>

inline std::optional<int> parseServerPort(std::string_view value)
{
	if (value.empty()) {
		return std::nullopt;
	}

	int port = 0;
	const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), port);
	if (error != std::errc{} || end != value.data() + value.size() || port < 1 || port > 65535) {
		return std::nullopt;
	}

	return port;
}
