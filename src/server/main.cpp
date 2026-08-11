#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/provider.h>
#include <memory>
#include <queue>
#include <exception>
#include <stack>

import SecurityCoreLibrary;

using namespace security_core::common_types;
using namespace security_core::biow;

import NetworkCoreLibrary;
import CoreUtility;
using core_utility::GetTime;
using namespace network_core::socket_address;
using namespace network_core::core_api;

constexpr std::size_t BUFFER_SIZE = 16 * 1024;

void handle_client(BIO* client_bio) {
    std::vector<char> buffer(BUFFER_SIZE);

    // 1. Perform the TLS Handshake as Server
    if (BIO_do_handshake(client_bio) <= 0) {
        std::cerr << "[+] TLS Handshake failed.\n";
        ERR_print_errors_fp(stderr);
        BIO_free_all(client_bio);
        return;
    }

    std::cout << "[+] TLS Handshake successful!\n";

    // 2. Read incoming client request
    int bytes_read = BIO_read(client_bio, buffer.data(), buffer.size() - 1);
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        std::cout << "[+] Received request:\n" << buffer.data() << "\n";

        // 3. Send a simple HTTP 200 OK response
        std::string response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Connection: close\r\n"
            "\r\n"
            "Hello from C++ TLS Server!\n";

        BIO_write(client_bio, response.c_str(), response.length());
        BIO_flush(client_bio);
    }

    // 4. Shutdown TLS session cleanly and free the client chain
    BIO_shutdown_wr(client_bio);
    BIO_free_all(client_bio);
    std::cout << "[+] Client disconnected.\n";
}


