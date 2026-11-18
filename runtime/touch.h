#pragma once
// The controller drawn on the screen, for a phone or a tablet with no pad
// connected: a stick under the left thumb, the view under the right one, and
// the 360 pad's buttons by their own names, since the game's prompts show
// them ("hold X to reload").
//
// The presenter's thread feeds it the fingers (Event) and draws a copy of it
// (Snapshot, gpu/vulkan/menu_overlay.cpp); the title's input poll reads the
// pad it makes (Sample, kernel/input.cpp).
//
// The layout and the way a short tap is held long enough for a 60 Hz poll
// follow the "FPS Compact" overlay of XeniOS / ReXGlue (BSD-3-Clause, Ben
// Vanik and contributors; see docs/touch.md). The code is this port's own.
//
// Switches (the settings menu's Touch category writes them):
//   MW2_TOUCH=on|off        unset: shown when no controller is connected (iOS)
//   MW2_TOUCH_SENS=1..10    how far a swipe turns the view (5)
//   MW2_TOUCH_AIM=hold      LT held while the finger is on it; unset, a tap toggles
//   MW2_TOUCH_SIZE=small|large
//   MW2_TOUCH_OPACITY=25|75|100   (50)
//   MW2_TOUCH_FIRE2=1       a second RT under the left hand
//   MW2_TOUCH_SPRINT=0      no sprint from pushing the stick past its rim
//   MW2_TOUCH_LOOK=stick    the view through the right stick even where the
//                           title's angles can be turned directly
//   MW2_TOUCH_TURN=<deg/s>  the turn rate the title gives a full right stick (260)
#include <cstdint>
#include <vector>

namespace touch
{
    // The window in points, and the part of it clear of the notch and the
    // rounded corners.
    void Screen(float width, float height, float safeX, float safeY, float safeWidth, float safeHeight);

    // What Screen was last told: the window's width in points, and how far
    // the notch and the corners reach in from its left and right.
    void Insets(float& width, float& left, float& right);

    enum class Finger { Down, Move, Up, Cancel };
    // A finger, in points from the window's top left.
    void Event(Finger kind, uint64_t id, float x, float y);

    // MW2_TOUCH_SCRIPT's fingers that are due, given to `finger` as fractions
    // of the window, from the thread that has the real ones.
    void RunScript(void (*finger)(Finger, uint64_t, float, float));

    // A real controller was just used (kernel/input.cpp, from its poll): the
    // screen's one goes away until a finger comes back.
    void PadUsed();

    // The view's turn since the last call, in degrees (right and down are
    // positive), for the hook that adds it to the title's own angles
    // (touch_look.cpp). False when there is none to take, or the stick is to
    // carry it (MW2_TOUCH_LOOK=stick).
    bool TakeLook(float& yaw, float& pitch);
    // Read the switches again (the settings menu, after it wrote one).
    void Configure();

    // True when the title is to be given the screen's pad.
    bool Active();

    struct Pad
    {
        uint16_t buttons = 0;
        uint8_t triggers[2]{};
        int16_t axes[4]{};      // the guest's: Y up
    };
    void Sample(Pad& pad);

    // Nothing, in touch_look.cpp; see there.
    void LinkTheLook();

    // What the presenter draws.
    struct Shape
    {
        float x, y, radius;     // points
        const char* label;
        uint32_t tint;          // 0xRRGGBB, the label's colour
        bool pressed;
    };
    struct View
    {
        bool shown = false;
        float opacity = 0.5f;
        float width = 0, height = 0;   // the window, in points
        std::vector<Shape> shapes;
        bool stick = false;            // the left stick's ring and knob
        float baseX = 0, baseY = 0, baseRadius = 0, knobX = 0, knobY = 0;
    };
    View Snapshot();
}
