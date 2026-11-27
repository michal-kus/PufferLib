// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "doom_ipc.h"
#include "doomgeneric.h"
#include "doomdef.h"
#include "doomstat.h"
#include "d_loop.h"
#include "d_main.h"
#include "g_game.h"
#include "i_video.h"
#include "r_main.h"

void R_ExecuteSetViewSize(void);

static DoomFrame frame;
static uint32_t clock_ms = 1;

void DG_Init(void) {
}

void DG_DrawFrame(void) {
    for (int y = 0; y < DOOM_HEIGHT; y++) {
        for (int x = 0; x < DOOM_WIDTH; x++) {
            uint32_t rgb = DG_ScreenBuffer[(4*y + 2)*DOOMGENERIC_RESX + 4*x + 2];
            int r = (rgb >> 16) & 255;
            int g = (rgb >> 8) & 255;
            int b = rgb & 255;
            frame.pixels[y*DOOM_WIDTH + x] = (77*r + 150*g + 29*b) >> 8;
        }
    }
}

void DG_SleepMs(uint32_t ms) {
    clock_ms += ms;
}

uint32_t DG_GetTicksMs(void) {
    return clock_ms;
}

int DG_GetKey(int* pressed, unsigned char* key) {
    return 0;
}

void DG_SetWindowTitle(const char* title) {
}

int main(int argc, char** argv) {
    assert(argc == 2);
    char wad[PATH_MAX];
    if (!realpath(argv[1], wad)) {
        perror(argv[1]);
        return 1;
    }
    if (chdir("build") != 0) {
        perror("build");
        return 1;
    }
    char* args[] = {
        argv[0], "-iwad", wad, "-warp", "1", "1", "-skill", "1",
        "-nosound", "-config", "/dev/null", "-extraconfig", "/dev/null",
    };
    singletics = true;
    doomgeneric_Create(sizeof(args)/sizeof(args[0]), args);
    R_SetViewSize(11, 0);
    R_ExecuteSetViewSize();
    ticcmd_t commands[MAXPLAYERS] = {0};
    netcmds = commands;
    const ticcmd_t controls[DOOM_ACTIONS] = {
        [DOOM_FORWARD] = {.forwardmove = 25},
        [DOOM_BACK] = {.forwardmove = -25},
        [DOOM_LEFT] = {.angleturn = 640},
        [DOOM_RIGHT] = {.angleturn = -640},
        [DOOM_ATTACK] = {.buttons = BT_ATTACK},
        [DOOM_USE] = {.buttons = BT_USE},
        [DOOM_FORWARD_ATTACK] = {.forwardmove = 25, .buttons = BT_ATTACK},
    };
    DoomCommand request;
    while (doom_transfer(STDIN_FILENO, &request, sizeof(request), false)) {
        frame.reward = 0;
        frame.completed = false;
        frame.terminal = false;
        if (request.action == DOOM_RESET) {
            gametic = 1;
            G_InitNew(sk_baby, 1, 1);
            commands[consoleplayer] = controls[DOOM_IDLE];
            G_Ticker();
            gametic++;
        } else {
            assert(request.action >= 0 && request.action < DOOM_ACTIONS);
            assert(request.tics > 0);
            commands[consoleplayer] = controls[request.action];
            player_t* player = &players[consoleplayer];
            int kills = player->killcount;
            for (int i = 0; i < request.tics; i++) {
                G_Ticker();
                gametic++;
                frame.completed = gameaction == ga_completed;
                frame.terminal = frame.completed || player->playerstate == PST_DEAD;
                if (frame.terminal) {
                    break;
                }
            }
            frame.reward = player->killcount - kills;
            if (frame.terminal) {
                frame.reward += frame.completed ? 10 : -1;
            }
        }
        if (!frame.terminal) {
            R_RenderPlayerView(&players[consoleplayer]);
            I_FinishUpdate();
        }
        if (!doom_transfer(STDIN_FILENO, &frame, sizeof(frame), true)) {
            break;
        }
    }
}
