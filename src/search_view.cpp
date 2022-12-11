// SPDX-FileCopyrightText: Moritz Bruder <muesli4 at gmail dot com>
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <algorithm>

#include <unicode/unistr.h>

#include "widget_util.hpp"
#include "search_view.hpp"

search_view::search_view(SDL_Renderer * r, vec size, std::string keys, std::vector<std::string> const & values, std::function<void(std::size_t)> activate_callback)
    : search_view(r, std::make_shared<keypad>(size, keys, [=](auto str){ on_submit(str); })
    , std::make_shared<list_view>(_filtered_values, 0, [=](auto idx){ activate_callback(this->_filtered_indices[idx]); })
    , values
    )
{
}

search_view::search_view(SDL_Renderer * r, std::shared_ptr<keypad> keypad, std::shared_ptr<list_view> list_view, std::vector<std::string> const & values)
    : embedded_widget<notebook>(std::vector<widget_ptr>{ keypad, add_list_view_controls(r, list_view, ICONDIR "keyboard.png", [this](){ on_back(); }) })
    , _keypad(keypad)
    , _list_view(list_view)
    , _values(values)
{
}

void search_view::on_submit(std::string search_term)
{
    // TODO check if changed
    _filtered_indices.clear();
    _filtered_values.clear();

    for (std::size_t pos = 0; pos < _values.get().size(); pos++)
    {
        auto const & s = _values.get()[pos];
        if (icu::UnicodeString::fromUTF8(s).toLower().indexOf(icu::UnicodeString::fromUTF8(search_term)) != -1)
        {
            _filtered_indices.push_back(pos);
            _filtered_values.push_back(s);
        }
    }
    _embedded_widget.set_page(1);
}

void search_view::on_back()
{
    _embedded_widget.set_page(0);
}


void search_view::on_playlist_changed()
{
    // TODO safe search term in optional and do the search again if there has been an active search before
    _list_view->set_position(0);
    on_back();
}

void search_view::set_position(std::size_t position)
{
    _list_view->set_position(position);
}

void search_view::set_filtered_highlight_position(std::size_t position)
{
    auto b = std::cbegin(_filtered_indices);
    auto e = std::cend(_filtered_indices);
    auto it = std::find(b, e, position);
    if (it != e)
    {
        _list_view->set_highlight_position(std::distance(b, it));
    }
}

void search_view::set_selected_position(std::size_t position)
{
    _list_view->set_selected_position(position);
}

