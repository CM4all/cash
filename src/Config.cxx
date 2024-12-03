// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Config.hxx"
#include "io/BufferedReader.hxx"
#include "io/FdReader.hxx"
#include "io/Open.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "util/CharUtil.hxx"
#include "util/StringStrip.hxx"

#include <charconv>
#include <stdexcept>

using std::string_view_literals::operator""sv;

static constexpr bool
IsCommandChar(char ch) noexcept
{
	return IsLowerAlphaASCII(ch);
}

static std::pair<std::string_view, std::string_view>
ExtractCommandValue(const char *line)
{
	const char *p = line;

	while (IsCommandChar(*p))
		++p;

	const std::string_view command{line, p};
	if (command.empty())
		throw std::runtime_error{"No command"};

	if (IsWhitespaceNotNull(*p))
		p = StripLeft(p + 1);
	else if (*p != 0)
		throw std::runtime_error{"Malformed command"};

	return {command, StripRight(std::string_view{p})};
}

static uint_least8_t
ParsePercent(std::string_view s)
{
	if (!s.ends_with('%'))
		throw std::runtime_error{"Value must end with '%'"};

	const char *const first = s.data(), *const last = first + s.size() - 1;

	uint_least8_t value;
	auto [ptr, ec] = std::from_chars(first, last, value, 10);
	if (ptr == first || ptr != last || ec != std::errc{})
		throw std::runtime_error{"Malformed number"};

	return value;
}

Config
LoadConfigFile(const char *path)
{
	Config config;
	auto kernel_config_iterator = config.kernel_config.before_begin();

	const auto fd = OpenReadOnly(path);
	FdReader fd_reader{fd};
	BufferedReader r{fd_reader};

	while (const char *line = r.ReadLine()) {
		line = StripLeft(line);
		if (*line == 0 || *line == '#')
			continue;

		const auto [command, value] = ExtractCommandValue(line);
		if (command == "dir"sv)
			config.dir = value;
		else if (command == "brun"sv)
			config.brun = ParsePercent(value);
		else if (command == "frun"sv)
			config.frun = ParsePercent(value);
		else if (command == "nocull"sv) {
			config.culling_disabled = true;
			continue;
		} else
			config.frun = ParsePercent(value);

		kernel_config_iterator =
			config.kernel_config.emplace_after(kernel_config_iterator,
							   command.begin(), value.end());
	}

	if (config.dir.empty())
		throw std::runtime_error{"No 'dir' setting"};

	return config;
}
