#pragma once
#include <esper-gui/views/framework.h>
#include <esper-core/platform.h>
#include <esper-core/prefs.h>
#include <stack>
#include <tuple>

namespace {
    static const uint8_t switch_off_data[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0xFF, 0xF0,
        0x1F, 0xF0, 0x08, 0x31, 0xF8, 0xE4, 0x67, 0xFD, 0x12, 
        0x6F, 0xFD, 0x12, 0x7F, 0xFD, 0x32, 0x7F, 0xFD, 0x52, 
        0x7F, 0xFD, 0x92, 0x7F, 0xFD, 0x12, 0x3F, 0xF8, 0xE4, 
        0x1F, 0xF0, 0x08, 0x0F, 0xFF, 0xF0, 0x00, 0x00, 0x00, 
        0x00, 0x00, 0x00,
    };
    
    static const EGImage switch_off = {
        .format = EG_FMT_HORIZONTAL,
        .size = {24, 16},
        .data = switch_off_data
    };

    static const uint8_t switch_on_data[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0xFF, 0xF0,
        0x10, 0x0F, 0xF8, 0x22, 0x18, 0xFC, 0x46, 0x33, 0xFE, 
        0x42, 0x37, 0xFE, 0x42, 0x3F, 0xFE, 0x42, 0x3F, 0xFE, 
        0x42, 0x3F, 0xFE, 0x42, 0x3F, 0xFE, 0x27, 0x1F, 0xFC, 
        0x10, 0x0F, 0xF8, 0x0F, 0xFF, 0xF0, 0x00, 0x00, 0x00, 
        0x00, 0x00, 0x00,
    };

    static const EGImage switch_on = {
        .format = EG_FMT_HORIZONTAL,
        .size = {24, 16},
        .data = switch_on_data
    };
}

class MenuNavigator;
class MenuPresentable: public virtual UI::View {
public:
    virtual void on_presented() {}
    virtual void on_dismissed() {}
    virtual void on_key_pressed(VirtualKey, MenuNavigator*) {}
};

class MenuNavigator: public UI::View {
public:
    MenuNavigator(std::shared_ptr<MenuPresentable> root, EGRect f): back_stack{}, current{root}, UI::View(f) {
        set_current(root);
    }

    void push(std::shared_ptr<MenuPresentable> next_view) {
        back_stack.push(current);
        current->on_dismissed();
        set_current(next_view);
    }

    void pop() {
        if(back_stack.empty()) return;
        current->on_dismissed();
        set_current(back_stack.top());
        back_stack.pop();
    }

    void pop_to_root() {
        while(!back_stack.empty()) {
            auto tmp = back_stack.top();
            back_stack.pop();
            if(back_stack.empty()) {
                set_current(tmp);
            }
        }
    }

    bool can_go_up() const {
        return !back_stack.empty();
    }

    void on_key_pressed(VirtualKey k) { current->on_key_pressed(k, this); }

private:
    std::shared_ptr<MenuPresentable> current;

    void set_current(std::shared_ptr<MenuPresentable> cur) {
        subviews.clear();
        cur->frame = {EGPointZero, {frame.size.width, frame.size.height}};
        ESP_LOGI("MenuNav", "Cur new w=%i h=%i", cur->frame.size.width, cur->frame.size.height);
        subviews.push_back(cur);
        current = cur;
        current->on_presented();
    }
    std::stack<std::shared_ptr<MenuPresentable>> back_stack;
};


class InfoMessageBox: public MenuPresentable {
public:
    InfoMessageBox(const std::string& msg, const std::function<void(MenuNavigator *)> okAction = [](MenuNavigator* h){ h->pop(); }): 
        action(okAction),
        lbl(std::make_shared<UI::Label>(EGRectZero, Fonts::FallbackWildcard16px, UI::Label::Alignment::Center, msg)),
        quasiButton(std::make_shared<UI::Label>(EGRectZero, Fonts::FallbackWildcard8px, UI::Label::Alignment::Center, "OK")),
        UI::View() {
            subviews.push_back(lbl);
            subviews.push_back(quasiButton);
    }

    void on_presented() override {
        lbl->frame = { {2, 2}, {frame.size.width - 4, 16} };
        quasiButton->frame = {{frame.size.width / 2 - 8, frame.size.height - 11}, {16, 8}};
    }

    void render(EGGraphBuf * buf) override {
        EGRect frameRect = {EGPointZero, frame.size};
        EGDrawRect(buf, frameRect);
        EGRect quasiButtonRect = EGRectInset(quasiButton->frame, -16, -2);
        EGDrawRect(buf, quasiButtonRect);
        MenuPresentable::render(buf);
    }

