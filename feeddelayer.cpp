#ifdef _WIN32
    #define _WIN32_WINNT 0x0A00
    #pragma comment (lib, "crypt32")
#endif

const unsigned DEFAULT_INTERVAL = 60;
const unsigned INITIAL_NUM_ELEMENTS = 100;
const unsigned NETWORK_TIMEOUT = 30;

#include "libs/beast/example/common/root_certificates.hpp"
#include <boost/asio.hpp>
#include <boost/atomic.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/strand.hpp>
#include <boost/program_options.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <boost/thread/thread.hpp>
#include <functional>
#include <iostream>
#include <memory>
#include <string>

//------------------------------------------------------------------------------

// Report a failure
void return_if_fail(boost::beast::error_code ec, char const* what)
{
    if (ec)
    {
        std::cerr << what << ": " << ec.message() << "\n";
    }
}

// Sends a WebSocket message and prints the response
class Session : public std::enable_shared_from_this<Session>
{
public:
    // Resolver and socket require an io_context
    explicit
        Session(boost::asio::io_context& ioc, boost::asio::ssl::context& ctx)
        : m_resolver(boost::asio::make_strand(ioc))
        , m_ws(boost::asio::make_strand(ioc), ctx)
        , m_buffers(INITIAL_NUM_ELEMENTS)
        , m_thread_ptr(nullptr)
        , m_thread_running(false)
        , m_interval_sec(DEFAULT_INTERVAL)
        , m_collect_stream(true)
    {}

    // Start the asynchronous operation
    void run(
            char const* host,
            char const* port,
            char const* target,
            char const* data,
            unsigned timeout_sec,
            bool collect_stream)
    {
        // Save these for later
        m_host = host;
        m_data = data;
        m_target = target;
        m_interval_sec = timeout_sec;
        m_collect_stream = collect_stream;

        // Look up the domain name
        m_resolver.async_resolve(
            host,
            port,
            boost::beast::bind_front_handler(
                &Session::on_resolve,
                shared_from_this()));


    }

protected:
    void on_resolve(
            boost::beast::error_code ec,
            boost::asio::ip::tcp::resolver::results_type results)
    {
        return_if_fail(ec, "resolve");

        // Set a timeout on the operation
        boost::beast::get_lowest_layer(m_ws).expires_after(
            std::chrono::seconds(NETWORK_TIMEOUT));

        // Make the connection on the IP address we get from a lookup
        boost::beast::get_lowest_layer(m_ws).async_connect(
            results,
            boost::beast::bind_front_handler(
                &Session::on_connect,
                shared_from_this()));
    }

    void on_connect(
            boost::beast::error_code ec, 
            boost::asio::ip::tcp::resolver::results_type::endpoint_type ep)
    {
        return_if_fail(ec, "connect");

        // Set a timeout on the operation
        boost::beast::get_lowest_layer(m_ws).expires_after(
            std::chrono::seconds(NETWORK_TIMEOUT));

        // Set SNI Hostname (many hosts need this to handshake successfully)
        if (!SSL_set_tlsext_host_name(
            m_ws.next_layer().native_handle(),
            m_host.c_str()))
        {
            return_if_fail(boost::beast::error_code(
                static_cast<int>(::ERR_get_error()),
                boost::asio::error::get_ssl_category()), "connect");
        }

        // Update the host_ string. This will provide the value of the
        // Host HTTP header during the WebSocket handshake.
        // See https://tools.ietf.org/html/rfc7230#section-5.4
        m_host += ':' + std::to_string(ep.port());

        // Perform the SSL handshake
        m_ws.next_layer().async_handshake(
            ssl::stream_base::client,
            boost::beast::bind_front_handler(
                &Session::on_ssl_handshake,
                shared_from_this()));
    }

    void on_ssl_handshake(boost::beast::error_code ec)
    {
        return_if_fail(ec, "ssl_handshake");

        // Turn off the timeout on the tcp_stream, because
        // the websocket stream has its own timeout system.
        boost::beast::get_lowest_layer(m_ws).expires_never();

        // Set suggested timeout settings for the websocket
        m_ws.set_option(
            boost::beast::websocket::stream_base::timeout::suggested(
                boost::beast::role_type::client));

        // Set a decorator to change the User-Agent of the handshake
        m_ws.set_option(boost::beast::websocket::stream_base::decorator(
            [](boost::beast::websocket::request_type& req)
        {
            req.set(boost::beast::http::field::user_agent,
                std::string(BOOST_BEAST_VERSION_STRING) +
                " websocket-client-async-ssl");
        }));

        // Perform the websocket handshake
        m_ws.async_handshake(m_host, m_target,
            boost::beast::bind_front_handler(
                &Session::on_handshake,
                shared_from_this()));
    }

