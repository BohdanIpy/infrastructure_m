#include <iostream>
#include <thread>
#include <chrono>
#include <memory>
#include <cstdio>
#include <vector>
#include <system_error>
#include <poll.h>

#include "ares.h"
#include <liburing.h>

struct State
{
    io_uring& m_ring;
    ares_channel_t *m_channel{nullptr};

    State(io_uring& ring) : m_ring(ring) {}
};

enum class Operation
{
    POLL_READ,
    POLL_WRITE,
};

struct IoContext
{
    Operation op;
    State* state{nullptr};
    ares_socket_t fd;
};

// Callback invoked by c-ares whenever socket readiness requirements change
void socket_changed_state_cb(void *data, ares_socket_t socket_fd, int readable, int writable)
{
    auto* state = static_cast<State*>(data);
    std::cout << "Socket callback [FD " << socket_fd << "] w|r -> " << writable << "|" << readable << std::endl;

    // Handle socket registration for reading via io_uring poll
    if (readable)
    {
        auto* sqe = io_uring_get_sqe(&state->m_ring);
        io_uring_prep_poll_add(sqe, socket_fd, POLLIN);

        auto* ctx = new IoContext{Operation::POLL_READ, state, socket_fd};
        io_uring_sqe_set_data(sqe, ctx);
    }

    // Handle socket registration for writing via io_uring poll
    if (writable)
    {
        auto* sqe = io_uring_get_sqe(&state->m_ring);
        io_uring_prep_poll_add(sqe, socket_fd, POLLOUT);

        auto* ctx = new IoContext{Operation::POLL_WRITE, state, socket_fd};
        io_uring_sqe_set_data(sqe, ctx);
    }

    // Submit pending poll requests to the ring
    if (readable || writable) {
        io_uring_submit(&state->m_ring);
    }
}

void worker(State* state)
{
    while (true)
    {
        io_uring_cqe* cqe = nullptr;

        int ret = io_uring_wait_cqe(&state->m_ring, &cqe);
        if (ret < 0) {
            // Handle error or break loop
            break;
        }

        auto* ctx = static_cast<IoContext*>(io_uring_cqe_get_data(cqe));

        // cqe->res contains the mask of poll events (e.g. POLLIN/POLLOUT) or error code
        if (ctx && cqe->res > 0)
        {
            if (ctx->op == Operation::POLL_READ)
            {
                ::ares_process_fd(state->m_channel, ctx->fd, ARES_SOCKET_BAD);
            }
            else if (ctx->op == Operation::POLL_WRITE)
            {
                ::ares_process_fd(state->m_channel, ARES_SOCKET_BAD, ctx->fd);
            }
        }

        delete ctx; // Clean up context memory
        io_uring_cqe_seen(&state->m_ring, cqe);
    }
    std::cout << "End of worker loop\n";
}

void addrinfo_cb(void *arg, int status, int timeouts, ares_addrinfo *result)
{
    std::cout << "\n--- Address Info Callback ---" << std::endl;
    printf("Result: %s, timeouts: %d\n", ares_strerror(status), timeouts);

    if (status == ARES_SUCCESS && result) {
        for (const ares_addrinfo_node* node = result->nodes; node != nullptr; node = node->ai_next) {
            char addr_buf[64] = "";
            const void *ptr = nullptr;
            if (node->ai_family == AF_INET) {
                const auto *in_addr = reinterpret_cast<const struct sockaddr_in*>(node->ai_addr);
                ptr = &in_addr->sin_addr;
            } else if (node->ai_family == AF_INET6) {
                const auto *in_addr = reinterpret_cast<const struct sockaddr_in6*>(node->ai_addr);
                ptr = &in_addr->sin6_addr;
            } else {
                continue;
            }
            ares_inet_ntop(node->ai_family, ptr, addr_buf, sizeof(addr_buf));
            printf("Resolved IP Address: %s\n", addr_buf);
        }
    }
    ares_freeaddrinfo(result);
}

int main()
{
    if (int err = ares_library_init(ARES_LIB_INIT_ALL); err != 0) {
        std::cerr << "ares: ares_library_init failed: " << err << std::endl;
        return 1;
    }

    io_uring ring{};
    if (int ret = io_uring_queue_init(512, &ring, 0); ret < 0) {
        throw std::system_error(-ret, std::generic_category(), "io_uring_queue_init failed");
    }

    State state{ring};

    struct ares_options options {};
    int optmask = 0;
    ares_channel_t *channel = nullptr;

    options.sock_state_cb = socket_changed_state_cb;
    options.sock_state_cb_data = &state;
    optmask |= ARES_OPT_SOCK_STATE_CB;

    int res = ::ares_init_options(&channel, &options, optmask);
    if (res != ARES_SUCCESS) {
        std::cerr << "Init failed: " << ::ares_strerror(res) << std::endl;
        io_uring_queue_exit(&ring);
        ares_library_cleanup();
        return 1;
    }

    // Assign channel back to state after successful initialization
    state.m_channel = channel;

    ares_addrinfo_hints hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    // Issue request
    ares_getaddrinfo(channel, "www.example.com", "443", &hints, addrinfo_cb, &state);

    // Run worker in a separate thread
    std::jthread worker_thread(worker, &state);

    // Wait until channel queues are fully processed
    ares_queue_wait_empty(channel, -1);

    // Shutdown io_uring worker safely
    io_uring_queue_exit(&ring);

    ares_destroy(channel);
    ares_library_cleanup();
    return 0;
}


/*
int main(int argc, char *argv[])
{
    int err = ares_library_init(ARES_LIB_INIT_ALL);
    if (err != 0) {
        std::cerr << "ares: ares_library_init failed: " << err << std::endl;
        return 1;
    }

    State state;

    struct ares_options options {};
    int optmask = 0;
    ares_channel_t *channel = nullptr;

    // CAN HAVE BOTH SOCKEt CALLBACK AND EV SYSTEM THREAD

    // options.sock_state_cb = socket_changed_state_cb;
    // options.sock_state_cb_data = &state;
    // optmask |= ARES_OPT_SOCK_STATE_CB;

    // FIX 1: Pass a valid null-terminated string literal

    static char lookups_str[] = "b";
    options.lookups = lookups_str;
    optmask |= ARES_OPT_LOOKUPS;

    // FIX 2: Use ARES_OPT_TIMEOUTMS for millisecond timeouts

    options.timeout = 2000;
    options.tries = 3;
    optmask |= ARES_OPT_TIMEOUTMS | ARES_OPT_TRIES;

    options.flags = ARES_FLAG_STAYOPEN;
    optmask |= ARES_OPT_FLAGS;

    options.evsys = ARES_EVSYS_DEFAULT;
    optmask |= ARES_OPT_EVENT_THREAD;

    int res = ::ares_init_options(&channel, &options, optmask);
    if (res != ARES_SUCCESS) {
        std::cerr << "Init failed: " << ::ares_strerror(res) << std::endl;
        ares_library_cleanup();
        return 1;
    }

    ares_addrinfo_hints hints{};
    // hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    ares_getaddrinfo(channel, "www.example.com", "443", &hints, addrinfo_cb, &state);

    ares_queue_wait_empty(channel, -1);

    ares_destroy(channel);
    ares_library_cleanup();
    return 0;
}
*/