    void on_key_pressed(VirtualKey k, MenuNavigator * host) override {
        if(k == RVK_CURS_ENTER) {
            action(host);
        }
    }
private:
    const std::shared_ptr<UI::Label> lbl;
    const std::shared_ptr<UI::Label> quasiButton;
    const std::function<void(MenuNavigator *)> action;
};

class MenuNode {
public:
    MenuNode(const std::string t, const EGImage* img = nullptr): title{t}, icon{img} {}
    virtual ~MenuNode() = default;

    const std::string title;
    const EGImage * icon;
    virtual void execute(MenuNavigator * host) const { ESP_LOGE("MenuNode", "Did you forget to override execute()?"); }
    virtual void draw_accessory(EGGraphBuf* buf, EGSize bounds) const {}
};

class ActionMenuNode: public MenuNode {
public:
    ActionMenuNode(const std::string title, const std::function<void(MenuNavigator *)> action, const EGImage * icon = nullptr):
        _act(action),
        MenuNode(title, icon) {
        }

    void execute(MenuNavigator * host) const override {
        _act(host);
    }

protected:
    const std::function<void(MenuNavigator *)> _act;
};

class TogglePreferenceMenuNode: public MenuNode {
public:
    TogglePreferenceMenuNode(const std::string title, const Prefs::Key<bool> key, const EGImage * icon = nullptr):
        _pref(key),
        MenuNode(title, icon)
     {}

    void execute(MenuNavigator * host) const override {
        bool current = Prefs::get(_pref);
        Prefs::set(_pref, !current);
        host->set_needs_display();
    }

    void draw_accessory(EGGraphBuf* buf, EGSize bounds) const override {
        const EGImage * img = (Prefs::get(_pref) ? &switch_on : &switch_off);
        EGBlitImage(buf, {bounds.width - img->size.width, bounds.height/2 - img->size.height/2}, img);
    }

protected:
    Prefs::Key<bool> _pref;
};

class DetailTextMenuNode : public ActionMenuNode {
public:
    DetailTextMenuNode(const std::string& title, const std::string& detailText, const std::function<void(MenuNavigator *)> action = [](MenuNavigator*){}, const EGImage* icon = nullptr)
        : ActionMenuNode(title, action, icon), detailText(detailText) {}

    void draw_accessory(EGGraphBuf* buf, EGSize bounds) const override {
        EGSize str_size = Fonts::EGFont_measure_string(Fonts::FallbackWildcard16px, detailText.c_str());
        Fonts::EGFont_put_string(Fonts::FallbackWildcard16px, detailText.c_str(), {bounds.width - str_size.width, bounds.height / 2 - str_size.height / 2}, buf);
        MenuNode::draw_accessory(buf, bounds);
    }
protected:
    mutable std::string detailText;
};

class ListMenuNode: public MenuPresentable, public MenuNode,  public UI::ListView {
public:
    // Explanation: https://stackoverflow.com/a/79487087/565185 -- TODO figure out how this works
    template <typename... Ts>
    ListMenuNode(const std::string title,
                const EGImage * icon,
                std::tuple<Ts...>&& items) :
        subnodes(std::apply([](auto&&... args) -> std::vector<std::shared_ptr<MenuNode>>
                            {
                                return { std::make_shared<Ts>(std::move(args))... };
                            }, std::move(items))),
        UI::ListView(EGRectZero, {}),
        MenuNode(title, icon)
    {
        set_subnodes(subnodes);
    }

    void set_subnodes(std::vector<std::shared_ptr<MenuNode>> new_subnodes) {
        subnodes = new_subnodes;
        std::vector<std::shared_ptr<UI::ListItem>> viewItems = {};
        for(auto& subnode: subnodes) {
            viewItems.push_back(std::make_shared<UI::ListItem>(subnode->title, [subnode](EGGraphBuf *b, EGSize s) { subnode->draw_accessory(b, s); }, subnode->icon));
        }
        set_items(viewItems);   
        layout_items();
    }

    void draw_accessory(EGGraphBuf* buf, EGSize bounds) const override { UI::ListItem::DisclosureIndicatorDrawingFunc(buf, bounds); }
    
    void execute(MenuNavigator * host) const override {
        host->push(std::make_shared<ListMenuNode>(*this));
    }

    void on_presented() override {
        // update frames of things after push has set our frame correctly
        int last_sel = selection;
        layout_items();
        select(last_sel);
        set_needs_display();
    }

    void on_key_pressed(VirtualKey k, MenuNavigator* host) override {
        if(k == RVK_CURS_DOWN) down();
        else if(k == RVK_CURS_UP) up();
        else if(k == RVK_CURS_ENTER) {
            subnodes[selection]->execute(host);
        }
        else if(k == RVK_CURS_LEFT) {
            if(host->can_go_up()) {
                if(selection < _items.size() && selection >= 0) 
                    _items[selection]->set_selected(false);
                host->pop();
                selection = 0;
            }
        }
    }
protected:
    std::vector<std::shared_ptr<MenuNode>> subnodes;
};

