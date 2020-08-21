#include "network/terrier_server.h"

#include <unistd.h>
#include <fstream>
#include <memory>
#include <sys/un.h>

#include "common/dedicated_thread_registry.h"
#include "common/settings.h"
#include "common/utility.h"
#include "loggers/network_logger.h"
#include "network/connection_handle_factory.h"
#include "network/network_defs.h"

namespace terrier::network {

TerrierServer::TerrierServer(common::ManagedPointer<ProtocolInterpreter::Provider> protocol_provider,
                             common::ManagedPointer<ConnectionHandleFactory> connection_handle_factory,
                             common::ManagedPointer<common::DedicatedThreadRegistry> thread_registry,
                             const uint16_t port, const uint16_t connection_thread_count, const bool use_unix_socket,
                             const std::string& socket_directory)
    : DedicatedThreadOwner(thread_registry),
      running_(false),
      use_unix_socket_(use_unix_socket),
      port_(port),
      socket_directory_(socket_directory),
      max_connections_(connection_thread_count),
      connection_handle_factory_(connection_handle_factory),
      provider_(protocol_provider) {
  // For logging purposes
  //  event_enable_debug_mode();

  //  event_set_log_callback(LogCallback);

  // Commented because it's not in the libevent version we're using
  // When we upgrade this should be uncommented
  //  event_enable_debug_logging(EVENT_DBG_ALL);

  // Ignore the broken pipe signal, return EPIPE on pipe write failures.
  // We don't want to exit on write when the client disconnects
  signal(SIGPIPE, SIG_IGN);
}

template<TerrierServer::SocketType type>
void TerrierServer::RegisterSocket() {
  static_assert(type == NETWORKED_SOCKET || type == UNIX_DOMAIN_SOCKET, "There should only be two socket types.");

  constexpr auto conn_backlog = common::Settings::CONNECTION_BACKLOG;
  constexpr auto is_networked_socket = type == NETWORKED_SOCKET;
  constexpr std::string_view socket_description = is_networked_socket ? "networked" : "Unix domain";

  auto &socket_fd = is_networked_socket ? network_socket_fd_ : unix_domain_socket_fd_;

  // Gets the appropriate sockaddr for the given SocketType.
  auto socket_addr = ([&] {
    if constexpr (is_networked_socket) {
      struct sockaddr_in sin = {0};

      sin.sin_family = AF_INET;
      sin.sin_addr.s_addr = INADDR_ANY;
      sin.sin_port = htons(port_);

      return sin;
    } else {
      // Builds the socket path name
      // Naming conventions come from https://www.postgresql.org/docs/9.3/runtime-config-connection.html
      const std::string socket_path = fmt::format("{0}/.s.PGSQL.{1}", socket_directory_, port_);
      struct sockaddr_un sun = {0};
      memset(&sun, 0, sizeof(struct sockaddr_un));

      // Validate pathname
      if (socket_path.length() >= 108 /* Max Unix socket path length */ ) {
        NETWORK_LOG_ERROR("Unix domain socket name too long (should be at most 108 characters)");
        throw NETWORK_PROCESS_EXCEPTION(fmt::format("Failed to name {} socket.", socket_description));
      }

      sun.sun_family = AF_UNIX;
      std::strcpy(sun.sun_path, socket_path.c_str());

      return sun;
    }
  })();

  // Create socket
  socket_fd = socket(is_networked_socket ? AF_INET : AF_UNIX, SOCK_STREAM, 0);

  // Check if socket was successfully created
  if (socket_fd < 0) {
    NETWORK_LOG_ERROR("Failed to open {} socket: {}", socket_description, strerror(errno));
    throw NETWORK_PROCESS_EXCEPTION(fmt::format("Failed to open {} socket.", socket_description));
  }

  // For networked sockets, tell the kernel that we would like to reuse local addresses whenever possible.
  if constexpr(is_networked_socket) {
    int reuse = 1;
    setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  }

  // Bind the socket
  int status = bind(socket_fd, reinterpret_cast<struct sockaddr *>(&socket_addr), sizeof(socket_addr));
  if (status < 0) {
    NETWORK_LOG_ERROR("Failed to bind {} socket: {}", socket_description, strerror(errno), errno);

    // We can recover from exactly one type of error here, contingent on this being a Unix domain socket.
    if constexpr (!is_networked_socket) {
      auto recovered = false;

      if (errno == EADDRINUSE) {
        // I find this disgusting, but it's the approach favored by a bunch of software that uses Unix domain sockets.
        // BSD syslogd, for example, does this in *every* case--error handling or not--and I'm not one to question it.
        recovered = !std::remove(fmt::format("{0}.s.PGSQL.{1}", socket_directory_, port_).c_str()) &&
                    bind(socket_fd, reinterpret_cast<struct sockaddr *>(&socket_addr), sizeof(socket_addr)) >= 0;
      }

      if (recovered) {
        NETWORK_LOG_INFO("Recovered! Managed to bind {} socket by purging a pre-existing bind.", socket_description)
      } else {
        throw NETWORK_PROCESS_EXCEPTION(fmt::format("Failed to bind and recover {} socket.", socket_description));
      }
    } else {
      throw NETWORK_PROCESS_EXCEPTION(fmt::format("Failed to bind {} socket.", socket_description));
    }
  }

  // Listen on the socket
  status = listen(socket_fd, conn_backlog);
  if (status < 0) {
    NETWORK_LOG_ERROR("Failed to listen on {} socket: {}", socket_description, strerror(errno));
    throw NETWORK_PROCESS_EXCEPTION(fmt::format("Failed to listen on {} socket.", socket_description));
  }

  dispatcher_task_ = thread_registry_->RegisterDedicatedThread<ConnectionDispatcherTask>(
      this /* requester */, max_connections_, socket_fd, this, common::ManagedPointer(provider_.Get()),
      connection_handle_factory_, thread_registry_);

  NETWORK_LOG_INFO("Listening on {} socket with port {} [PID={}]", socket_description, port_, ::getpid());
}

void TerrierServer::RunServer() {
  // This line is critical to performance for some reason
  // evthread_use_pthreads(); // TODO(jordig) Test and see how important this really is.
  // Update... Done! Very important! Bug spotted.

  // Register the network socket
  RegisterSocket<NETWORKED_SOCKET>();

  // Register the Unix domain socket if Unix domain sockets have been turned on
  if (use_unix_socket_) {
    // TODO(jordig) Why would we need a lockfile á la Postgres? What do they even use it for?
//    RegisterSocket<UNIX_DOMAIN_SOCKET>();
  }

  // Set the running_ flag for any waiting threads
  {
    std::lock_guard<std::mutex> lock(running_mutex_);
    running_ = true;
  }
}

void TerrierServer::StopServer() {
  NETWORK_LOG_TRACE("Begin to stop server");
  const bool result UNUSED_ATTRIBUTE =
      thread_registry_->StopTask(this, dispatcher_task_.CastManagedPointerTo<common::DedicatedThreadTask>());
  TERRIER_ASSERT(result, "Failed to stop ConnectionDispatcherTask.");

  // Close the network socket
  TerrierClose(network_socket_fd_);

  // Close the Unix domain socket if it exists
  if (use_unix_socket_ && unix_domain_socket_fd_ >= 0) {
    std::remove(fmt::format("{0}.s.PGSQL.{1}", socket_directory_, port_).c_str());
    TerrierClose(unix_domain_socket_fd_); // TODO(jordig) Does this do *anything*?
  }

  NETWORK_LOG_INFO("Server Closed");

  // Clear the running_ flag for any waiting threads and wake up them up with the condition variable
  {
    std::lock_guard<std::mutex> lock(running_mutex_);
    running_ = false;
  }
  running_cv_.notify_all();
}

/**
 * Change port to new_port
 */
void TerrierServer::SetPort(uint16_t new_port) { port_ = new_port; }

}  // namespace terrier::network