int main(int argc, char *argv[])
{
    // // Configure your paths and port here
    // std::string keyfile  = "key.pem";   // Private Key file
    // std::string certfile = "cert.pem";  // Certificate Chain file
    // std::string port     = "8443";      // Port to listen on
    //
    // // Initialize OpenSSL Context
    // SSL_CTX* ctx = SSL_CTX_new(TLS_server_method());
    // if (!ctx) {
    //     std::cerr << "[-] Failed to create SSL_CTX\n";
    //     return 1;
    // }
    //
    // // Load Private Key
    // if (SSL_CTX_use_PrivateKey_file(ctx, keyfile.c_str(), SSL_FILETYPE_PEM) <= 0) {
    //     std::cerr << "[-] Failed to load private key file: " << keyfile << "\n";
    //     ERR_print_errors_fp(stderr);
    //     SSL_CTX_free(ctx);
    //     return 1;
    // }
    //
    // // Load Certificate Chain (FIX: used certfile instead of keyfile)
    // if (SSL_CTX_use_certificate_chain_file(ctx, certfile.c_str()) <= 0) {
    //     std::cerr << "[-] Failed to load certificate file: " << certfile << "\n";
    //     ERR_print_errors_fp(stderr);
    //     SSL_CTX_free(ctx);
    //     return 1;
    // }
    //
    // // Verify key matches cert
    // if (SSL_CTX_check_private_key(ctx) <= 0) {
    //     std::cerr << "[-] Private key does not match certificate!\n";
    //     SSL_CTX_free(ctx);
    //     return 1;
    // }
    //
    // SSL_CTX_set_mode(ctx, SSL_MODE_AUTO_RETRY);
    //
    // // Create Accept BIO listening on the given port
    // BIO* accept_bio = BIO_new_accept(port.c_str());
    // if (!accept_bio) {
    //     std::cerr << "[-] Failed to create accept BIO\n";
    //     SSL_CTX_free(ctx);
    //     return 1;
    // }
    //
    // // First call to BIO_do_accept binds and sets up listening socket
    // if (BIO_do_accept(accept_bio) <= 0) {
    //     std::cerr << "[-] BIO_do_accept failed to bind on port " << port << "\n";
    //     ERR_print_errors_fp(stderr);
    //     BIO_free_all(accept_bio);
    //     SSL_CTX_free(ctx);
    //     return 1;
    // }
    //
    // std::cout << "[*] TLS Server listening on port " << port << "...\n";
    //
    // // Server main loop
    // while (true) {
    //     // Second call to BIO_do_accept waits for incoming TCP connections
    //     if (BIO_do_accept(accept_bio) <= 0) {
    //         std::cerr << "[-] Error accepting connection\n";
    //         break;
    //     }
    //
    //     // Pop the newly accepted client raw socket BIO
    //     BIO* client_socket_bio = BIO_pop(accept_bio);
    //
    //     // Attach an SSL layer to the accepted client socket
    //     BIO* ssl_bio = BIO_new_ssl(ctx, 0 /* 0 = server mode */);
    //     BIO* client_bio = BIO_push(ssl_bio, client_socket_bio);
    //
    //     // Process client request
    //     handle_client(client_bio);
    // }
    //
    // // Cleanup resources
    // BIO_free_all(accept_bio);
    // SSL_CTX_free(ctx);


    // UniqueBIOPtr bio_file_reader{BIO_new(BIO_s_file()), &BIO_free};
    // if (BIO_read_filename(bio_file_reader.get(), "text.txt") <= 0)
    // {
    //     std::cout << "BIO_read_filename failed\n";
    //     return 0;
    // }
    // auto inBuffer = std::make_unique<char[]>(50);
    // std::size_t read_bytes = 0;
    // if (BIO_read_ex(bio_file_reader.get(), inBuffer.get(), 49, &read_bytes) <= 0)
    // {
    //     std::cout << "Error while reading from file\n";
    //     return 0;
    // }
    // std::cout << "Read bytes: " << read_bytes << "\n";
    // inBuffer.get()[read_bytes] = '\0';
    // std::cout << "Read data:\n";
    // for (int i = 0; i < read_bytes; ++i)
    // {
    //     std::cout << inBuffer.get()[i] << "\n";
    // }

    //BIO_read_ex(bio_file_reader.get(), )

    // read util
/*
static auto clearFunction = [](unsigned char* ptr) {
        OPENSSL_free(ptr);
    };

    using UniqueBIOPtr = std::unique_ptr<BIO, decltype(&BIO_free)>;
    using UniqueBioChainPtr = std::unique_ptr<BIO, decltype(&BIO_free_all)>;
    // Fixed: Changed 'unsigned char[]' to 'unsigned char'
    using HexStrPtr = std::unique_ptr<unsigned char, decltype(clearFunction)>;
    using OsslProviderPtr = std::unique_ptr<OSSL_PROVIDER, decltype(&OSSL_PROVIDER_unload)>;

    enum class ProgramMode { EncryptMode = 1, DecryptMode = 0 };
    ProgramMode mode = ProgramMode::DecryptMode; // Set to Decrypt

    // 1. Providers
    OsslProviderPtr legacy_provider{OSSL_PROVIDER_load(NULL, "legacy"), OSSL_PROVIDER_unload};
    OsslProviderPtr default_provider{OSSL_PROVIDER_load(NULL, "default"), OSSL_PROVIDER_unload};

    if (!legacy_provider || !default_provider) {
        std::cerr << "Failed to load OpenSSL providers!\n";
        return 1;
    }

    // 2. File BIO
    UniqueBIOPtr bio_file_reader{BIO_new(BIO_s_file()), BIO_free};
    if (BIO_read_filename(bio_file_reader.get(), "file.bin") <= 0) {
        std::cerr << "BIO_read_filename failed\n";
        return 1;
    }

    // 3. Buffer BIO
    UniqueBIOPtr bio_s_buffer{BIO_new(BIO_f_buffer()), BIO_free};
    if (BIO_set_buffer_size(bio_s_buffer.get(), 400) <= 0) {
        std::cerr << "Buffer size failed\n";
        return 1;
    }

    // 4. Cipher BIO setup
    UniqueBIOPtr bio_f_cipher{BIO_new(BIO_f_cipher()), BIO_free};
    const EVP_CIPHER *cipher = EVP_bf_cbc();
    std::string key = "991f27c302e73411b035dc6ca58271a0";
    std::string iv  = "f193c505552a8726";

    long length = 0;
    HexStrPtr hexkey{OPENSSL_hexstr2buf(key.c_str(), &length), clearFunction};
    HexStrPtr hexiv{OPENSSL_hexstr2buf(iv.c_str(), &length), clearFunction};

    if (!hexkey || !hexiv) {
        std::cerr << "Failed to parse Hex Key or IV\n";
        return 1;
    }

    // Note: static_cast<int>(mode) passes 0 for decryption or 1 for encryption
    if (BIO_set_cipher(bio_f_cipher.get(), cipher, hexkey.get(), hexiv.get(), static_cast<int>(mode)) <= 0) {
        std::cerr << "Failed to load the cipher\n";
        return 1;
    }

    // 5. Build Chain: [cipher] -> [buffer] -> [file]
    BIO_push(bio_f_cipher.get(), bio_s_buffer.get());
    BIO* head = BIO_push(bio_f_cipher.get(), bio_file_reader.get());

    if (!head) {
        std::cerr << "Failed to assemble BIO chain!\n";
        return 1;
    }

    // Transfer ownership safely
    bio_f_cipher.release();
    bio_s_buffer.release();
    bio_file_reader.release();

    UniqueBioChainPtr chain{head, BIO_free_all};

    // 6. Read Loop
    std::size_t buffer_size = 100;
    auto inBuffer = std::make_unique<char[]>(buffer_size + 1);
    std::size_t read_bytes = 0;
    int err = 0;

    do {
        err = BIO_read_ex(chain.get(), inBuffer.get(), buffer_size, &read_bytes);
        if (read_bytes > 0) {
            inBuffer[read_bytes] = '\0';
            std::cout << inBuffer.get();
        }
    } while (err > 0 && read_bytes > 0);

    std::cout << "\n[+] Reading finished successfully.\n";
    */

    // OsslProviderPtr legacy_provider{OSSL_PROVIDER_load(NULL, "legacy"), OSSL_PROVIDER_unload};
    // OsslProviderPtr default_provider{OSSL_PROVIDER_load(NULL, "default"), OSSL_PROVIDER_unload};
    //
    // if (!legacy_provider || !default_provider) {
    //     std::cerr << "Failed to load OpenSSL providers!\n";
    //     return 1;
    // }
    //
    // BioChain chain;
    //
    // // File => buffer => cipher => read part => write part => File
    //
    // UniqueBIOPtr fileReaderLayer{BIO_new(BIO_s_file()), BioFree};
    // UniqueBIOPtr bufferLayer{BIO_new(BIO_f_buffer()), BioFree};
    // UniqueBIOPtr cipherLayer{BIO_new(BIO_f_cipher()), BioFree};
    // UniqueBIOPtr fileWriterLayer{BIO_new(BIO_s_file()), BioFree};
    //
    // BioLink fileReaderLink{fileReaderLayer.release(), [](BIO *bio)
    // {
    //     std::string filename;
    //     std::cout << "[+] Enter filename: ";
    //     std::getline(std::cin, filename);
    //     if(BIO_read_filename(bio, filename.c_str()) <= 0)
    //     {
    //         throw std::runtime_error{"Cannot open the file"};
    //     }
    // }};
    // BioLink bufferLink{
    //     bufferLayer.release(), [](BIO *bio)
    //     {
    //         if (BIO_set_buffer_size(bio, 200) <= 0 )
    //         {
    //             throw std::runtime_error{"Cannot set buffer size"};
    //         }
    //     }
    // };
    // std::shared_ptr<IBioDecryptProcessor> keyProvider = std::make_shared<BioConsoleDecryptProvider>();
    // BioLink cipherLink{
    //     cipherLayer.release(), [provider = keyProvider](BIO *bio)
    //     {
    //         (*provider)(bio);
    //     }
    //     //std::string key = "991f27c302e73411b035dc6ca58271a0";
    //     //std::string iv  = "f193c505552a8726";
    //     //std::string salt = "3170c8e232e5c882";
    // };
    //
    // //
    // BioLink fileWriterLink{fileWriterLayer.release(), [](BIO *bio)
    // {
    //     std::string filename;
    //     std::cout << "[+] Enter name of decrypted file: ";
    //     std::getline(std::cin, filename);
    //     if(BIO_write_filename(bio, filename.data()) <= 0)
    //     {
    //         throw std::runtime_error{"Cannot open the file"};
    //     }
    // }};
    // fileWriterLink.apply_processor();
    // UniqueBIOPtr fileWriterChain{fileWriterLink.release(), BioFree};
    // //
    //
    // auto fullChain = chain
    //     .AddBioPtr(std::move(cipherLink))
    //     .AddBioPtr(std::move(bufferLink))
    //     .AddBioPtr(std::move(fileReaderLink))
    // .BuildChain();
    //
    // // read, decrypt and write
    // std::size_t buffer_size = 100;
    // auto inBuffer = std::make_unique<char[]>(buffer_size + 1);
    // std::size_t read_bytes = 0;
    // int err = 0;
    //
    // do {
    //     err = BIO_read_ex(fullChain.get(), inBuffer.get(), buffer_size, &read_bytes);
    //     if (read_bytes > 0) {
    //         inBuffer[read_bytes] = '\0';
    //         BIO_write(fileWriterChain.get(), inBuffer.get(), read_bytes);
    //     }
    // } while (err > 0 && read_bytes > 0);
    //

    //ListAdapters(GetAllAdapters());

    GetTime();

    return 0;
}