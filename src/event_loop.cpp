#include "event_loop.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <iterator>
#include <mutex>
#include <queue>
#include <thread>

#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <libwtk-sdl2/box.hpp>
#include <libwtk-sdl2/text_button.hpp>
#include <libwtk-sdl2/texture_button.hpp>
#include <libwtk-sdl2/list_view.hpp>
#include <libwtk-sdl2/notebook.hpp>
#include <libwtk-sdl2/padding.hpp>
#include <libwtk-sdl2/sdl_util.hpp>
#include <libwtk-sdl2/util.hpp>
#include <libwtk-sdl2/widget.hpp>
#include <libwtk-sdl2/widget_context.hpp>

#include "idle_timer.hpp"
#include "mpd_control.hpp"
#include "navigation_event.hpp"
#include "udp_control.hpp"
#include "user_event.hpp"
#include "util.hpp"
#include "widget_util.hpp"
#include "enum_texture_button.hpp"

#include "cover_view.hpp"
#include "search_view.hpp"

// Allow debugging from build directory and avoid errors.
#ifndef ICONDIR
#define ICONDIR "../data/icons/"
#endif

// struct gui_view
// {
//     // e.g., load current cover
//     virtual void on_wake_up() = 0;
// 
//     virtual void on_change_event(change_event_type type) = 0;
// };
// 
// 
// // use MVC
// create models for state that proxies directly to MPD
// views are asked which models they want to listen to (i.e. some context type where they can register themselves

void handle_cover_swipe_direction(swipe_direction dir, mpd_control & mpdc, unsigned int volume_step)
{
    switch (dir)
    {
        case swipe_direction::UP:
            mpdc.inc_volume(volume_step);
            break;
        case swipe_direction::DOWN:
            mpdc.dec_volume(volume_step);
            break;
        case swipe_direction::RIGHT:
            mpdc.next_song();
            break;
        case swipe_direction::LEFT:
            mpdc.prev_song();
            break;
        default:
            break;
    }
}

template <typename T, typename EventSender, typename Event>
void push_change_event(EventSender & es, Event e, T & old_val, T const & new_val)
{
    if (old_val != new_val)
    {
        old_val = new_val;
        es.push(e);
    }
}

bool is_input_event(SDL_Event & ev)
{
    return ev.type == SDL_MOUSEBUTTONDOWN ||
           ev.type == SDL_MOUSEBUTTONUP ||
           ev.type == SDL_KEYDOWN ||
           ev.type == SDL_KEYUP ||
           ev.type == SDL_FINGERDOWN ||
           ev.type == SDL_FINGERUP;
}

bool is_duplicate_touch_finger_event(SDL_Event & ev)
{
    bool const duplicate_motion =
        ev.type == SDL_MOUSEMOTION && ev.motion.which == SDL_TOUCH_MOUSEID;
    bool const duplicate_button =
        (ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_MOUSEBUTTONUP)
        && ev.button.which == SDL_TOUCH_MOUSEID;
    return duplicate_motion || duplicate_button;
}

/**
 * Should the screen be undimmed by this event?
 */
bool is_undim_event(SDL_Event & ev)
{
    if (ev.type == SDL_WINDOWEVENT)
    {
        uint32_t we = ev.window.event;
        // Mouse moved inside the window.
        return we == SDL_WINDOWEVENT_ENTER;
    }
    // Ignore any down events because the up event will follow later.
    else if (ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_FINGERDOWN)
    {
        return false;
    }
    else
    {
        return !is_duplicate_touch_finger_event(ev);
    }
}

/**
 * Refresh the current playlist by destructively updating the current state.
 *
 */
void refresh_current_playlist
    ( std::vector<std::string> & cpl
    , unsigned int & cpv
    , mpd_control & mpdc
    )
{
    playlist_change_info pci = mpdc.get_current_playlist_changes(cpv);
    cpl.resize(pci.new_length);
    cpv = pci.new_version;
    for (auto & p : pci.changed_positions)
    {
        cpl[p.first] = p.second;
    }
}

// TODO refactor
bool idle_timer_enabled(program_config const & cfg)
{
    return cfg.dim_idle_timer.delay.count() != 0;
}

/*
// Types of events that come from a model.
enum class model_event_type
{
    PLAYLIST,
    SONG_LOCATION,
    PLAYBACK_STATUS
};

template <typename Function>
struct callback_handler
{
    std::size_t add(Function f)
    {
        int id =_id_gen;
        _functions.insert(id, f);
        _id_gen++;
        return id;
    }

    void remove(std::size_t id)
    {
        _functions.erase(id);
    }

    template <typename T, typename... Args>
    void emit(Args... args)
    {
        for (auto f : _functions)
            f(args...);
    }

    private:

    std::size_t _id_gen;

    std::unordered_map<std::size_t, Function> _functions;
};

typedef std::function<void(playlist_change_info)> playback_status_function;
typedef std::function<void(std::optional<song_location>)> song_location_function;
typedef std::function<void(status_info)> status_function;

struct model_event_handler : callback_handler<playback_status_function>, callback_handler<song_location_function>, callback_handler<status_function>
{
    void process(model_event_type met);
};
*/

