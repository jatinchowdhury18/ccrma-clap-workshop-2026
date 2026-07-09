#pragma once

#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <functional>

#include <readerwriterqueue.h>
#include <visage/app.h>
#include <visage/widgets.h> 
#include "embedded/fonts.h"


// @WORKSHOP: the GUI and the audio thread never share mutable data directly.
// Each direction gets its own single-producer single-consumer lock-free queue

struct To_UI          // audio thread -> UI;; indicates a param change
{
    uint32_t param_id;
    double   value;
};

struct From_UI    
{    // UI -> audio thread; indicates a gesture
    enum Type { Begin_Edit, End_Edit, Adjust_Value } type;
    uint32_t param_id;
    double   value;
};


static constexpr uint32_t GUI_WIDTH  = 420;
static constexpr uint32_t GUI_HEIGHT = 300;

////////////
// slider //
////////////
// A visage::Frame is a rectangle in the UI tree with its own draw() and mouse
// handlers. The parent positions it with setBounds(); inside the frame, both
// drawing and mouse coordinates are LOCAL: (0, 0) is this frame's top-left
// corner, and width()/height() are this frame's size, not the window's.

struct Slider : visage::Frame
{
    uint32_t    param_id;
    const char *label;
    float       min_value;
    float       max_value;
    const char *value_format; // snprintf format for the value readout, just like in the param value -> text CLAP function

    float value; // current value, natural units — the UI-side copy

    // we give our slider a reference (not a copy!!) of the from_ui_q
    // that way, the slider can send a message to the audio thread
    // when the user moves a slider
    moodycamel::ReaderWriterQueue<From_UI> &from_ui_q;
    std::function<void()>                   request_param_flush;

    visage::Font font { 15.0f, visage::fonts::Lato_Regular_ttf };

    static constexpr float LABEL_H = 22.0f; // label row on top, track below

    Slider(uint32_t id, const char *label_, float min, float max, const char *format,
           moodycamel::ReaderWriterQueue<From_UI> &q, std::function<void()> flush)
        : param_id(id), label(label_), min_value(min), max_value(max),
          value_format(format), value(min), from_ui_q(q),
          request_param_flush(std::move(flush)) {}

    // called from outside (the editor's timer, guiCreate) 
    // so we redraw to update the visual state
    void set_value(double v) {
        value = (float) v;
        redraw();
    }

    float norm() const { return (value - min_value) / (max_value - min_value); }

    void draw(visage::Canvas &canvas) override {
        char text[32];
        std::snprintf(text, sizeof text, value_format, (double) value);

        canvas.setColor(0xffb8b8c0); // label + value readout
        canvas.text(label, font, visage::Font::kLeft,  0, 0, width(), LABEL_H - 4.0f);
        canvas.text(text,  font, visage::Font::kRight, 0, 0, width(), LABEL_H - 4.0f);
        canvas.setColor(0xff3a3a44); // track
        canvas.rectangle(0, LABEL_H, width(), height() - LABEL_H);
        canvas.setColor(0xff8ab4ff); // fill up to the current value
        canvas.rectangle(0, LABEL_H, width() * norm(), height() - LABEL_H);
    }

    void apply_drag(float mouse_x) {
        const float n = std::clamp(mouse_x / width(), 0.0f, 1.0f);
        value = min_value + (max_value - min_value) * n;
        from_ui_q.try_enqueue({From_UI::Adjust_Value, param_id, (double) value});
        request_param_flush(); // audio not running? make the host call paramsFlush
        redraw();
    }

    // visage only calls mouseDown on a frame if the mouse is IN that frame
    void mouseDown(const visage::MouseEvent &e) override {
        // this corresponds to the start of a UI guesture, so we send a gesture begin message
        from_ui_q.try_enqueue({From_UI::Begin_Edit, param_id, 0.0});
        apply_drag(e.position.x);
    }

    // the frame that received mouseDown keeps receiving mouseDrag/mouseUp,
    // even after the cursor leaves its bounds
    void mouseDrag(const visage::MouseEvent &e) override {
        apply_drag(e.position.x);
    }

