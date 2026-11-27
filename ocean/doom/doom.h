#include <assert.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include "doom_ipc.h"

typedef float obs_t;
#include "pufferenv.h"

#define OBS_SIZE (DOOM_WIDTH * DOOM_HEIGHT)
#define NUM_ATNS 1
#define ACT_SIZES {DOOM_ACTIONS}
#define PUF_STEPS_PER_SEC 9

extern char** environ;

struct Log {
    float perf, score, episode_length, n;
};

struct Env {
    Log log;
    int num_agents;
    unsigned int rng;
    Agent agents[1];
    int tag, boundary_reached;
    const char* iwad;
    int frameskip, max_steps, steps;
    float score;
    int socket;
    pid_t worker;
    Texture2D texture;
};

void puf_init(Env* env, Dict* kwargs) {
    env->num_agents = 1;
    env->iwad = dict_get_str(kwargs, "iwad");
    env->frameskip = dict_get(kwargs, "frameskip");
    env->max_steps = dict_get(kwargs, "max_steps");
    assert(env->frameskip > 0 && env->max_steps > 0);
    env->socket = -1;
}

static DoomFrame doom_command(Env* env, int action) {
    DoomCommand request = {.action = action, .tics = env->frameskip};
    DoomFrame frame;
    assert(doom_transfer(env->socket, &request, sizeof(request), true)
        && "could not send an action to the Doom worker");
    assert(doom_transfer(env->socket, &frame, sizeof(frame), false)
        && "Doom worker stopped before sending an observation");
    if (!frame.terminal) {
        for (int i = 0; i < OBS_SIZE; i++) {
            env->agents[0].observations[i] = frame.pixels[i] / 255.0f;
        }
    }
    return frame;
}

void puf_reset(Env* env) {
    if (env->socket < 0) {
        int sockets[2];
        assert(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) == 0);
        posix_spawn_file_actions_t files;
        assert(posix_spawn_file_actions_init(&files) == 0);
        assert(posix_spawn_file_actions_adddup2(&files, sockets[1], STDIN_FILENO) == 0);
        assert(posix_spawn_file_actions_addopen(
            &files, STDOUT_FILENO, "/dev/null", O_WRONLY, 0) == 0);
        char worker[] = "build/doom_worker";
        char* args[] = {worker, (char*)env->iwad, NULL};
        int status = posix_spawn(&env->worker, worker, &files, NULL, args, environ);
        posix_spawn_file_actions_destroy(&files);
        close(sockets[1]);
        assert(status == 0 && "could not start build/doom_worker");
        env->socket = sockets[0];
    }
    doom_command(env, DOOM_RESET);
    env->steps = 0;
    env->score = 0;
}

void puf_step(Env* env) {
    Agent* agent = &env->agents[0];
    DoomFrame frame = doom_command(env, (int)agent->actions[0]);
    env->steps++;
    env->score += frame.reward;
    agent->rewards[0] = frame.reward;
    agent->terminals[0] = frame.terminal || env->steps >= env->max_steps;
    if (agent->terminals[0]) {
        env->log.perf += frame.completed;
        env->log.score += env->score;
        env->log.episode_length += env->steps;
        env->log.n++;
        puf_reset(env);
    }
}

void puf_log(Log* log, Dict* out) {
    dict_set(out, "perf", log->perf);
    dict_set(out, "score", log->score);
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "n", log->n);
}

void puf_render(Env* env) {
    unsigned char pixels[OBS_SIZE];
    for (int i = 0; i < OBS_SIZE; i++) {
        pixels[i] = env->agents[0].observations[i] * 255.0f + 0.5f;
    }
    const int scale = 8;
    if (!IsWindowReady()) {
        InitWindow(DOOM_WIDTH*scale, DOOM_HEIGHT*scale, "Ocean Doom");
        SetTargetFPS(60);
    }
    if (!env->texture.id) {
        Image image = {pixels, DOOM_WIDTH, DOOM_HEIGHT, 1, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE};
        env->texture = LoadTextureFromImage(image);
    }
    UpdateTexture(env->texture, pixels);
    BeginDrawing();
    ClearBackground(BLACK);
    DrawTextureEx(env->texture, (Vector2){0, 0}, 0, scale, WHITE);
    EndDrawing();
}

void puf_close(Env* env) {
    if (env->socket >= 0) {
        close(env->socket);
        while (waitpid(env->worker, NULL, 0) < 0 && errno == EINTR) {
        }
    }
    if (env->texture.id) {
        UnloadTexture(env->texture);
    }
}
