// The controller drawn on the screen (touch.h).
#include "touch.h"
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
#include "env.h"
#include "log.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

namespace
{
    using Clock = std::chrono::steady_clock;

    // XINPUT_GAMEPAD's buttons.
    constexpr uint16_t kUp = 0x0001, kDown = 0x0002, kLeft = 0x0004, kRight = 0x0008;
    constexpr uint16_t kStart = 0x0010, kBack = 0x0020, kLS = 0x0040, kRS = 0x0080;
    constexpr uint16_t kLB = 0x0100, kRB = 0x0200, kA = 0x1000, kB = 0x2000, kX = 0x4000, kY = 0x8000;

    enum class Corner { TopLeft, TopRight, BottomLeft, BottomRight };
    enum class Kind { Button, Aim, Fire, Menu };

    // A control: a circle, placed from a corner of the safe area, in points of
    // a 440-point-high screen (an iPhone Pro Max held sideways).
    struct Control
    {
        Kind kind;
        uint16_t bits;
        const char* label;
        Corner corner;
        float dx, dy, radius;
        uint32_t tint;
        bool turns;       // a finger that starts on it and slides turns the view
        bool optional;    // the second fire button
    };
    constexpr uint32_t kWhite = 0xFFFFFF;
    constexpr Control kControls[] = {
        // The right hand.
        { Kind::Fire,   0,    "RT",    Corner::BottomRight, 100, 118, 40, 0xFFD9C8, true,  false },
        { Kind::Aim,    0,    "LT",    Corner::BottomRight,  38, 212, 29, 0xC8E6FF, true,  false },
        { Kind::Button, kA,   "A",     Corner::BottomRight,  40,  58, 28, 0x8EE27A, true,  false },
        { Kind::Button, kB,   "B",     Corner::BottomRight, 114,  38, 24, 0xFF7A6E, true,  false },
        { Kind::Button, kX,   "X",     Corner::BottomRight, 192,  76, 26, 0x7DB7FF, true,  false },
        { Kind::Button, kY,   "Y",     Corner::BottomRight, 186, 150, 24, 0xFFD95A, true,  false },
        { Kind::Button, kRS,  "RS",    Corner::BottomRight, 114, 207, 23, kWhite,   true,  false },
        { Kind::Button, kRB,  "RB",    Corner::BottomRight,  38, 288, 22, kWhite,   true,  false },
        { Kind::Button, kLB,  "LB",    Corner::BottomRight, 104, 282, 22, kWhite,   true,  false },
        // The top row.
        { Kind::Button, kStart, "START", Corner::TopRight,   38,  32, 19, kWhite,   false, false },
        { Kind::Menu,   0,    "MENU",  Corner::TopRight,     92,  32, 19, 0xD5EB0A, false, false },
        { Kind::Button, kBack, "BACK", Corner::TopLeft,      38,  32, 19, kWhite,   false, false },
        // The d-pad, at the screen's edge: the game's map is in the picture's
        // top left corner, and on a phone the picture starts further in.
        { Kind::Button, kUp,    "^",   Corner::TopLeft,      52, 116, 16, kWhite,   false, false },
        { Kind::Button, kDown,  "v",   Corner::TopLeft,      52, 184, 16, kWhite,   false, false },
        { Kind::Button, kLeft,  "<",   Corner::TopLeft,      18, 150, 16, kWhite,   false, false },
        { Kind::Button, kRight, ">",   Corner::TopLeft,      86, 150, 16, kWhite,   false, false },
        // The left hand.
        { Kind::Button, kLS,  "LS",    Corner::BottomLeft,  222,  46, 22, kWhite,   false, false },
        { Kind::Fire,   0,    "RT",    Corner::BottomLeft,  160, 205, 26, 0xFFD9C8, false, true  },
    };
    constexpr int kCount = int(std::size(kControls));
    constexpr int kStick = -1, kLook = -2;

    struct Capture
    {
        uint64_t id;
        int control;            // an index, kStick or kLook
        float startX, startY, x, y;
        bool sprinted = false;  // the stick has been past its rim since it last came back
    };

    struct State
    {
        std::mutex lock;
        bool configured = false;
        // The switches.
        int mode = 0;               // 0 auto, 1 on, 2 off
        float degreesPerPoint = 0.30f;
        bool aimHold = false, fire2 = false, sprint = true;
        float size = 1.0f, opacity = 0.5f;
        float turnRate = 260.0f;