    void mouseUp(const visage::MouseEvent &e) override {
        from_ui_q.try_enqueue({From_UI::End_Edit, param_id, 0.0});
        request_param_flush();
    }
};

////////////
// editor //
////////////
// Runs entirely on the UI thread. 
// Info about audio thread's values only ever arrive through to_ui_q.
// We don't read directly

struct My_Editor : visage::ApplicationWindow, visage::EventTimer
{
    moodycamel::ReaderWriterQueue<To_UI> &to_ui_q; // we consume (the audio thread produces)

    visage::Font font { 15.0f, visage::fonts::Lato_Regular_ttf };

    // child frames (ranges/labels hardcoded to mirror paramsInfo)
    Slider gain_slider;
    Slider dummy_slider;

    // showcase widgets: visage ships stock widgets too (see visage_widgets/ for
    // more — color picker, graphs, etc). These are wired to nothing. they only
    // demonstrate that our hand-made Slider and stock widgets are all just Frames.
    visage::UiButton   showcase_button { "Push Me", font };
    visage::TextEditor showcase_text;

    My_Editor(moodycamel::ReaderWriterQueue<To_UI> &to_ui,
              moodycamel::ReaderWriterQueue<From_UI> &from_ui, std::function<void()> flush)
        : to_ui_q(to_ui),
          gain_slider (0, "Gain",  -60.0f, 6.0f, "%.1f dB", from_ui, flush),
          dummy_slider(1, "Dummy",   0.0f, 1.0f, "%.3f",    from_ui, flush)
    {
        addChild(gain_slider);
        addChild(dummy_slider);

        showcase_text.setDefaultText("type here...");
        showcase_text.setFont(font);
        addChild(showcase_button);
        addChild(showcase_text);

        startTimer(30); // poll the queue at a 30ms rate
    }

    // visage calls this when this component/window is given a size
    void resized() override {
        gain_slider    .setBounds(20.0f,  33.0f, width() - 40.0f, 48.0f);
        dummy_slider   .setBounds(20.0f, 113.0f, width() - 40.0f, 48.0f);
        showcase_button.setBounds(20.0f, 185.0f, 120.0f, 34.0f);
        showcase_text  .setBounds(160.0f, 185.0f, width() - 180.0f, 34.0f);
    }

    void draw(visage::Canvas &canvas) override {
        canvas.setColor(0xff26262c);
        canvas.fill(0, 0, width(), height()); // children draw themselves on top

        // we can also draw freely in the draw function! Let's draw some text
        canvas.setColor(0xffe8e8f0);
        canvas.text("Hello clap!", font, visage::Font::kCenter, 0, 4.0f, width(), 24.0f);

        // Visage has some canvas primtiives
        canvas.setColor(0xffef8354);
        canvas.circle(20.0f, 235.0f, 40.0f); // x, y, diameter

        canvas.setColor(0xff8ab4ff);
        canvas.ring(84.0f, 235.0f, 40.0f, 4.0f); // ... and thickness

        canvas.setColor(0xff7bd88f);
        canvas.arc(148.0f, 235.0f, 40.0f, 4.0f, 0.0f, 2.2f, true); 

        canvas.setColor(0xffe8c15a);
        canvas.triangle(212.0f, 275.0f, 232.0f, 235.0f, 252.0f, 275.0f);

        canvas.setColor(visage::Brush::horizontal(0xffc792ea, 0xff8ab4ff)); // colors can be
        canvas.diamond(276.0f, 235.0f, 40.0f, 6.0f);

        canvas.setColor(0xffb8b8c0);
        canvas.segment(340.0f, 271.0f, 380.0f, 239.0f, 4.0f, true); 
    }

    // in the UI thread, we check periodically to see if there's anything in the queue
    // if there is, we act on it!
    void timerCallback() override {
        To_UI msg;
        while (to_ui_q.try_dequeue(msg)) {
            if (msg.param_id == gain_slider.param_id)
                gain_slider.set_value(msg.value);
            else if (msg.param_id == dummy_slider.param_id)
                dummy_slider.set_value(msg.value);
        }
    }
};
