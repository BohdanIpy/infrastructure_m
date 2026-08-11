#include <iostream>
#include <variant>
#include <cstring>
#include <vector>

#include "openssl/ssl.h"
#include "openssl/tls1.h"

#include <openssl/ssl.h>
#include <openssl/err.h>


import CoreUtility;

using namespace core_utility;

// TLS basics

constexpr std::size_t BUFFER_SIZE = 16 * 1024;

int main(int argc, char* argv[])
{
    // Initialize OpenSSL context
    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        std::cerr << "Failed to create SSL context\n";
        return 1;
    }

    if (SSL_CTX_set_default_verify_paths(ctx) <= 0) {
        std::cerr << "SSL_CTX_set_default_verify_paths() failed\n";
        SSL_CTX_free(ctx);
        return 1;
    }

    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
    SSL_CTX_set_mode(ctx, SSL_MODE_AUTO_RETRY);

    std::string hostname = "example.org";
    std::string port = "443";

    // Set up BIO chain
    BIO* ssl_bio = BIO_new_ssl_connect(ctx);
    if (!ssl_bio) {
        std::cerr << "Failed to create BIO\n";
        SSL_CTX_free(ctx);
        return 1;
    }

    // Set host:port format for BIO
    std::string target = hostname + ":" + port;
    BIO_set_conn_hostname(ssl_bio, target.c_str());

    // Configure SNI and Hostname Verification on the underlying SSL pointer
    SSL* ssl = nullptr;
    BIO_get_ssl(ssl_bio, &ssl);
    if (!ssl) {
        std::cerr << "Failed to retrieve SSL pointer from BIO\n";
        BIO_free_all(ssl_bio);
        SSL_CTX_free(ctx);
        return 1;
    }

    SSL_set_tlsext_host_name(ssl, hostname.c_str());
    SSL_set1_host(ssl, hostname.c_str());

    // Connect & Perform Handshake
    if (BIO_do_connect(ssl_bio) <= 0) {
        std::cerr << "Cannot connect to host\n";
        ERR_print_errors_fp(stderr);
        BIO_free_all(ssl_bio);
        SSL_CTX_free(ctx);
        return 1;
    }

    // Verify TLS handshake succeeded
    if (BIO_do_handshake(ssl_bio) <= 0) {
        std::cerr << "TLS handshake failed\n";
        ERR_print_errors_fp(stderr);
        BIO_free_all(ssl_bio);
        SSL_CTX_free(ctx);
        return 1;
    }

    // Allocate buffer
    std::vector<char> buffer(BUFFER_SIZE);

    // FIX 1: Format HTTP request directly into buffer
    int request_length = snprintf(
        buffer.data(), buffer.size(),
        "GET / HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "User-Agent: Example TLS client\r\n"
        "\r\n",
        hostname.c_str()
    );

    std::cout << "*** Sending to the server:\n" << buffer.data() << "\n";

    // FIX 2: Send the formatted request string
    int nbytes_written = BIO_write(ssl_bio, buffer.data(), request_length);
    if (nbytes_written <= 0) {
        std::cerr << "BIO_write() failed\n";
        BIO_free_all(ssl_bio);
        SSL_CTX_free(ctx);
        return 1;
    }

    std::cout << "*** Server Response:\n";

    while (true) {
        int nbytes_read = BIO_read(ssl_bio, buffer.data(), buffer.size() - 1);
        if (nbytes_read <= 0) {
            break;
        }

        buffer[nbytes_read] = '\0';
        std::cout.write(buffer.data(), nbytes_read);
    }

    std::cout << "\n\n*** Connection Closed Successfully.\n";

    // Cleanup OpenSSL objects
    BIO_free_all(ssl_bio); // Automatically frees the attached SSL object
    SSL_CTX_free(ctx);
    return 0;
}
