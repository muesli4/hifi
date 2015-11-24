#include <chrono>
#include <thread>

#include "mpd_control.hpp"
#include "util.hpp"

mpd_control::mpd_control(std::function<void(std::string)> new_song_path_cb)
    : _c(mpd_connection_new(0, 0, 0))
    , _run(true)
    , _new_song_path_cb(new_song_path_cb)
{
}

mpd_control::~mpd_control()
{
    mpd_connection_free(_c);
}

void mpd_control::run()
{
    mpd_song * last_song = mpd_run_current_song(_c);

    if (last_song != 0) new_song_cb(last_song);

    while (_run)
    {
        mpd_send_idle_mask(_c, MPD_IDLE_PLAYER);

        // TODO use condition variable here!
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (mpd_run_noidle(_c) != 0)
        {

            mpd_song * song = mpd_run_current_song(_c);

            if (song != 0)
            {
                if (last_song != 0)
                {
                    if (mpd_song_get_id(song) != mpd_song_get_id(last_song))
                    {
                        new_song_cb(song);
                    }
                    mpd_song_free(last_song);
                }
            }
            last_song = song;
        }
        scoped_lock lock(_external_tasks_mutex);

        while (!_external_tasks.empty())
        {
            _external_tasks.front()(_c);
            _external_tasks.pop_front();
        }

    }
    mpd_song_free(last_song);
}

void mpd_control::stop()
{
    _run = false;
}

void mpd_control::toggle_pause()
{
    add_external_task([](mpd_connection * c){ mpd_run_toggle_pause(c); });
}

void mpd_control::inc_volume(unsigned int amount)
{
    add_external_task([amount](mpd_connection * c)
    {
        mpd_status * s = mpd_run_status(c);
        int new_volume = std::min(100, mpd_status_get_volume(s) + static_cast<int>(amount));
        mpd_run_set_volume(c, new_volume);
    });
}

void mpd_control::dec_volume(unsigned int amount)
{
    add_external_task([amount](mpd_connection * c)
    {
        mpd_status * s = mpd_run_status(c);
        int new_volume = std::max(0, mpd_status_get_volume(s) - static_cast<int>(amount));
        mpd_run_set_volume(c, new_volume);
    });
}

void mpd_control::next_song()
{
    add_external_task([](mpd_connection * c){ mpd_run_next(c); });
}

void mpd_control::prev_song()
{
    add_external_task([](mpd_connection * c){ mpd_run_previous(c); });
}

void mpd_control::add_external_task(std::function<void(mpd_connection *)> t)
{
    scoped_lock lock(_external_tasks_mutex);
    _external_tasks.push_back(t);
}

void mpd_control::new_song_cb(mpd_song * s)
{
    _new_song_path_cb(mpd_song_get_uri(s));
}
