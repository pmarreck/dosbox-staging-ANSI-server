// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "config/config.h"
#include "config/setup.h"
#include "shell/command_line.h"
#include "textmode_server/textmode_server.h"

#include <gtest/gtest.h>

#include <string>

namespace {

class TextModeServerConfigTest : public ::testing::Test {
protected:
	TextModeServerConfigTest() : command_line(0, nullptr)
	{
		config = std::make_unique<Config>(&command_line);
	}

	void SetUp() override
	{
		TEXTMODESERVER_AddConfigSection(config);
	}

	SectionProp* GetSection()
	{
		auto* section = config->GetSection("textmode_server");
		return dynamic_cast<SectionProp*>(section);
	}

	CommandLine command_line;
	ConfigPtr config;
};

TEST_F(TextModeServerConfigTest, Defaults)
{
	auto* props = GetSection();
	ASSERT_NE(props, nullptr);

	auto* enable_prop = props->GetBoolProp("enable");
	ASSERT_NE(enable_prop, nullptr);
	EXPECT_FALSE(enable_prop->GetValue());

	auto* port_prop = dynamic_cast<PropInt*>(props->GetProperty("port"));
	ASSERT_NE(port_prop, nullptr);
	EXPECT_EQ(static_cast<int>(port_prop->GetValue()), 6000);

	auto* show_attr_prop = props->GetBoolProp("show_attributes");
	ASSERT_NE(show_attr_prop, nullptr);
	EXPECT_TRUE(show_attr_prop->GetValue());

	auto* sentinel_prop = props->GetStringProp("sentinel");
	ASSERT_NE(sentinel_prop, nullptr);
	const std::string expected_sentinel = "\xF0\x9F\x96\xB5";
	EXPECT_EQ(static_cast<std::string>(sentinel_prop->GetValue()), expected_sentinel);

	auto* close_prop = props->GetBoolProp("close_after_response");
	ASSERT_NE(close_prop, nullptr);
	EXPECT_FALSE(close_prop->GetValue());

}

TEST_F(TextModeServerConfigTest, Overrides)
{
	auto* props = GetSection();
	ASSERT_NE(props, nullptr);

	ASSERT_TRUE(props->HandleInputline("enable = true"));
	ASSERT_TRUE(props->HandleInputline("port = 6123"));
	ASSERT_TRUE(props->HandleInputline("show_attributes = false"));
	ASSERT_TRUE(props->HandleInputline("sentinel = @"));
	ASSERT_TRUE(props->HandleInputline("close_after_response = true"));


	auto* enable_prop = props->GetBoolProp("enable");
	ASSERT_NE(enable_prop, nullptr);
	EXPECT_TRUE(enable_prop->GetValue());

	auto* port_prop = dynamic_cast<PropInt*>(props->GetProperty("port"));
	ASSERT_NE(port_prop, nullptr);
	EXPECT_EQ(static_cast<int>(port_prop->GetValue()), 6123);

	auto* show_attr_prop = props->GetBoolProp("show_attributes");
	ASSERT_NE(show_attr_prop, nullptr);
	EXPECT_FALSE(show_attr_prop->GetValue());

	auto* sentinel_prop = props->GetStringProp("sentinel");
	ASSERT_NE(sentinel_prop, nullptr);
	EXPECT_EQ(static_cast<std::string>(sentinel_prop->GetValue()), "@");

	auto* close_prop = props->GetBoolProp("close_after_response");
	ASSERT_NE(close_prop, nullptr);
	EXPECT_TRUE(close_prop->GetValue());

}

} // namespace
