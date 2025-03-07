#pragma once
#include <esp32-hal-log.h>
#include <esper-gui/views/framework.h>
#include <esper-cdp/types.h>

using UI::Label;
using UI::ProgressBar;
using std::shared_ptr;
using std::make_shared;

class TimeBar: public UI::View {
public:
    shared_ptr<Label> lblLeft;
    shared_ptr<Label> lblRight;
    shared_ptr<ProgressBar> trackbar;

    TimeBar(EGRect frame): View(frame) {
        lblLeft = make_shared<Label>(EGRect {EGPointZero, {20, frame.size.height}}, Fonts::TinyDigitFont, Label::Alignment::Right);
        lblRight = make_shared<Label>(EGRect {{frame.size.width - 20, 0}, {20, frame.size.height}}, Fonts::TinyDigitFont, Label::Alignment::Left);
        trackbar = make_shared<ProgressBar>(EGRect {{lblLeft->frame.size.width + 1, 0},{frame.size.width - lblLeft->frame.size.width - lblRight->frame.size.width - 2, frame.size.height}});

        subviews.push_back(lblLeft);
        subviews.push_back(lblRight);
        subviews.push_back(trackbar);
    }

    void update_msf(MSF start, MSF cur, MSF duration, bool is_pos_negative) {
        if(cur.M != cur_msf.M || cur.S != cur_msf.S || is_pos_negative != negative) {
            char buf[8] = { 0 };
            snprintf(buf, 8, "%d:%02d", cur.M, cur.S);
            lblLeft->set_value((is_pos_negative ? "-" : "") + std::string(buf));
            trackbar->value = MSF_TO_FRAMES(cur);
        }
        cur_msf = cur;
        negative = is_pos_negative;

        if(dur_msf.M != duration.M || dur_msf.S != duration.S || start.M != start_pos_msf.M || start.S != start_pos_msf.S) {
            int duration_frames = MSF_TO_FRAMES(duration);
            int duration_sec = (duration_frames / MSF::FRAMES_IN_SECOND) % 60;
            int duration_min = (duration_frames / MSF::FRAMES_IN_SECOND) / 60;

            trackbar->maximum = duration_frames - 2*MSF::FRAMES_IN_SECOND; // add some leeway for the bar to appear full on the last 2 seconds
            trackbar->value = MSF_TO_FRAMES(cur_msf);
            
            char buf[9] = { 0 };
            snprintf(buf, 9, "%d:%02d", duration_min, duration_sec);
            lblRight->set_value(std::string(buf));
        }
        
        if(is_pos_negative) {
            trackbar->value = trackbar->minimum;
        }

        start_pos_msf = start;
        dur_msf = duration;
    }

protected:
    MSF start_pos_msf;
    MSF cur_msf;
    MSF dur_msf;
    bool negative;
};