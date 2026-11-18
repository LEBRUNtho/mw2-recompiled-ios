// The screen's controller turns the view by the title's own angles, where
// they are known (title.h): a swipe is a distance, and the right stick can
// only give the title a speed, which it then limits, eases and bends.
//
// CL_GamepadMove(client, time, usercmd) reads the sticks into the client's
// view angles; the command the title then builds takes them from there. What
// the fingers turned since the last command is added after it has run, as a
// mouse's movement is on a PC. The title clamps the pitch itself, later, in
// the player's movement.
#include <ppc_recomp_shared.h>
#include "title.h"
#include "guest.h"
#include "touch.h"
#include "log.h"

// Called from touch.cpp, so that a link from static archives (the iOS build)
// takes this file, and with it the hook below: nothing else names it.
void touch::LinkTheLook() {}

#ifdef T_CL_GamepadMove
GUEST_HOOK(T_CL_GamepadMove)
{
    const uint32_t client = ctx.r3.u32;
    GUEST_ORIG(T_CL_GamepadMove)(ctx, base);
    if (client != 0) return;
    static bool called = false;
    if (!called) { called = true; LOGI("touch: the title reads the pad's sticks for the view"); }

    float yaw = 0, pitch = 0;
    if (!touch::TakeLook(yaw, pitch)) return;

#ifdef T_DATA_LookClientsPtr
    const be<uint32_t>* pointer = GuestPtr<be<uint32_t>>(T_DATA_LookClientsPtr);
    const uint32_t state = pointer ? uint32_t(*pointer) : 0;
    if (!state) return;
    // A menu over the game: the title turns the view under it, a finger on
    // the menu does not.
    if (const be<uint32_t>* catchers = GuestPtr<be<uint32_t>>(T_DATA_KeyCatchers); catchers && (*catchers & 0x10))
        return;
#else
    const uint32_t state = T_DATA_LookClients;
#endif
    // The view is held (a scripted moment): the title left it alone too.
    if (const be<uint32_t>* flags = GuestPtr<be<uint32_t>>(state + T_LOOK_FLAGS); flags && (*flags & 0x800))
    {
        static bool held = false;
        if (!held) { held = true; LOGI("touch: a swipe while the title holds the view (first time)"); }
        return;
    }

    // Aiming down the sights narrows the view, and the title slows the stick
    // by as much: the same scale keeps a swipe the same distance on screen.
    float scale = 1.0f;
    if (const be<float>* zoom = GuestPtr<be<float>>(T_DATA_AimAssist + 0x120))
    {
        const float value = *zoom;
        if (value >= 0.05f && value <= 1.0f) scale = value;
    }

    be<float>* angles = GuestPtr<be<float>>(state + T_LOOK_ANGLES);
    if (!angles) return;
    // Positive pitch looks down, and yaw grows to the left.
    angles[0] = float(angles[0]) + pitch * scale;
    angles[1] = float(angles[1]) - yaw * scale;

    static bool said = false;
    if (!said) { said = true; LOGI("touch: the view is turned by the title's own angles"); }
}
#endif