        float width = 0, height = 0;
        float safeX = 0, safeY = 0, safeW = 0, safeH = 0;
        bool pad = false;           // a controller was used since the last finger
        bool everTouched = false;
        bool lookByStick = false;
        Clock::time_point directAt{};   // when the title's angles were last turned directly

        std::vector<Capture> captures;
        int held[kCount]{};
        Clock::time_point until[kCount]{};
        bool aiming = false;
        Clock::time_point sprintUntil{};
        // What the fingers have turned the view by since the last poll, in
        // points, and what the stick has not yet been able to turn, in degrees.
        float lookX = 0, lookY = 0;
        double owedYaw = 0, owedPitch = 0;
        // The same turn as a path through time, for the direct look: how far
        // the fingers had turned the view in all, at each of their moves.
        struct Turned { Clock::time_point at; double x, y; };
        std::deque<Turned> path;
        double turnedX = 0, turnedY = 0;      // the path's end
        double takenX = 0, takenY = 0;        // how far along it the title has been taken
        Clock::time_point polled{};

        // MW2_TOUCH_LOG.
        bool log = false;
        struct Stats
        {
            int downs = 0, moves = 0, ups = 0, cancels = 0;
            double ageSum = 0, ageMost = 0, buffered = 0;
            int polls = 0, looks = 0, looksEmpty = 0, looksDouble = 0;
            int movesSinceLook = 0;
            double gapMost = 0, yaw = 0, pitch = 0;
        } stats;
        Clock::time_point statsAt{};
    } s;

    void ConfigureLocked()
    {
        s.configured = true;
        touch::LinkTheLook();
        const char* mode = env::Text("MW2_TOUCH");
        s.mode = !mode ? 0 : (std::strcmp(mode, "off") == 0 || std::strcmp(mode, "0") == 0) ? 2 : 1;
        const double level = std::clamp(env::Real("MW2_TOUCH_SENS", 5.0), 1.0, 10.0);
        s.degreesPerPoint = float(0.06 * level);
        const char* aim = env::Text("MW2_TOUCH_AIM");
        s.aimHold = aim && std::strcmp(aim, "hold") == 0;
        if (s.aimHold) s.aiming = false;
        s.fire2 = env::Flag("MW2_TOUCH_FIRE2");
        s.sprint = !env::Text("MW2_TOUCH_SPRINT") || env::Flag("MW2_TOUCH_SPRINT");
        const char* size = env::Text("MW2_TOUCH_SIZE");
        s.size = !size ? 1.0f : std::strcmp(size, "small") == 0 ? 0.85f : std::strcmp(size, "large") == 0 ? 1.15f : 1.0f;
        const double opacity = env::Real("MW2_TOUCH_OPACITY", 50.0);
        s.opacity = float(std::clamp(opacity, 10.0, 100.0) / 100.0);
        const char* look = env::Text("MW2_TOUCH_LOOK");
        s.lookByStick = look && std::strcmp(look, "stick") == 0;
        s.log = env::Flag("MW2_TOUCH_LOG");
        s.turnRate = float(std::clamp(env::Real("MW2_TOUCH_TURN", 260.0), 30.0, 2000.0));
    }

    bool ActiveLocked()
    {
        if (!s.configured) ConfigureLocked();
        if (s.mode == 2) return false;
        if (s.mode == 1) return true;
#if defined(__APPLE__) && TARGET_OS_IOS
        return !s.pad;
#else
        return false;
#endif
    }

    float Scale() { return (s.height > 0 ? s.height / 440.0f : 1.0f) * s.size; }

    // The control's centre and radius on this screen.
    void Place(const Control& c, float& x, float& y, float& radius)
    {
        const float k = Scale();
        const bool right = c.corner == Corner::TopRight || c.corner == Corner::BottomRight;
        const bool bottom = c.corner == Corner::BottomLeft || c.corner == Corner::BottomRight;
        // The notch's side is kept clear of, but its whole inset is more than
        // a thumb wants to reach across.
        const float insetLeft = s.safeX * 0.7f, insetRight = (s.width - s.safeX - s.safeW) * 0.7f;
        const float insetTop = s.safeY, insetBottom = (s.height - s.safeY - s.safeH) * 0.5f;
        x = right ? s.width - insetRight - c.dx * k : insetLeft + c.dx * k;
        y = bottom ? s.height - insetBottom - c.dy * k : insetTop + c.dy * k;
        radius = c.radius * k;
    }

