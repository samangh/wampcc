#include "IOLoop.h"

#include "Logger.h"
#include "IOHandle.h"

#include <iostream>

#include <unistd.h>
#include <string.h>


#define SYSTEM_HEARTBEAT_MS 10

namespace XXX {



/* Called on the IO thread when a ioreq attempt has completed */
static void __on_tcp_connect_cb(uv_connect_t* __req, int status )
{
  std::unique_ptr<uv_connect_t> connect_req(__req);

  // IOLoop has set itself as the uv_loop data member
  IOLoop * ioloop = static_cast<IOLoop* >(__req->handle->loop->data);

  // get the user object
  std::unique_ptr<io_request> ioreq  (( io_request*) connect_req->data );
  Logger * __logptr = ioreq->logptr;

  if (status < 0)
  {
    // libuv, the status is the negative of the errno (on linux)
    _WARN_( "connect error, " <<  uv_strerror(status) );

    ioloop->add_active_handle(nullptr, abs(status), ioreq.get());

    delete ioreq->tcp_handle;
  }
  else
  {
    IOHandle* ioh = new IOHandle( __logptr,
                                  (uv_stream_t *) ioreq->tcp_handle,
                                  ioloop );
    ioloop->add_active_handle(ioh, 0, ioreq.get());
  }

}

static void __on_tcp_connect(uv_stream_t* server, int status)
{
  // IOLoop has set itself as the uv_loop data member
  tcp_server* myserver = (tcp_server*) server;
  IOLoop * myIOLoop = static_cast<IOLoop* >(server->loop->data);

  if (status < 0)
  {
    // TODO: logging
    fprintf(stderr, "New connection error %s\n", uv_strerror(status));
    return;
  }

  uv_tcp_t *client = new uv_tcp_t();
  uv_tcp_init(myIOLoop->uv_loop(), client);

  if (uv_accept(server, (uv_stream_t *) client) == 0)
  {
    IOHandle* ioh = new IOHandle( myIOLoop->logptr(),
                                  (uv_stream_t *) client, myIOLoop);

    int fd = client->io_watcher.fd;
    std::cout << "accept: type=" << client->type
              << ", fd=" << fd << "\n";

    if (client->type == UV_TCP)
    {
      std::cout << "got tcp fd "<<  fd<< "\n";
    }

    // register the stream before beginning read operations
    myserver->ioloop->add_passive_handle(myserver, ioh );

  //   // NOTE: registration of read event moved into the handle

  //   // // new client is accepted, identified via its stream handle ... need to put
  //   // // in place a lot more session tracking here.
  //   // uv_read_start((uv_stream_t *) client, alloc_buffer, io_on_read);
  }
  else
  {
    uv_close((uv_handle_t *) client, NULL);
  }

}


IOLoop::IOLoop(Logger * logptr)
  : __logptr( logptr),
    m_uv_loop( new uv_loop_t() ),
    m_timer( new uv_timer_t() ),
    m_async( new uv_async_t() ),
    m_pending_flags( eNone )
{
  uv_loop_init(m_uv_loop);
  m_uv_loop->data = this;

  // set up the async handler
  uv_async_init(m_uv_loop, m_async.get(), [](uv_async_t* h) {
      IOLoop* p = static_cast<IOLoop*>( h->data );
      p->on_async();
    });
  m_async->data = this;

  // set up timer

  uv_timer_init(m_uv_loop, m_timer.get());
  m_timer->data = this;
  uv_timer_start(m_timer.get(), [](uv_timer_t* h){
      IOLoop* p = static_cast<IOLoop*>( h->data );
      p->on_timer();
    }, SYSTEM_HEARTBEAT_MS, SYSTEM_HEARTBEAT_MS);

  // TODO: I prefer to not have to do this.  Need to review what is the correct
  // policy for handling sigpipe using libuv.
  //  signal(SIGPIPE, SIG_IGN);
}

void IOLoop::stop()
{
  {
    std::lock_guard< std::mutex > guard (m_pending_requests_lock);
    m_pending_flags |= eFinal;
  }

  async_send();
  if (m_thread.joinable()) m_thread.join();
}

IOLoop::~IOLoop()
{
  for (auto & item : m_handles) delete item;
  m_handles.clear();

  // uv_loop kept as raw pointer, because need to pass to several uv functions
  uv_loop_close(m_uv_loop);
  delete m_uv_loop;
}

void IOLoop::create_tcp_server_socket(int port,
                                      socket_accept_cb cb
  )
{
  /* UV Loop */

  // Create a tcp socket, and configure for listen
  tcp_server * myserver = new tcp_server();
  myserver->port   = port;
  myserver->ioloop = this;
  myserver->cb = cb;

  uv_tcp_init(m_uv_loop, &myserver->uvh);

  struct sockaddr_in addr;
  uv_ip4_addr("0.0.0.0", port, &addr);

  unsigned flags = 0;
  uv_tcp_bind(&myserver->uvh, (const struct sockaddr*)&addr, flags);
  int r = uv_listen((uv_stream_t *) &myserver->uvh, 5, __on_tcp_connect);
  if (!r)
  {
    // TODO: handle listen failure
  }

  m_server_handles.push_back( std::unique_ptr<tcp_server>(myserver) );
}


void IOLoop::on_async()
{
  /* IO thread */

  std::vector< io_request > work;
  int pending_flags = eNone;

  {
    std::lock_guard< std::mutex > guard (m_pending_requests_lock);
    work.swap( m_pending_requests );
    std::swap(pending_flags, m_pending_flags);
  }

  if (m_async_closed) return;

  if (pending_flags & eFinal)
  {
    for (auto & i : m_handles)
      i->close_async();

    for (auto & i : m_server_handles)
      uv_close((uv_handle_t*)i.get(), 0);

    uv_close((uv_handle_t*) m_timer.get(), 0);
    uv_close((uv_handle_t*) m_async.get(), 0);

    m_async_closed = true;
    return;
  }

  for (auto & item : work)
  {
    if (item.addr.empty())
    {
      // TODO: check we dont already have port on this.
      create_tcp_server_socket(item.port, item.on_accept);
    }
    else
    {
      std::unique_ptr<io_request> req( new io_request( item ) );

      // use C++ allocator for the uv objects, so we can use vanilla unique_ptr

      req->tcp_handle = new uv_tcp_t();
      uv_tcp_init(m_uv_loop, req->tcp_handle);

      uv_connect_t * connect_req = new uv_connect_t();
      connect_req->data = (void*) req.get();

      struct sockaddr_in dest;
      memset(&dest, 0, sizeof(sockaddr_in));
      uv_ip4_addr(item.addr.c_str(), item.port, &dest);

      _INFO_("making TCP connection to " << item.addr.c_str() <<  ":" << item.port);
      int r = uv_tcp_connect(connect_req,
                             req->tcp_handle,
                             (const struct sockaddr*) &dest,
                             __on_tcp_connect_cb);
      if (r == 0)
      {
        req.release(); // owner transfered to UV callback
      }
      else
      {
        delete connect_req;
        delete req->tcp_handle;

        if (m_new_client_cb)
          m_new_client_cb( nullptr,
                           r<0?-r:r,
                           req->user_conn_id);
      }
    }
  }

}


void IOLoop::async_send()
{
  /* ANY thread */

  // cause the IO thread to wake up
  uv_async_send( m_async.get() );
}

void IOLoop::run_loop()
{
  while ( true )
  {
    try
    {
      int r = uv_run(m_uv_loop, UV_RUN_DEFAULT);

      // if r == 0, there are no more handles; implies we are shutting down.
      if (r == 0) return;
    }
    catch(...)
    {
      // TODO: log me properly, and test these can come here
      std::cout << "exception in IOLoop thread\n";
    }
  }
}

void IOLoop::start()
{
  m_thread = std::thread(&IOLoop::run_loop, this);
}



void IOLoop::on_timer()
{


  bool need_remove = false;
  for (auto & item  : m_handles)
  {
    if (item->can_be_deleted())
    {
      delete item;
      item = nullptr;
      need_remove = true;
    }
  }
  if (need_remove) m_handles.remove( nullptr );
}

void IOLoop::add_server(int port, socket_accept_cb cb)
{
  {
    std::lock_guard< std::mutex > guard (m_pending_requests_lock);
    io_request r( __logptr );
    r.port = port;
    r.on_accept = cb;
    m_pending_requests.push_back( r );
  }

  this->async_send();
}


void IOLoop::add_passive_handle(tcp_server* myserver, IOHandle* iohandle)
{
  m_handles.push_back( iohandle );

  if (myserver->cb) myserver->cb(myserver->port, iohandle);
}


/* Here we have completed a tcp connect attempt (either successfully or
 * unsuccessfully).  Next we shall escalate the result upto the handler.
 */
void IOLoop::add_active_handle(IOHandle * iohandle, int status, io_request * req)
{
  /* IO thread */

  if (iohandle) m_handles.push_back( iohandle );

  if (m_new_client_cb)
    m_new_client_cb( iohandle,
                     status,
                     req->user_conn_id);
}


void IOLoop::add_connection(std::string addr,
                            int port,
                            t_connection_id user_conn_id)
{
  {
    std::lock_guard< std::mutex > guard (m_pending_requests_lock);
    io_request r( __logptr );
    r.addr = addr;
    r.port = port;
    r.user_conn_id = user_conn_id;
    m_pending_requests.push_back( r );
  }

  this->async_send();
}



} // namespace XXX