class TextEditor: public MenuPresentable, public MenuNode {
public:
    std::string edited_string;
    TextEditor(std::string title):
        edited_string(""),
        editingFont(Fonts::FallbackWildcard16px),
        _leftLbl(std::make_shared<UI::Label>(EGRectZero, editingFont, UI::Label::Alignment::Right)),
        _curLbl(std::make_shared<UI::Label>(EGRectZero, editingFont, UI::Label::Alignment::Center)),
        _rightLbl(std::make_shared<UI::Label>(EGRectZero, editingFont, UI::Label::Alignment::Left)),
        MenuPresentable(),
        MenuNode(title) {
            subviews.push_back(_leftLbl);
            subviews.push_back(_curLbl);
            subviews.push_back(_rightLbl);
        }

    void on_presented() override {
        ESP_LOGI("TextEdit", "string before edit = '%s'", edited_string.c_str());
        str_pos = edited_string.size();
        int vert_center = frame.size.height/2 - editingFont->size.height/2;
        _curLbl->frame = {{frame.size.width / 2 - editingFont->size.width/2, vert_center}, editingFont->size};
        _leftLbl->frame = {{0, vert_center}, {_curLbl->frame.origin.x - 5, editingFont->size.height}};
        _rightLbl->frame = {{_curLbl->frame.origin.x + _curLbl->frame.size.width + 5, vert_center}, {frame.size.width - (_curLbl->frame.origin.x + _curLbl->frame.size.width + 5), editingFont->size.height}};
        update_labels();
    }

    void draw_accessory(EGGraphBuf* buf, EGSize bounds) const override { UI::ListItem::DisclosureIndicatorDrawingFunc(buf, bounds); }

    void render(EGGraphBuf * buf) override {
        EGRect cursorRect = EGRectInset(_curLbl->frame, -1, -1);
        EGDrawRect(buf, cursorRect);

        int y = _curLbl->frame.origin.y - editingFont->size.height - 1;
        int cur_idx = cur_wheel_idx;
        do {
            if(cur_idx == 0) cur_idx = input_wheel_length - 1;
            else cur_idx--;
            auto glyph = Fonts::EGFont_glyph(editingFont, input_wheel[cur_idx]);
            EGBlitBuffer(buf, {_curLbl->frame.origin.x, y}, &glyph);
            y -= editingFont->size.height;
        } while(y > -editingFont->size.height);

        cur_idx = cur_wheel_idx;
        y = _curLbl->frame.origin.y + editingFont->size.height + 1;
        do {
            if(cur_idx == input_wheel_length - 1) cur_idx = 0;
            else cur_idx++;
            auto glyph = Fonts::EGFont_glyph(editingFont, input_wheel[cur_idx]);
            EGBlitBuffer(buf, {_curLbl->frame.origin.x, y}, &glyph);
            y += editingFont->size.height;
        } while(y < (frame.size.height + editingFont->size.height));

        View::render(buf);
    }

    void on_dismissed() {
        if(edited_string.size() > 0 && edited_string.back() == ' ') {
            edited_string.pop_back();
        }
        ESP_LOGI("TextEdit", "string after edit = '%s'", edited_string.c_str());
    }

    void execute(MenuNavigator * host) const override {
        host->push(std::make_shared<TextEditor>(*this));
    }