    bool Shown(const Control& c) { return !c.optional || s.fire2; }

    int Hit(float x, float y)
    {
        int best = -1;
        float bestDistance = 0;
        for (int i = 0; i < kCount; i++)
        {
            if (!Shown(kControls[i])) continue;
            float cx, cy, radius;
            Place(kControls[i], cx, cy, radius);
            // A thumb is not a pointer: a little past the rim still counts.
            const float reach = radius * 1.18f, d = std::hypot(x - cx, y - cy);
            if (d <= reach && (best < 0 || d < bestDistance)) { best = i; bestDistance = d; }
        }
        return best;
    }

    float StickRadius() { return 58.0f * Scale(); }

    void Release(const Capture& capture)
    {
        if (capture.control >= 0 && s.held[capture.control] > 0) s.held[capture.control]--;
    }

    void CancelAll()
    {
        s.captures.clear();
        std::fill(std::begin(s.held), std::end(s.held), 0);
        s.lookX = s.lookY = 0;
        s.owedYaw = s.owedPitch = 0;
        s.path.clear();
        s.takenX = s.turnedX;
        s.takenY = s.turnedY;
    }

    bool Pressed(int control, Clock::time_point now)
    {
        return s.held[control] > 0 || s.until[control] > now;
    }
}

void touch::Screen(float width, float height, float safeX, float safeY, float safeWidth, float safeHeight)
{
    std::lock_guard held(s.lock);
    if (width == s.width && height == s.height && safeX == s.safeX && safeY == s.safeY &&
        safeWidth == s.safeW && safeHeight == s.safeH)
        return;
    CancelAll();
    s.width = width; s.height = height;
    s.safeX = safeX; s.safeY = safeY; s.safeW = safeWidth; s.safeH = safeHeight;
    LOGI("touch: the screen is %.0fx%.0f points, clear of the notch from %.0f,%.0f for %.0fx%.0f",
         width, height, safeX, safeY, safeWidth, safeHeight);
}

void touch::Insets(float& width, float& left, float& right)
{
    std::lock_guard held(s.lock);
    width = s.width;
    left = s.safeX;
    right = s.width > 0 ? s.width - s.safeX - s.safeW : 0;
}

void touch::Measure(double eventAgeMs, double framesBuffered)
{
    std::lock_guard held(s.lock);
    s.stats.ageSum += eventAgeMs;
    s.stats.ageMost = std::max(s.stats.ageMost, eventAgeMs);
    s.stats.buffered = framesBuffered;
}

void touch::PadUsed()
{
    std::lock_guard held(s.lock);
    if (s.pad) return;
    s.pad = true;
    CancelAll();
    s.aiming = false;
}

bool touch::TakeLook(float& yaw, float& pitch)
{
    std::lock_guard held(s.lock);
    if (!ActiveLocked() || s.lookByStick) return false;
    s.directAt = Clock::now();
    // Where the fingers had got to a moment ago, between the two moves either
    // side of that moment. The moves are read in batches a frame apart, and
    // unevenly: taken as they are read, one frame turned by nothing and the
    // next by two frames' worth, and the view juddered at a steady 60. The
    // moment is far enough back for its batch to have been read.
    constexpr auto kBehind = std::chrono::milliseconds(20);
    const auto moment = s.directAt - kBehind;
    double x = s.turnedX, y = s.turnedY;
    if (!s.path.empty() && moment < s.path.back().at)
    {
        // The last move at or before the moment, and the one after it.
        size_t after = 0;
        while (after < s.path.size() && s.path[after].at <= moment) after++;
        if (after == 0) { x = s.takenX; y = s.takenY; }
        else
        {
            const State::Turned& a = s.path[after - 1];
            const State::Turned& b = s.path[after];
            const double span = std::chrono::duration<double>(b.at - a.at).count();
            const double t = span > 0 ? std::chrono::duration<double>(moment - a.at).count() / span : 1.0;
            x = a.x + (b.x - a.x) * t;
            y = a.y + (b.y - a.y) * t;
        }
        while (s.path.size() > 2 && s.path[1].at <= moment) s.path.pop_front();
    }
    else
        s.path.clear();
    yaw = float(x - s.takenX) * s.degreesPerPoint;
    pitch = float(y - s.takenY) * s.degreesPerPoint * 0.8f;
    s.takenX = x;
    s.takenY = y;
    if (s.log)
    {
        const bool finger = std::any_of(s.captures.begin(), s.captures.end(), [](const Capture& c) {
            return c.control == kLook || (c.control >= 0 && kControls[c.control].turns);
        });
        if (finger)
        {
            s.stats.looks++;
            if (s.stats.movesSinceLook == 0) s.stats.looksEmpty++;
            if (s.stats.movesSinceLook >= 2) s.stats.looksDouble++;
            s.stats.yaw += std::fabs(yaw);
            s.stats.pitch += std::fabs(pitch);
        }
        s.stats.movesSinceLook = 0;
    }
    s.lookX = s.lookY = 0;
    s.owedYaw = s.owedPitch = 0;
    return yaw != 0 || pitch != 0;
}

