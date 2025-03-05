#pragma once
#include "view.h"
#include "../text.h"
#include "imageview.h"

namespace UI {
    class ScrollBar: public View {
    public:
    /*
         ^    <-- arrows
         |   <--- trackbar
         |
         []  <--- slider
         |
         v
    */
        ScrollBar(EGRect frame): View(frame) {}
        int maximum = 100;
        int page_size = 32;
        int current_offset = 0;

        void render(EGGraphBuf * buf) override {
            // draw up down arrows
            EGGraphBuf tmp = {
                .fmt = EG_FMT_HORIZONTAL,
                .size = {5, 4},
                .data = (EGRawGraphBuf) arrow_up_data
            };
            EGBlitBuffer(buf, EGPointZero, &tmp);
            tmp.data = (EGRawGraphBuf) arrow_down_data;
            EGBlitBuffer(buf, {0, frame.size.height - 4}, &tmp);

            // draw trackbar
            int midpoint_x = frame.size.width / 2;
            int trackbar_height = frame.size.height - 8; // 4 at both top and bottom for the arrows
            EGDrawLine(buf, {midpoint_x, 4}, {midpoint_x, 4 + trackbar_height - 1});

            // draw slider
            int scale_factor = std::max(1, maximum / trackbar_height);
            int top_of_slider_scaled = current_offset / scale_factor;
            int bottom_of_slider_scaled = (current_offset + page_size) / scale_factor;
            ESP_LOGD("ScrlBar", "MAX = %i, PS = %i, POS = %i -> SCALE = %i, TOP = %i, BOTT = %i", maximum, page_size, current_offset, scale_factor, top_of_slider_scaled, bottom_of_slider_scaled);
            EGDrawRect(buf, {{midpoint_x - 1, 4 + top_of_slider_scaled}, {3, bottom_of_slider_scaled - top_of_slider_scaled}}, true);

            View::render(buf);
        }

        protected:
            const uint8_t arrow_up_data[4] = {
                0b00100000,
                0b01110000,
                0b11111000,
                0b00000000,
            };
            const uint8_t arrow_down_data[4] = {
                0b00000000,
                0b11111000,
                0b01110000,
                0b00100000,
            };
    };

    class ScrollView: public View {
        public:
            const std::shared_ptr<UI::View> contentView;
            ScrollView(EGRect frame, EGSize contentSize): 
                contentView(std::make_shared<UI::View>(UI::View({EGPointZero, contentSize}))),
                scrollBar(std::make_shared<ScrollBar>(EGRect {{frame.size.width - 5, 0}, {5, frame.size.height}})),
                View(frame) {
                    subviews.push_back(contentView);
                    subviews.push_back(scrollBar);
                }

            virtual ~ScrollView() = default;

            void set_content_offset(EGPoint offset) {
                contentView->frame.origin = {-offset.x, -offset.y};
                scrollBar->maximum = contentView->frame.size.height;
                scrollBar->page_size = frame.size.height;
                scrollBar->current_offset = offset.y;
                scrollBar->set_needs_display();
                set_needs_display();
            }

            EGPoint content_offset() {
                return {-contentView->frame.origin.x, -contentView->frame.origin.y};
            }
        protected:
            const std::shared_ptr<ScrollBar> scrollBar;
    };

    class ListItem: public View {
        public:
            ListItem(const std::string title, bool disclosureIndicator = false, const UI::Image * icon = nullptr, const Fonts::Font* font = Fonts::FallbackWildcard16px):
                _font(font),
                _title(title),
                _icon(icon),
                disclosure(disclosureIndicator),
                View({EGPointZero, {0, font->size.height}}) {
            }
            virtual ~ListItem() = default;

            void render(EGGraphBuf * buf) override {
                if(_icon != nullptr) {
                    const EGGraphBuf tmp_buf = {
                        .fmt = _icon->format,
                        .size = _icon->size,
                        .data = (EGRawGraphBuf) _icon->data
                    };
                    EGBlitBuffer(buf, {left_margin, 0}, &tmp_buf);
                }
                Fonts::EGFont_put_string(_font, _title.c_str(), {left_margin + ((_icon == nullptr) ? 0 : (_icon->size.width + left_margin)), 0}, buf);
                if(disclosure) {
                    Fonts::EGFont_put_string(_font, "\x10" /* in keyrus0808 it's a filled right arrow in CP866 */, {frame.size.width - _font->size.width - left_margin, 0}, buf);
                }
                if(is_selected) {
                    EGBufferInvert(buf);
                }
                View::render(buf);
            }

            virtual void set_selected(bool selected) {
                is_selected = selected;
                set_needs_display();
            }
        protected:
            bool is_selected = false;
            bool disclosure;
            int left_margin = 2;
            const Fonts::Font* _font;
            const std::string _title;
            const UI::Image* _icon;
    };

    class ListView: public ScrollView {
        public:
            ListView(EGRect frame, const std::vector<std::shared_ptr<ListItem>> items):
                _items(items),
                ScrollView(frame, {frame.size.width - 6, 0})
            {
                for(auto& item: _items) {
                    item->frame.origin.y = contentView->frame.size.height;
                    item->frame.size.width = contentView->frame.size.width;
                    contentView->frame.size.height += item->frame.size.height;
                    contentView->subviews.push_back(item);
                }
                if(!_items.empty()) select(0);
            }

            void select(int idx) {
                if(selection < _items.size() && selection >= 0) _items[selection]->set_selected(false);
                if(idx < _items.size() && idx >= 0) {
                    selection = idx;
                    _items[idx]->set_selected(true);

                    ESP_LOGD("ListView", "Content Offset = {%i, %i}, New selection frame = {%i, %i, %i, %i}",
                         content_offset().x, content_offset().y, 
                         _items[idx]->frame.origin.x, _items[idx]->frame.origin.y, _items[idx]->frame.size.width, _items[idx]->frame.size.height);

                    if(content_offset().y + frame.size.height <= _items[idx]->frame.origin.y) {
                        set_content_offset({0, _items[idx]->frame.origin.y - _items[idx-1]->frame.size.height});
                    }
                    else if(_items[idx]->frame.origin.y < content_offset().y) {
                        set_content_offset({0, _items[idx]->frame.origin.y});
                    }
                }
            }

            void down() {
                if(selection >= _items.size() - 1) {
                    select(0);
                } else {
                    select(selection + 1);
                }
            }

            void up() {
                if(selection == 0) {
                    select(_items.size() - 1);
                } else {
                    select(selection - 1);
                }
            }

        protected:
            const std::vector<std::shared_ptr<ListItem>> _items;
            int selection = -1;
    };
}