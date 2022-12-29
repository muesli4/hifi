// SPDX-FileCopyrightText: Moritz Bruder <muesli4 at gmail dot com>
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef PROGRAM_CONFIG_HPP
#define PROGRAM_CONFIG_HPP

#include <boost/filesystem.hpp>
#include <string>
#include <optional>
#include <chrono>

#include <libwtk-sdl2/geometry.hpp>
#include <libwtk-sdl2/font.hpp>

struct display_config
{
    bool fullscreen;
    vec resolution;
};

struct system_control_config
{
    std::string shutdown_command;
    std::string reboot_command;
};

struct dim_idle_timer_config
{
    std::string dim_command;
    std::string undim_command;
    std::chrono::seconds delay;
};

/*
struct swipe_config
{
    double dir_unambig_factor_threshold;
};
*/

struct filesystem_cover_provider_config
{
    std::string directory;
    std::vector<std::string> extensions;
    std::vector<std::string> names;
};

struct cover_config
{
    std::vector<std::string> sources;
    std::optional<filesystem_cover_provider_config> opt_filesystem_cover_provider;
};

struct on_screen_keyboard_config
{
    vec size;
    std::string keys;
};

struct program_config
{
    font default_font;
    font big_font;

    std::string font_path;
    display_config display;
    system_control_config system_control;
    dim_idle_timer_config dim_idle_timer;
    //swipe_config swipe;
    cover_config cover;
    on_screen_keyboard_config on_screen_keyboard;

    std::optional<int> opt_port;
};

bool parse_program_config(boost::filesystem::path config_path, program_config & result);

#endif

