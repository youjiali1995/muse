#define _BSD_SOURCE
#define _POSIX_SOURCE
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/epoll.h>
#include "server.h"
#include "util.h"
#include "net.h"
#include "connection.h"
#include "ev.h"

config_t server_cfg;
int epfd;

static void print_usage(void)
{
    printf("Usage: muse [OPTION]\n"
           "    --stop      Stop muse.\n"
           "    --restart    Restart muse and reload config.json.\n"
           "    --help      Print usage.\n");
}

static pid_t get_pid(void)
{
    FILE *fp = fopen("log/muse.pid", "r");
    MUSE_EXIT_ON(!fp, "open pid file: log/muse.pid failed");

    pid_t pid = 0;
    fscanf(fp, "%d", &pid);
    return pid;
}

static void save_pid(pid_t pid)
{
    FILE *fp = fopen("log/muse.pid", "w");
    MUSE_EXIT_ON(!fp, "open pid file: log/muse.pid failed");
    fprintf(fp, "%d", pid);
    fclose(fp);
}

static void send_signal(int sig)
{
    pid_t pid = get_pid();
    MUSE_EXIT_ON(pid == 0 , "muse is not running");

    kill(pid, sig);
}

static bool reload_flag = false;

static void sigint_handler(int sig)
{
    muse_log("muse exited");
    if (getpid() == get_pid()) {
        kill(-getpid(), SIGINT);
    }
    if (!reload_flag)
        raise(SIGKILL);
    else
        reload_flag = false;
}

static void sighup_handler(int sig)
{
    int save_errno = errno;

    muse_log("muse reload config.json and restarting");
    MUSE_EXIT_ON(config_load(&server_cfg, "config.json") != MUSE_OK,
            "load config.json failed");
    reload_flag = true;
    kill(-getpid(), SIGINT);

    errno = save_errno;
}

static void set_sig_handler(int sig, void (*handler)(int))
{
    struct sigaction sa;
    sa.sa_handler = handler;
    sa.sa_flags = SA_RESTART;
    sigfillset(&sa.sa_mask);
    MUSE_EXIT_ON(sigaction(sig, &sa, NULL) == -1,
            "set signal hanlder failed");
}

static void server_init(void)
{
    MUSE_EXIT_ON(config_load(&server_cfg, "config.json") != MUSE_OK,
            "load config.json failed");

    if (server_cfg.daemon)
        daemon(1, 0);
    save_pid(getpid());

    signal(SIGPIPE, SIG_IGN);
    set_sig_handler(SIGINT, sigint_handler);
    set_sig_handler(SIGHUP, sighup_handler);

    struct rlimit nofile_limit = {65535, 65535};
    MUSE_EXIT_ON(setrlimit(RLIMIT_NOFILE, &nofile_limit) == -1,
            strerror(errno));
}

int main(int argc, char *argv[])
{
    if (argc >= 2) {
        if (!strncmp(argv[1], "--stop", 6))
            send_signal(SIGINT);
        else if (!strncmp(argv[1], "--restart", 9))
            send_signal(SIGHUP);
        else
            print_usage();

        exit(MUSE_OK);
    }

    MUSE_EXIT_ON(get_pid() != 0, "muse has already been running");

    server_init();
    muse_log("muse server started, listening at port: %u", server_cfg.port);

    int worker = 0;
    for (;;) {
        if (worker >= server_cfg.worker) {
            wait(NULL);
            worker--;
            muse_log("muse subprocess failed, restarting");
        }

        pid_t pid = fork();
        if (pid == 0)
            break;
        else if (pid < 0)
            continue;
        worker++;
    }

    int listen_fd = tcp_listen_fd(NULL, server_cfg.port, 1024);
    MUSE_EXIT_ON(listen_fd == MUSE_ERROR, strerror(errno));
    ev_t listen_ev = {
        .ptr = &listen_fd,
        .in_handler = accept_connection
    };
    struct epoll_event event;
    event.data.ptr = &listen_ev;
    event.events = EPOLLIN;
    MUSE_EXIT_ON(epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &event), strerror(errno));

    muse_log("muse subprocess started");

    struct epoll_event events[MAX_EVENTS];
    for (;;) {
        int nfds = epoll_wait(epfd, events, MAX_EVENTS, 500);
        if (nfds == MUSE_ERROR)
            MUSE_EXIT_ON(errno != EINTR, strerror(errno));

        for (int i = 0; i < nfds; i++) {
            ev_t *ev = events[i].data.ptr;
            if (events[i].events & EPOLLIN) {
                if (ev->in_handler(ev->ptr) == MUSE_ERROR) {
                    if (ev->err_handler)
                        ev->err_handler(ev->ptr);
                    memset(&events[i], 0, sizeof(events[i]));
                } else {
                    if (ev->ok_handler)
                        ev->ok_handler(ev->ptr);
                }
            }
            if (events[i].events & EPOLLOUT) {
                if (ev->out_handler(ev->ptr) == MUSE_ERROR) {
                    if (ev->err_handler)
                        ev->err_handler(ev->ptr);
                    memset(&events[i], 0, sizeof(events[i]));
                } else {
                    if (ev->ok_handler)
                        ev->ok_handler(ev->ptr);
                }
            }
        }
        sweep_connection();
    }

    close(listen_fd);
    return MUSE_OK;
}
