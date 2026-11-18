# Touch controls (iPhone, iPad)

With no controller connected, the app draws one on the screen
(`runtime/touch.cpp`, drawn by `runtime/gpu/vulkan/menu_overlay.cpp`):

- **Left thumb**: anywhere on the left of the screen is the left stick; it
  appears where the thumb comes down. Pushed forward well past its rim, it
  clicks (sprint). `LS` does the same.
- **Right thumb**: a swipe anywhere else turns the view. A finger that starts
  on `RT`, `LT` or a face button and slides turns the view too, so the view
  can follow a target while firing.
- **Buttons** carry the Xbox 360's names, because the game's own prompts show
  them: `RT` fire, `LT` aim (a tap toggles; Settings > Touch > Aim), `A` jump,
  `B` crouch / prone (hold), `X` reload / use (hold), `Y` switch weapon,
  `RS` melee, `RB` / `LB` grenades, the d-pad for equipment and night vision,
  `START` pause, `BACK` objectives / scores.
- **MENU** opens this port's settings menu (RB + Start on a controller). With
  fingers the menu is worked directly: tap a category, tap a row, tap the left
  or right half of a value to change it, tap a chosen action to do it.

The title only knows a controller, so the view is turned through the right
stick: each input poll, the stick is held where the game turns, in that frame,
by as much as the finger moved. What a full stick cannot turn in one frame is
owed to the next few, so a quick flick arrives whole. The game's own
Sensitivity option sets the ceiling.

On a phone the title is told its display is widescreen (an iPad is told 4:3),
and the 16:9 picture sits between two bars that clear the notch; Settings >
Display > Screen Fill zooms or stretches it instead.

Switches (`.env`, or Settings > Touch): `MW2_TOUCH=on|off`,
`MW2_TOUCH_SENS=1..10`, `MW2_TOUCH_AIM=hold`, `MW2_TOUCH_SIZE=small|large`,
`MW2_TOUCH_OPACITY=25|75|100`, `MW2_TOUCH_FIRE2=1`, `MW2_TOUCH_SPRINT=0`,
`MW2_TOUCH_TURN=<degrees a second of a full stick>`. `MW2_TOUCH=on` on a
desktop build takes the mouse for a finger.

## Credits

The layout idea (an "FPS compact" pad with the console's button names, the
stick under the left thumb, the view under the right) and the minimum hold of
a tap follow the touch overlay of XeniOS / ReXGlue as adapted by
[ReCoD4](https://github.com/DevZer0D4Y/ReCoD4)
(`ports/ios/input/TouchControls.h`), which is under the BSD 3-Clause License:

    Copyright (c) 2026, MC360-Recomp contributors
    Copyright 2026 Ben Vanik. All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice,
       this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its
       contributors may be used to endorse or promote products derived from
       this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
    LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.

The code here is this port's own; no file of theirs is included.
