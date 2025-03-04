#pragma once
#include "view.h"
#include "../text.h"
#include "imageview.h"

namespace UI {
    class ScrollView: public View {
        public:
            const std::shared_ptr<UI::View> contentView;
            ScrollView(EGRect frame, EGSize contentSize): 
                contentView(std::make_shared<UI::View>(UI::View({EGPointZero, contentSize}))),
                View(frame) {
                    subviews.push_back(contentView);
                }

            virtual ~ScrollView() = default;

            void set_content_offset(EGPoint offset) {
                contentView->frame.origin = {-offset.x, -offset.y};
                set_needs_display();
            }

            EGPoint content_offset() {
                return {-contentView->frame.origin.x, -contentView->frame.origin.y};
            }
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
                ScrollView(frame, {frame.size.width, 0})
            {
                for(auto& item: _items) {
                    item->frame.origin.y = contentView->frame.size.height;
                    item->frame.size.width = frame.size.width;
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

                    ESP_LOGI("ScrollView", "Content Offset = {%i, %i}, New selection frame = {%i, %i, %i, %i}",
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