void touch::Configure()
{
    std::lock_guard held(s.lock);
    ConfigureLocked();
}

bool touch::Active()
{
    std::lock_guard held(s.lock);
    return ActiveLocked();
}

void touch::Event(Finger kind, uint64_t id, float x, float y, double ageMs)
{
    std::lock_guard held(s.lock);
    // A finger takes the screen's controller back from a real one.
    if (kind == Finger::Down) s.pad = false;
    if (!ActiveLocked() || s.width <= 0) return;
    const auto now = Clock::now();
    auto found = std::find_if(s.captures.begin(), s.captures.end(), [id](const Capture& c) { return c.id == id; });

    if (s.log)
    {
        (kind == Finger::Down ? s.stats.downs : kind == Finger::Move ? s.stats.moves :
         kind == Finger::Up ? s.stats.ups : s.stats.cancels)++;
        if (kind == Finger::Move) s.stats.movesSinceLook++;
    }
    switch (kind)
    {
    case Finger::Down:
    {
        if (found != s.captures.end()) return;
        if (!s.everTouched) { s.everTouched = true; LOGI("touch: the first finger, at %.0f,%.0f", x, y); }
        Capture capture{ id, Hit(x, y), x, y, x, y };
        if (capture.control < 0)
        {
            // Not on a button: the left of the screen is the stick, wherever
            // the thumb comes down, and the rest turns the view.
            const bool stickFree = std::none_of(s.captures.begin(), s.captures.end(),
                                                [](const Capture& c) { return c.control == kStick; });
            capture.control = (x < s.width * 0.42f && stickFree) ? kStick : kLook;
        }
        else
        {
            const Control& control = kControls[capture.control];
            s.held[capture.control]++;
            // Long enough for a poll at 60 Hz to see the shortest tap.
            s.until[capture.control] = now + std::chrono::milliseconds(80);
            if (control.kind == Kind::Aim && !s.aimHold) s.aiming = !s.aiming;
        }
        s.captures.push_back(capture);
        break;
    }
    case Finger::Move:
    {
        if (found == s.captures.end()) return;
        Capture& capture = *found;
        const float dx = x - capture.x, dy = y - capture.y;
        capture.x = x; capture.y = y;
        if (capture.control == kLook || (capture.control >= 0 && kControls[capture.control].turns))
        {
            s.lookX += dx;
            s.lookY += dy;
            s.turnedX += dx;
            s.turnedY += dy;
            const auto when = now - std::chrono::microseconds(int64_t(std::clamp(ageMs, 0.0, 100.0) * 1000.0));
            // Kept in order: a batch's events can be a hair out of it.
            const auto at = s.path.empty() ? when : std::max(when, s.path.back().at);
            s.path.push_back({ at, s.turnedX, s.turnedY });
            if (s.path.size() > 512) s.path.pop_front();
        }
        else if (capture.control == kStick && s.sprint)
        {
            // Pushed forward well past the rim: a click of the stick, which
            // is the game's sprint. Once, until the thumb comes back.
            const float fx = capture.x - capture.startX, fy = capture.y - capture.startY;
            const float radius = StickRadius();
            const bool far = -fy > radius * 1.5f && std::fabs(fx) < -fy * 0.6f;
            if (far && !capture.sprinted)
            {
                capture.sprinted = true;
                s.sprintUntil = now + std::chrono::milliseconds(160);
            }
            else if (!far && -fy < radius * 1.15f) capture.sprinted = false;
        }
        break;
    }
    case Finger::Up:
    case Finger::Cancel:
        if (found == s.captures.end()) return;
        Release(*found);
        if (kind == Finger::Cancel && found->control >= 0 && s.held[found->control] == 0)
            s.until[found->control] = {};
        s.captures.erase(found);
        break;
    }
}

