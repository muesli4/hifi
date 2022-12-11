// SPDX-FileCopyrightText: Moritz Bruder <muesli4 at gmail dot com>
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "cover_view.hpp"

cover_view::cover_view(std::function<void(swipe_direction)> swipe_callback, std::function<void()> press_callback)
    : cover_view(std::make_shared<label>(""), std::make_shared<texture_view>(), swipe_callback, press_callback)
{
}

cover_view::cover_view(std::shared_ptr<label> l, std::shared_ptr<texture_view> tv, std::function<void(swipe_direction)> swipe_callback, std::function<void()> press_callback)
    : embedded_widget<notebook>(std::vector<widget_ptr>{ l, tv })
    , _label(l)
    , _texture_view(tv)
    , _swipe_area(swipe_callback, press_callback)
{
    _label->set_wrap(true);
}

cover_view::~cover_view()
{
}

void cover_view::on_mouse_up_event(mouse_up_event const & e)
{
    _swipe_area.on_mouse_up_event(e);
}

std::vector<widget *> cover_view::get_children()
{
    return { &_embedded_widget, &_swipe_area };
}

std::vector<widget const *> cover_view::get_children() const
{
    return { &_embedded_widget, &_swipe_area };
}

void cover_view::on_box_allocated()
{
    _embedded_widget.apply_layout(get_box());
    _swipe_area.apply_layout(get_box());
}

void cover_view::set_cover(unique_texture_ptr texture_ptr)
{
    _embedded_widget.set_page(1);
    // Minimum width is 0 and natural width is actual width of the image.
    // TODO Add option to scale to box
    _texture_view->set_texture(std::move(texture_ptr), 0, -1);
}

void cover_view::set_cover(std::string title, std::string artist, std::string album)
{
    _embedded_widget.set_page(0);
    // TODO make sure the font actually exists
    _label->set_content({ paragraph(title, 1, 1), paragraph(artist, 1, 1), paragraph(album, 0, 1) });
}