enum class change_event_type
{
    RANDOM_CHANGED,
    SONG_CHANGED,
    PLAYLIST_CHANGED,
    PLAYBACK_STATE_CHANGED
};

void event_loop::handle_other_event(SDL_Event const & e, widget_context & ctx)
{
    if (_nes.is_event_type(e.type))
    {
        navigation_event ne;
        _nes.read(e, ne);
        if (ne.type == navigation_event_type::NAVIGATION)
        {
            ctx.navigate_selection(ne.nt);
        }
        else
        {
            ctx.activate();
        }
    }
    else
    {
        ctx.process_event(e);
    }
}

widget_ptr make_shutdown_view(quit_action & result, bool & run)
{
    return vbox({ { false, std::make_shared<text_button>("Shutdown", [&run, &result](){ result = quit_action::SHUTDOWN; run = false; }) }
                , { false, std::make_shared<text_button>("Reboot", [&run, &result](){ result = quit_action::REBOOT; run = false; }) }
                }, 5, true);
}

quit_action event_loop::run(SDL_Renderer * renderer, program_config const & cfg)
{
    quit_action result = quit_action::NONE;

    // loop state
    std::string current_song_path;

    bool random;
    mpd_state playback_state = MPD_STATE_UNKNOWN;

    unsigned int current_song_pos = 0;
    std::vector<std::string> cpl;
    unsigned int cpv;

    bool refresh_cover = true;
    bool current_playlist_needs_refresh = false;

    // Set up user events.
    enum_user_event_sender<change_event_type> ces;
    enum_user_event_sender<idle_timer_event_type> tes;

    idle_timer_info iti(tes);
    bool dimmed = false;

    // timer is enabled
    if (idle_timer_enabled(cfg))
    {
        // act as if the timer expired and it should dim now
        tes.push(idle_timer_event_type::IDLE_TIMER_EXPIRED);
    }

    mpd_control mpdc(
        [&](std::optional<song_location> opt_sl)
        {
            // TODO probably move, as string copy might not be atomic
            //      another idea is to completely move all song information
            //      into the main thread
            if (opt_sl.has_value())
            {
                auto const & sl = opt_sl.value();
                current_song_path = sl.path;
                current_song_pos = sl.pos;
            }

            ces.push(change_event_type::SONG_CHANGED);
        },
        [&](bool value)
        {
            push_change_event(ces, change_event_type::RANDOM_CHANGED, random, value);
        },
        [&]()
        {
            ces.push(change_event_type::PLAYLIST_CHANGED);
        },
        [&](mpd_state state)
        {
            push_change_event(ces, change_event_type::PLAYBACK_STATE_CHANGED, playback_state, state);
        }
    );
    std::thread mpdc_thread(&mpd_control::run, std::ref(mpdc));

    std::optional<udp_control> opt_udp_control;
    std::thread udp_thread;
    if (cfg.opt_port.has_value())
    {
        opt_udp_control.emplace(cfg.opt_port.value(), _nes);
        udp_thread = std::thread { &udp_control::run, std::ref(opt_udp_control.value()) };
    }

    try
    {

        // get initial state from mpd
        std::tie(cpl, cpv) = mpdc.get_current_playlist();
        random = mpdc.get_random();
        // TODO ask mpd state!

        bool run = true;
        SDL_Event ev;

        // TODO move to MVC

        // construct views
        auto cv = std::make_shared<cover_view>([&](swipe_direction dir){ handle_cover_swipe_direction(dir, mpdc, 5); }, [&](){ mpdc.toggle_pause(); });
        auto playlist_v = std::make_shared<list_view>(cpl, current_song_pos, [&mpdc](std::size_t pos){ mpdc.play_position(pos); });
        auto search_v = std::make_shared<search_view>(renderer, cfg.on_screen_keyboard.size, cfg.on_screen_keyboard.keys, cpl, [&](auto pos){ mpdc.play_position(pos); });

        auto view_box = std::make_shared<notebook>(
            std::vector<widget_ptr>{ cv
                                   , add_list_view_controls(renderer, playlist_v, ICONDIR "jump_to_arrow.png", [=, &current_song_pos](){ playlist_v->set_position(current_song_pos); })
                                   , search_v
                                   , make_shutdown_view(result, run)
                                   });

        // side bar button controls
        auto random_button =
            make_enum_texture_button<bool, 2>( renderer
                                             , random
                                             , { ICONDIR "random_on.png"
                                               , ICONDIR "random_off.png"
                                               }
                                             , [&](){ mpdc.set_random(!random); }
                                             );
        auto play_button =
            make_enum_texture_button<mpd_state, 4>( renderer
                                                  , playback_state
                                                  , { ICONDIR "play.png"
                                                    , ICONDIR "play.png"
                                                    , ICONDIR "pause.png"
                                                    , ICONDIR "play.png"
                                                    }
                                                  , [&](){ mpdc.toggle_pause(); }
                                                  );

        auto button_controls = vbox(
                { { false, make_texture_button(renderer, ICONDIR "apps.png", [&](){ view_box->set_page((view_box->get_page() + 1) % 4);  }) }
                , { false, play_button }
                , { false, random_button }
                }, 5, true);

        box main_widget
            ( box::orientation::HORIZONTAL
            , { { false, pad_right(-5, pad(5, button_controls)) }
            , { true, pad(5, view_box) }
            }
            , 0
            );

        // TODO add dir_unambig_factor_threshold from config
        widget_context ctx(renderer, { cfg.default_font, cfg.big_font }, main_widget);
        ctx.draw();

        while (run && SDL_WaitEvent(&ev) == 1)
        {
            if (is_quit_event(ev))
            {
                std::cout << "Requested quit" << std::endl;
                run = false;
            }
            // most of the events are not required for a standalone fullscreen application
            else if (is_input_event(ev) || _nes.is_event_type(ev.type)
                                        || ces.is_event_type(ev.type)
                                        || tes.is_event_type(ev.type)
                                        || ev.type == SDL_WINDOWEVENT
                    )
            {
                // handle asynchronous user events synchronously
                if (ces.is_event_type(ev.type))
                {
                    switch (ces.read(ev))
                    {
                        case change_event_type::SONG_CHANGED:
                            refresh_cover = true;
                            playlist_v->set_highlight_position(current_song_pos);
                            search_v->set_filtered_highlight_position(current_song_pos);
                            break;
                        case change_event_type::PLAYLIST_CHANGED:
                            if (dimmed)
                                current_playlist_needs_refresh = true;
                            else
                            {
                                refresh_current_playlist(cpl, cpv, mpdc);

                                if (current_song_pos >= cpl.size())
                                {
                                    playlist_v->set_position(0);
                                }
                                search_v->on_playlist_changed();
                            }
                            break;
                        case change_event_type::RANDOM_CHANGED:
                            random_button->set_state(random);
                            break;
                        case change_event_type::PLAYBACK_STATE_CHANGED:
                            play_button->set_state(playback_state);
                            break;
                        default:
                            break;
                    }

                    if (dimmed)
                        continue;
                }
                // dim idle timer expired
                else if (tes.is_event_type(ev.type))
                {
                    dimmed = true;
                    system(cfg.dim_idle_timer.dim_command.c_str());
                    continue;
                }
                else
                {
                    if (idle_timer_enabled(cfg))
                    {
                        if (dimmed)
                        {
                            if (is_undim_event(ev))
                            {
                                if (current_playlist_needs_refresh)
                                {
                                    refresh_current_playlist(cpl, cpv, mpdc);
                                    current_playlist_needs_refresh = false;

                                    if (current_song_pos >= cpl.size())
                                    {
                                        playlist_v->set_position(0);
                                    }
                                    playlist_v->set_position(0);
                                    search_v->on_playlist_changed();
                                }
                                // ignore one event, turn on lights
                                dimmed = false;
                                // TODO run after drawing
                                system(cfg.dim_idle_timer.undim_command.c_str());
                                iti.sync();
                                // TODO refactor into class
                                SDL_AddTimer(std::chrono::milliseconds(cfg.dim_idle_timer.delay).count(), idle_timer_cb, &iti);
                            }
                        }
                        else
                        {
                            iti.signal_user_activity();
                            handle_other_event(ev, ctx);
                        }
                    }
                    else
                    {
                        handle_other_event(ev, ctx);
                    }
                }

                // Avoid unnecessary I/O on slower devices.
                if (refresh_cover)
                {
                    cover_type ct;
                    if (cfg.cover.opt_directory.has_value())
                    {
                        auto opt_cover_path = find_cover_file(current_song_path, cfg.cover.opt_directory.value(), cfg.cover.names, cfg.cover.extensions);

                        if (opt_cover_path.has_value())
                        {
                            ct = cover_type::IMAGE;
                            cv->set_cover(std::move(load_texture_from_image(renderer, opt_cover_path.value())));
                        }
                        else
                        {
                            ct = cover_type::SONG_INFO;
                        }
                    }
                    else
                    {
                        ct = cover_type::SONG_INFO;
                    }

                    if (ct == cover_type::SONG_INFO)
                    {
                        cv->set_cover(mpdc.get_current_title(), mpdc.get_current_artist(), mpdc.get_current_album());
                    }
                    refresh_cover = false;
                }

                ctx.draw_dirty();
            }
        }
    }
    catch (std::exception const & e)
    {
        std::cerr << e.what() << std::endl;
    }

    mpdc.stop();
    mpdc_thread.join();

    if (cfg.opt_port.has_value())
    {
        opt_udp_control.value().stop();
        udp_thread.join();
    }

    return result;
}

