module;

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>
#include <utility>
#include <system_error>
#include <optional>
#include <tuple>

export module NetworkCoreLibrary:BasicSocket;

import :SocketAddress;
import :SocketEndpoint;
import :CoreApi;

using namespace network_core::core_api;
using namespace network_core::socket_address;
using namespace network_core::socket_endpoint;

export namespace network_core::basic_socket
{
    class BasicFd {
    public:
        explicit BasicFd(SocketT fd = InvalidSocket) noexcept : m_fd(fd) {}

        ~BasicFd() noexcept {
            Close();
        }

        BasicFd(const BasicFd&) = delete;
        BasicFd& operator=(const BasicFd&) = delete;

        BasicFd(BasicFd&& other) noexcept
            : m_fd(std::exchange(other.m_fd, InvalidSocket)) {}

        BasicFd& operator=(BasicFd&& other) noexcept {
            if (this != &other) {
                Close();
                m_fd = std::exchange(other.m_fd, InvalidSocket);
            }
            return *this;
        }

        [[nodiscard]] SocketT Get() const noexcept { return m_fd; }
        [[nodiscard]] bool IsValid() const noexcept { return IsValidSocket(m_fd); }

        SocketT Release() noexcept {
            return std::exchange(m_fd, InvalidSocket);
        }

        void Close() noexcept {
            if (IsValid()) {
                ::close(m_fd);
                m_fd = InvalidSocket;
            }
        }

    private:
        SocketT m_fd{InvalidSocket};
    };

    class BasicSocket
    {
    public:
        struct Options {
            bool non_blocking{false};
            bool reuse_address{true};   // Recommended default for servers
            bool reuse_port{false};     // Useful for multi-threaded listeners
            bool tcp_nodelay{true};     // Disables Nagle's algorithm for lower latency
            bool keep_alive{true};      // Detects dead TCP peers
        };

        BasicSocket() noexcept = default;

        explicit BasicSocket(BasicFd fd, SocketEndpoint ep, Options options) noexcept
            : m_fd(std::move(fd))
            , m_endpoint(std::move(ep))
            , m_options(options) {}

        BasicSocket(const BasicSocket&) = delete;
        BasicSocket& operator=(const BasicSocket&) = delete;

        BasicSocket(BasicSocket&&) noexcept = default;
        BasicSocket& operator=(BasicSocket&&) noexcept = default;

        [[nodiscard]] SocketT GetFd() const noexcept { return m_fd.Get(); }
        [[nodiscard]] bool IsValid() const noexcept { return m_fd.IsValid(); }
        [[nodiscard]] const SocketEndpoint& GetEndpoint() const noexcept { return m_endpoint; }
        [[nodiscard]] const Options& GetOptions() const noexcept { return m_options; }

        SocketT ReleaseFd() noexcept { return m_fd.Release(); }

        [[nodiscard]] sockaddr* GetRawAddressPtr() noexcept {
            return const_cast<sockaddr*>(m_endpoint.GetAddress().getRawAddress());
        }

        [[nodiscard]] socklen_t GetRawAddressSize() const noexcept {
            return m_endpoint.GetAddress().getSize();
        }

        static std::tuple<BasicSocket, std::error_code> Create(SocketEndpoint endpoint, Options options) noexcept {
            SocketT fd = endpoint.CreateSocket();
            if (!IsValidSocket(fd)) {
                return { BasicSocket{}, std::error_code(errno, std::generic_category()) };
            }

            BasicSocket socket{ BasicFd{fd}, std::move(endpoint), options };
            if (auto err = socket.ApplyOptions(options); err) {
                return { BasicSocket{}, err };
            }

            return { std::move(socket), std::error_code{} };
        }

        static BasicSocket FromRaw(SocketT raw_fd, SocketEndpoint ep, Options options) noexcept {
            return BasicSocket{ BasicFd{raw_fd}, std::move(ep), options };
        }

        std::error_code Bind() noexcept {
            const auto* raw_addr = m_endpoint.GetAddress().getRawAddress();
            socklen_t addr_len = m_endpoint.GetAddress().getSize();

            if (::bind(GetFd(), raw_addr, addr_len) != 0) {
                return std::error_code(errno, std::generic_category());
            }
            return {};
        }

        std::error_code Listen(int backlog = SOMAXCONN) noexcept {
            if (::listen(GetFd(), backlog) != 0) {
                return std::error_code(errno, std::generic_category());
            }
            return {};
        }

        std::error_code ApplyOptions(Options opts) noexcept {
            m_options = opts;
            if (!IsValid()) return {};

            int fd = GetFd();

            // Non-blocking
            if (opts.non_blocking) {
                int flags = ::fcntl(fd, F_GETFL, 0);
                if (flags == -1 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
                    return std::error_code(errno, std::generic_category());
                }
            }

            // SO_REUSEADDR
            int opt_val = opts.reuse_address ? 1 : 0;
            if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(opt_val)) != 0) {
                return std::error_code(errno, std::generic_category());
            }

            // SO_REUSEPORT
            if (opts.reuse_port) {
                opt_val = 1;
                if (::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt_val, sizeof(opt_val)) != 0) {
                    return std::error_code(errno, std::generic_category());
                }
            }

            // TCP_NODELAY (TCP Stream only)
            if (m_endpoint.GetType() == SocketType::Stream) {
                opt_val = opts.tcp_nodelay ? 1 : 0;
                ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt_val, sizeof(opt_val));
            }

            // SO_KEEPALIVE
            opt_val = opts.keep_alive ? 1 : 0;
            ::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &opt_val, sizeof(opt_val));

            return {};
        }

    private:
        BasicFd m_fd;
        SocketEndpoint m_endpoint;
        Options m_options;
    };
}