#pragma once
#include <esper-gui/views/framework.h>
using std::make_shared;
using std::shared_ptr;

class ChannelGridBar: public UI::View {
public:
    ChannelGridBar(EGRect f, int channel_count = 6): View(f) {
        auto font = Fonts::TinyDigitFont;
        int channel_width = frame.size.width / channel_count;
        int channel_margin = channel_width/2 - font->size.width/2; // assuming one digit

        for(int i = 1; i <= channel_count; i++) {
            auto lbl = make_shared<UI::Label>(EGRect {{(i-1)*channel_width + channel_margin, 0}, {font->size.width, font->size.height}}, font, UI::Label::Alignment::Center, std::to_string(i));
            subviews.push_back(lbl);
        }
    }

    void render(EGGraphBuf * buf) override {
        for(int i = 0; i < subviews.size(); i++) {
            auto lbl = subviews[i];
            if(i == active_ch_idx) {
                EGDrawLine(buf, {line_left_cur, lbl->frame.origin.y + lbl->frame.size.height + 1}, {line_right_cur, lbl->frame.origin.y + lbl->frame.size.height + 1});
            }
        }
        View::render(buf);
    }

    bool needs_display() override {
        if(line_right_tgt != line_right_cur || line_left_tgt != line_left_cur) {
            bool going_right = (line_right_tgt > line_right_cur || line_left_tgt > line_left_cur);
            int* first_coord = going_right ? &line_right_cur : &line_left_cur;
            int* first_coord_tgt = going_right ? &line_right_tgt : &line_left_tgt;
            int* second_coord = !going_right ? &line_right_cur : &line_left_cur;
            int* second_coord_tgt = !going_right ? &line_right_tgt : &line_left_tgt;

            if(*first_coord_tgt > *first_coord) {
                *first_coord += std::min(line_speed/2, *first_coord_tgt - *first_coord);
                line_speed = std::min(32, line_speed+1);
            }
            else if(*first_coord_tgt < *first_coord) {
                *first_coord -= std::min(line_speed/2, *first_coord - *first_coord_tgt);
                line_speed = std::min(32, line_speed+1);
            }
            else if(*second_coord_tgt > *second_coord) {
                *second_coord += std::min(line_speed/2, *second_coord_tgt - *second_coord);
                line_speed = std::max(2, line_speed - 1);
            }
            else if(*second_coord_tgt < *second_coord) {
                *second_coord -= std::min(line_speed/2, *second_coord - *second_coord_tgt);
                line_speed = std::max(2, line_speed - 1);
            }

            set_needs_display();
        }
        return View::needs_display();
    }

    void set_active_ch_idx(int num) {
        active_ch_idx = num;
        if(num < 0 || num > subviews.size()) {
            line_left_cur = 0;
            line_right_cur = 0;
            line_left_tgt = 0;
            line_right_tgt = 0;
            set_needs_display();
            return;
        }

        if(line_left_cur == line_left_tgt && line_right_cur == line_right_tgt) line_speed = 2;
        auto lbl = subviews[num];
        if(line_left_cur == 0) line_left_cur = lbl->frame.origin.x - 10;
        if(line_right_cur == 0) line_right_cur = lbl->frame.origin.x - 9;
        line_left_tgt = lbl->frame.origin.x - 1;
        line_right_tgt = lbl->frame.origin.x + lbl->frame.size.width + 1;
        set_needs_display();
    }

private:
    int active_ch_idx = -1;
    int line_speed = 2;
    
    int line_left_cur = 0;
    int line_right_cur = 0;
    int line_left_tgt = 0;
    int line_right_tgt = 0;
};