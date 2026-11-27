#pragma once

#include <errno.h>
#include <stdbool.h>
#include <sys/socket.h>

enum {
    DOOM_WIDTH = 80,
    DOOM_HEIGHT = 50,
    DOOM_RESET = -1,
    DOOM_IDLE = 0,
    DOOM_FORWARD,
    DOOM_BACK,
    DOOM_LEFT,
    DOOM_RIGHT,
    DOOM_ATTACK,
    DOOM_USE,
    DOOM_FORWARD_ATTACK,
    DOOM_ACTIONS,
};

typedef struct {
    int action;
    int tics;
} DoomCommand;

typedef struct {
    unsigned char pixels[DOOM_WIDTH * DOOM_HEIGHT];
    float reward;
    bool terminal;
    bool completed;
} DoomFrame;

static inline bool doom_transfer(int fd, void* data, size_t size, bool write) {
    unsigned char* bytes = (unsigned char*)data;
    while (size > 0) {
        ssize_t count = write ? send(fd, bytes, size, MSG_NOSIGNAL) : recv(fd, bytes, size, 0);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return false;
        }
        bytes += count;
        size -= count;
    }
    return true;
}