    void on_handshake(boost::beast::error_code ec)
    {
        return_if_fail(ec, "handshake");

        // Send the message
        m_ws.async_write(
            boost::asio::buffer(m_data),
            boost::beast::bind_front_handler(
                &Session::on_write,
                shared_from_this()));
    }

    void on_write(
            boost::beast::error_code ec,
            std::size_t bytes_transferred)
    {
        boost::ignore_unused(bytes_transferred);
        return_if_fail(ec, "write");

        // Read a message into our buffer
        m_ws.async_read(
            m_buffer,
            boost::beast::bind_front_handler(
                &Session::on_read,
                shared_from_this()));
    }

    void on_read(
            boost::beast::error_code ec,
            std::size_t bytes_transferred)
    {
        boost::ignore_unused(bytes_transferred);
        return_if_fail(ec, "read");

        // The make_printable() function helps print a ConstBufferSequence
        m_last_stream = boost::beast::buffers_to_string(m_buffer.data());
        if (m_collect_stream)
        {
            m_buffers.push(m_last_stream);
        }
        m_buffer.clear();

        m_ws.async_read(
            m_buffer, 
            boost::beast::bind_front_handler(
                &Session::on_read,
                shared_from_this()));

        if (!m_thread_running)
        {
            m_thread_ptr = new boost::thread(&Session::display_and_clear, 
                this);
            m_thread_running = true;
        }
    }

    void on_close(boost::beast::error_code ec)
    {
        return_if_fail(ec, "close");
    }

    void display_and_clear()
    {
        boost::this_thread::sleep_for(boost::chrono::seconds{ 
            m_interval_sec });
        if (m_collect_stream)
        {
            std::string data;
            while (m_buffers.pop(data))
            {
                std::cout << data << std::endl;
            }
        }
        else
        {
            std::cout << m_last_stream << std::endl;
        }
        m_thread_running = false;
    }

private:
    boost::asio::ip::tcp::resolver m_resolver;
    boost::beast::websocket::stream<
        boost::beast::ssl_stream<boost::beast::tcp_stream>> m_ws;
    boost::lockfree::spsc_queue<std::string> m_buffers;
    boost::beast::flat_buffer m_buffer;
    boost::thread* m_thread_ptr;
    boost::atomic<bool> m_thread_running;
    std::string m_host;
    std::string m_data;
    std::string m_target;
    std::string m_last_stream;
    unsigned m_interval_sec;
    bool m_collect_stream;
};

//------------------------------------------------------------------------------

int main(int argc, char** argv)
{
    // Check command line arguments
    const std::string host = "ws.binaryws.com";
    const char* target = "/websockets/v3?app_id=1089";
    const char* port = "443";
    //const char* text = "{\"authorize\":\"k3BZ8tl6rooqhdV\"}";
    const char* data = "{\"ticks\":\"R_50\",\"subscribe\":1}";
    unsigned timeout_secs = 60;
    bool collect_stream = true;

    try
    {
        boost::program_options::options_description desc{ 
            "feeddelayer Options" };
        desc.add_options()
            ("help,h", "This help screen")
            ("timeout,t", 
                boost::program_options::value<unsigned>()->default_value(60), 
                "Timeout/Delay in secs (default = 60, 0 = no delay)")
            ("collect_stream,c", 
                boost::program_options::value<bool>()->default_value(true), 
                "Collect stream within delay (true = yes/default, false = no)");

        boost::program_options::variables_map vm;
        store(parse_command_line(argc, argv, desc), vm);
        notify(vm);

        if (vm.count("help"))
        {
            std::cout << desc << std::endl;
            return EXIT_SUCCESS;
        }
        
        if (vm.count("timeout"))
        {
            timeout_secs = vm["timeout"].as<unsigned>();
            if (timeout_secs == std::numeric_limits<unsigned>::max())
            {
                std::cout << "Invalid parameter/option" << std::endl;
                return EXIT_FAILURE;
            }
        }

        if (vm.count("collect_stream"))
        {
            collect_stream = vm["collect_stream"].as<bool>();
        }

        // The io_context is required for all I/O
        boost::asio::io_context ioc;

        // The SSL context is required, and holds certificates
        boost::asio::ssl::context ctx{ ssl::context::tlsv12_client };

        // This holds the root certificate used for verification
        load_root_certificates(ctx);

        // Launch the asynchronous operation
        std::make_shared<Session>(ioc, ctx)->run(host.c_str(), 
            port, target, data, timeout_secs, collect_stream);
        
        // Run the I/O service. The call will return when
        // the socket is closed.
        ioc.run();
    }
    catch (const boost::program_options::error &ex)
    {
        std::cerr << ex.what() << '\n';
    }

    return EXIT_SUCCESS;
}