    void on_key_pressed(VirtualKey k, MenuNavigator* host) override {
        if(k == RVK_CURS_LEFT || k == RVK_TRACK_PREV) {
            str_pos = std::max(0, (str_pos - 1));
            update_labels();
        }
        else if(k == RVK_CURS_RIGHT || k == RVK_TRACK_NEXT) {
            str_pos = std::min((int)edited_string.size(), str_pos + 1);
            update_labels();
        }
        else if(k == RVK_CURS_ENTER) {
            host->pop();
        }
        else if(k == RVK_CURS_DOWN) {
            if(cur_wheel_idx == input_wheel_length - 1) cur_wheel_idx = 0;
            else cur_wheel_idx++;
            set_cur_char_from_wheel();
        }
        else if(k == RVK_CURS_UP) {
            if(cur_wheel_idx == 0) cur_wheel_idx = input_wheel_length - 1;
            else cur_wheel_idx--;
            set_cur_char_from_wheel();
        }
        else if(k == RVK_DEL) {
            if(str_pos < edited_string.size()) {
                edited_string.erase(str_pos, 1);
                update_labels();
            }
            else if(str_pos == edited_string.size() && str_pos > 0) {
                edited_string.erase(str_pos - 1, 1);
                str_pos--;
                update_labels();
            }
        }
        
        if(k >= RVK_START_OF_NUMBERS && k < RVK_END_OF_NUMBERS) {
            if(k != last_letter_key && str_pos == (edited_string.size() - 1)) {
                // auto advance when typing at the end
                str_pos++;
            }
            last_letter_key = k;

            switch(k) {
                case RVK_NUM_0: // SPC
                    move_wheel_to_char(' ');
                    break;
                
                case RVK_NUM_1: // SYM
                    if(cur_wheel_idx < 53 || cur_wheel_idx == (input_wheel_length - 1)) cur_wheel_idx = 53;
                    else cur_wheel_idx++;
                    break;

                case RVK_NUM_2: // ABCabc
                    handle_letter_button('A', 'C', 'a', 'c');
                    break;

                case RVK_NUM_3: // DEFdef
                    handle_letter_button('D', 'F', 'd', 'f');
                    break;

                case RVK_NUM_4: // GHIghi
                    handle_letter_button('G', 'I', 'g', 'i');
                    break;

                case RVK_NUM_5: // JKLjkl
                    handle_letter_button('J', 'L', 'j', 'l');
                    break;

                case RVK_NUM_6: // MNOmno
                    handle_letter_button('M', 'O', 'm', 'o');
                    break;

                case RVK_NUM_7: // PQRSpqrs
                    handle_letter_button('P', 'S', 'p', 's');
                    break;

                case RVK_NUM_8: // TUVtuv
                    handle_letter_button('T', 'V', 't', 'v');
                    break;

                case RVK_NUM_9: // WXYZwxyz
                    handle_letter_button('W', 'Z', 'w', 'z');
                    break;

                default: break;
            }
            set_cur_char_from_wheel();
            
        } else {
            last_letter_key = RVK_MAX_INVALID;
        }
    }

protected:
    int str_pos = 0;
    const Prefs::Key<std::string> _key;
    const Fonts::Font * editingFont;
    const std::shared_ptr<UI::Label> _leftLbl;
    const std::shared_ptr<UI::Label> _curLbl;
    const std::shared_ptr<UI::Label> _rightLbl;
    const std::shared_ptr<UI::Label> _titleLbl;

    const char * input_wheel = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.,@!?<>;:{}[]\"'#$%&()+-*/=|";
    const int input_wheel_length = strlen(input_wheel);
    int cur_wheel_idx = 0;
    bool last_letter_was_lower = false;
    VirtualKey last_letter_key = RVK_MAX_INVALID;

    void handle_letter_button(const char upper_start, const char upper_end, const char lower_start, const char lower_end) {
        if(input_wheel[cur_wheel_idx] == upper_end) {
            move_wheel_to_char(lower_start);
        }
        else if(input_wheel[cur_wheel_idx] == lower_end) {
            move_wheel_to_char(upper_start);
        }
        else if((input_wheel[cur_wheel_idx] >= upper_start && input_wheel[cur_wheel_idx] <= upper_end) || (input_wheel[cur_wheel_idx] >= lower_start && input_wheel[cur_wheel_idx] <= lower_end)) {
            cur_wheel_idx++;
        }
        else move_wheel_to_char(last_letter_was_lower ? lower_start : upper_start);
    }

    void set_cur_char_from_wheel() {
        if(str_pos < edited_string.size()) {
            edited_string[str_pos] = input_wheel[cur_wheel_idx];
        }
        else {
            edited_string.push_back(input_wheel[cur_wheel_idx]);
        }
        update_labels();
    }

    void move_wheel_to_char(char c) {
        for(int i = 0; i < input_wheel_length; i++) {
            if(input_wheel[i] == c) {
                cur_wheel_idx = i;
                break;
            }
        }
    }

    void update_labels() {
        _leftLbl->set_value(edited_string.substr(0, str_pos));
        if(str_pos < edited_string.size()) {
            _curLbl->set_value(edited_string.substr(str_pos, 1));
        } else {
            _curLbl->set_value("");
        }
        if((str_pos + 1) < edited_string.size()) {
            _rightLbl->set_value(edited_string.substr(str_pos + 1));
        } else {
            _rightLbl->set_value("");
        }

        if(str_pos < edited_string.size()) {
            move_wheel_to_char(edited_string[str_pos]);
            last_letter_was_lower = (edited_string[str_pos] >= 'a' && edited_string[str_pos] <= 'z');
        }

        set_needs_display();
    }
};

class TextPreferenceEditorNode: public TextEditor {
public:
TextPreferenceEditorNode(std::string title, const Prefs::Key<std::string> key):
    _key(key),
    TextEditor(title) {}

    void on_presented() override {
        edited_string = Prefs::get(_key);
        TextEditor::on_presented();
    }

    void on_dismissed() override {
        TextEditor::on_dismissed();
        Prefs::set(_key, edited_string);
    }

    void execute(MenuNavigator * host) const override {
        host->push(std::make_shared<TextPreferenceEditorNode>(*this));
    }

protected:
    std::string value;
    const Prefs::Key<std::string> _key;
};