namespace
{
    // MW2_TOUCH_SCRIPT="4:0.9,0.5:0.3;6:0.7,0.5:1:0.9,0.4": fingers at fixed
    // times, for a run with no hand on the screen (the simulator). Each entry
    // is when, where (as fractions of the window), for how long, and where
    // the finger slides to meanwhile, if it does.
    struct Scripted { double at, x, y, held, x2, y2; int state = 0; };
    std::vector<Scripted> g_script;
}

void touch::RunScript(void (*finger)(Finger, uint64_t, float, float))
{
    {
        static const Clock::time_point start = Clock::now();
        static const bool parsed = [] {
            const char* text = env::Text("MW2_TOUCH_SCRIPT");
            for (const char* at = text; at && *at; )
            {
                Scripted f{};
                const int got = std::sscanf(at, "%lf:%lf,%lf:%lf:%lf,%lf", &f.at, &f.x, &f.y, &f.held, &f.x2, &f.y2);
                if (got >= 4)
                {
                    if (got < 6) { f.x2 = f.x; f.y2 = f.y; }
                    g_script.push_back(f);
                }
                const char* next = std::strchr(at, ';');
                at = next ? next + 1 : nullptr;
            }
            return true;
        }();
        (void)parsed;
        if (g_script.empty()) return;
        const double now = std::chrono::duration<double>(Clock::now() - start).count();
        for (size_t i = 0; i < g_script.size(); i++)
        {
            Scripted& f = g_script[i];
            const uint64_t id = 0x5C000000u + i;
            const double t = std::clamp((now - f.at) / std::max(f.held, 0.01), 0.0, 1.0);
            const float x = float(f.x + (f.x2 - f.x) * t), y = float(f.y + (f.y2 - f.y) * t);
            if (f.state == 0 && now >= f.at) { f.state = 1; finger(Finger::Down, id, x, y); }
            else if (f.state == 1 && now >= f.at + f.held)
            {
                f.state = 2;
                finger(Finger::Move, id, x, y);
                finger(Finger::Up, id, x, y);
            }
            else if (f.state == 1) finger(Finger::Move, id, x, y);
        }
    }
}

void touch::Sample(Pad& pad)
{
    std::lock_guard held(s.lock);
    pad = Pad{};
    if (!ActiveLocked()) return;
    const auto now = Clock::now();
    double dt = s.polled.time_since_epoch().count() ? std::chrono::duration<double>(now - s.polled).count() : 1.0 / 60;
    s.polled = now;
    if (s.log)
    {
        s.stats.polls++;
        s.stats.gapMost = std::max(s.stats.gapMost, dt * 1000.0);
        if (now - s.statsAt >= std::chrono::seconds(1))
        {
            const State::Stats& t = s.stats;
            const int events = t.downs + t.moves + t.ups + t.cancels;
            if (events)
                LOGI("touch: 1 s: %d down %d move %d up %d cancel; events read %.1f ms old on average, %.1f at most; "
                     "%d polls (longest gap %.1f ms); view turned at %d of them: %d with no new move, %d with two or "
                     "more; %.1f deg yaw %.1f pitch; %.2f frames buffered; %zu fingers down",
                     t.downs, t.moves, t.ups, t.cancels, t.ageSum / events, t.ageMost, t.polls, t.gapMost,
                     t.looks, t.looksEmpty, t.looksDouble, t.yaw, t.pitch, t.buffered, s.captures.size());
            s.stats = State::Stats{};
            s.statsAt = now;
        }
    }
    dt = std::clamp(dt, 1.0 / 240, 1.0 / 15);

    bool aimHeld = false;
    for (int i = 0; i < kCount; i++)
    {
        if (!Shown(kControls[i]) || !Pressed(i, now)) continue;
        const Control& control = kControls[i];
        switch (control.kind)
        {
        case Kind::Button: pad.buttons |= control.bits; break;
        case Kind::Fire:   pad.triggers[1] = 255; break;
        case Kind::Aim:    aimHeld = true; break;
        // The settings menu's own chord (menu/menu.h).
        case Kind::Menu:   pad.buttons |= kRB | kStart; break;
        }
    }
    if (s.aimHold ? aimHeld : s.aiming) pad.triggers[0] = 255;
    if (s.sprintUntil > now) pad.buttons |= kLS;

    // The left stick. The title has a dead zone of its own in the middle, so
    // the least push here is already past it.
    for (const Capture& capture : s.captures)
    {
        if (capture.control != kStick) continue;
        const float radius = std::max(StickRadius(), 1.0f);
        float x = (capture.x - capture.startX) / radius, y = (capture.y - capture.startY) / radius;
        const float length = std::hypot(x, y);
        if (length < 0.14f) break;
        const float clamped = std::min(length, 1.0f);
        const float out = 0.26f + 0.74f * (clamped - 0.14f) / 0.86f;
        x = x / length * out;
        y = y / length * out;
        pad.axes[0] = int16_t(std::lround(x * 32767.0f));
        pad.axes[1] = int16_t(std::lround(-y * 32767.0f));
        break;
    }

    // The view. A stick gives the title a speed, and a finger gives a
    // distance: each poll the stick is held where it turns, in this frame,
    // as far as the finger went. What a full stick cannot turn in one frame
    // is owed to the next ones, up to a point, so a fast flick arrives whole
    // a moment later rather than cut short.
    // Unless the title's own angles are being turned (TakeLook), which is
    // exact: then the stick stays where it is.
    if (!s.lookByStick && now - s.directAt < std::chrono::milliseconds(250)) return;
    s.owedYaw += double(s.lookX) * s.degreesPerPoint;
    s.owedPitch += double(-s.lookY) * s.degreesPerPoint * 0.8;
    s.lookX = s.lookY = 0;
    // The stick has carried it: the direct look has nothing to catch up on.
    s.path.clear();
    s.takenX = s.turnedX;
    s.takenY = s.turnedY;
    const bool looking = std::any_of(s.captures.begin(), s.captures.end(), [](const Capture& c) {
        return c.control == kLook || (c.control >= 0 && kControls[c.control].turns);
    });
    auto stick = [&](double& owed, double rate) {
        const double most = rate * (looking ? 0.12 : 0.04);
        owed = std::clamp(owed, -most, most);
        const double want = std::clamp(owed / (rate * dt), -1.0, 1.0);
        if (std::fabs(want) < 0.003) return int16_t(0);
        owed -= want * rate * dt;
        constexpr double kDead = 0.2;
        const double out = std::copysign(kDead + (1.0 - kDead) * std::fabs(want), want);
        return int16_t(std::lround(out * 32767.0));
    };
    // The title turns up and down at about a third of the speed it turns
    // round (aim_turnrate_pitch, aim_turnrate_yaw).
    pad.axes[2] = stick(s.owedYaw, s.turnRate);
    pad.axes[3] = stick(s.owedPitch, s.turnRate * (90.0 / 260.0));
}

touch::View touch::Snapshot()
{
    std::lock_guard held(s.lock);
    View view;
    if (!ActiveLocked() || s.width <= 0) return view;
    view.shown = true;
    view.opacity = s.opacity;
    view.width = s.width;
    view.height = s.height;
    const auto now = Clock::now();
    for (int i = 0; i < kCount; i++)
    {
        const Control& control = kControls[i];
        if (!Shown(control)) continue;
        Shape shape{};
        Place(control, shape.x, shape.y, shape.radius);
        shape.label = control.label;
        shape.tint = control.tint;
        shape.pressed = Pressed(i, now) || (control.kind == Kind::Aim && !s.aimHold && s.aiming) ||
                        (control.bits == kLS && s.sprintUntil > now);
        view.shapes.push_back(shape);
    }
    for (const Capture& capture : s.captures)
    {
        if (capture.control != kStick) continue;
        const float radius = StickRadius();
        float x = capture.x - capture.startX, y = capture.y - capture.startY;
        const float length = std::hypot(x, y);
        if (length > radius) { x *= radius / length; y *= radius / length; }
        view.stick = true;
        view.baseX = capture.startX; view.baseY = capture.startY; view.baseRadius = radius;
        view.knobX = capture.startX + x; view.knobY = capture.startY + y;
    }
    // At rest, where the stick is: a faint ring, so the left of the screen
    // says what it is for.
    if (!view.stick)
    {
        const float k = Scale();
        view.baseRadius = StickRadius();
        view.baseX = s.safeX * 0.7f + 105.0f * k;
        view.baseY = s.height - (s.height - s.safeY - s.safeH) * 0.5f - 105.0f * k;
        view.knobX = view.baseX; view.knobY = view.baseY;
    }
    return view;
}
