#include "event.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "buf.h"
#include "hashmap.h"

#define EDELAYNS 1000000000

#define panic(format, ...)                             \
  {                                                    \
    fprintf(stderr, "panic: " format, ##__VA_ARGS__);  \
    fprintf(stderr, " (%s:%d)\n", __FILE__, __LINE__); \
    exit(1);                                           \
  }

#define eprintf(fatal, format, ...)                                        \
  {                                                                        \
    snprintf(event->errmsg, sizeof(event->errmsg), format, ##__VA_ARGS__); \
    if (event->events.error) {                                             \
      event->events.error(event->errmsg, fatal, event->udata);             \
    } else if (fatal) {                                                    \
      panic(format, ##__VA_ARGS__);                                        \
    }                                                                      \
    if (fatal) exit(1);                                                    \
  }

static bool wake(struct event_conn* conn);

static void set_fault(struct event_conn* conn) {
  conn->faulty = true;
  conn->next_faulty = conn->event->faulty;
  conn->event->faulty = conn;
}

void event_conn_write(struct event_conn* conn, const void* data, ssize_t len) {
  if (conn->faulty || conn->closed) {
    return;
  }
  if (!buf_append(&conn->wbuf, data, len) || !wake(conn)) {
    set_fault(conn);
  }
}

void event_conn_close(struct event_conn* conn) {
  if (conn->faulty || conn->closed) {
    return;
  }
  conn->closed = true;
  if (!wake(conn)) {
    set_fault(conn);
  }
}

void* event_conn_udata(struct event_conn* conn) { return conn->udata; }

void event_conn_set_udata(struct event_conn* conn, void* udata) {
  conn->udata = udata;
}

static int net_queue() { return epoll_create1(0); }

static int net_addrd(int qfd, int sfd) {
  struct epoll_event ev = {0};
  ev.events = EPOLLIN;
  ev.data.fd = sfd;
  return epoll_ctl(qfd, EPOLL_CTL_ADD, sfd, &ev);
}

static int net_addwr(int qfd, int sfd) {
  struct epoll_event ev = {0};
  ev.events = EPOLLIN | EPOLLOUT;
  ev.data.fd = sfd;
  return epoll_ctl(qfd, EPOLL_CTL_MOD, sfd, &ev);
}

static int net_delwr(int qfd, int sfd) {
  struct epoll_event ev = {0};
  ev.events = EPOLLIN;
  ev.data.fd = sfd;
  return epoll_ctl(qfd, EPOLL_CTL_MOD, sfd, &ev);
}

static int net_events(int qfd, int* fds, int nfds, int64_t timeout_ns) {
  struct epoll_event evs[nfds];  // VLA
  int n;
  if (timeout_ns < 0) {
    n = epoll_wait(qfd, evs, nfds, -1);
  } else {
    if (timeout_ns > EDELAYNS) {
      timeout_ns = EDELAYNS;
    }
    n = epoll_wait(qfd, evs, nfds, (int)(timeout_ns / 1000000));
  }
  if (n > 0) {
    for (int i = 0; i < n; i++) {
      fds[i] = evs[i].data.fd;
    }
  }
  return n;
}

int setkeepalive(int fd) {
  if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &(int){1}, sizeof(int))) {
    return -1;
  }

  // tcp_keepalive_time
  if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &(int){600}, sizeof(int))) {
    return -1;
  }
  // tcp_keepalive_intvl
  if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &(int){60}, sizeof(int))) {
    return -1;
  }
  // tcp_keepalive_probes
  if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &(int){6}, sizeof(int))) {
    return -1;
  }

  return 0;
}

// ban nagle
// can send the next packet only after receive the last ack
int settcpnodelay(int fd) {
  return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &(int){1}, sizeof(int));
}

static int setnonblock(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    return -1;
  }
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void ipstr(const struct sockaddr* sa, char* s, size_t len) {
  switch (sa->sa_family) {
    case AF_INET:
      strcpy(s, "tcp://");
      inet_ntop(AF_INET, &(((struct sockaddr_in*)sa)->sin_addr), s + 6,
                len - 6);
      break;
    case AF_INET6:
      strcpy(s, "tcp://[");
      inet_ntop(AF_INET6, &(((struct sockaddr_in6*)sa)->sin6_addr), s + 7,
                len - 7);
      strcat(s, "]");
      break;
    default:
      strncpy(s, "Unknown AF", len);
      return;
  }
}

const char* event_conn_addr(struct event_conn* conn) { return conn->addr; }

static void net_accept(struct event* event, int qfd, int sfd) {
  struct event_conn* conn = NULL;
  int cfd = -1;
  struct sockaddr_storage addr;
  socklen_t addrlen = sizeof(addr);
  cfd = accept(sfd, (struct sockaddr*)&addr, &addrlen);
  if (cfd < 0) goto fail;
  if (setnonblock(cfd) == -1) goto fail;
  if (setkeepalive(cfd) == -1) goto fail;
  if (net_addrd(qfd, cfd) == -1) goto fail;
  conn = malloc(sizeof(struct event_conn));
  if (!conn) goto fail;
  memset(conn, 0, sizeof(struct event_conn));
  char saddr[256];

  ipstr((struct sockaddr*)&addr, saddr, sizeof(saddr) - 1);
  sprintf(saddr + strlen(saddr), ":%d", ((struct sockaddr_in*)&addr)->sin_port);

  size_t saddrlen = strlen(saddr);
  conn->addr = malloc(saddrlen + 1);
  if (!conn->addr) goto fail;
  memcpy(conn->addr, saddr, saddrlen + 1);
  conn->fd = cfd;
  conn->qfd = qfd;
  conn->event = event;
  if (hashmap_set(event->conns, &conn)) {
    panic("duplicate fd");
  } else if (hashmap_oom(event->conns)) {
    goto fail;
  }

  return;
fail:
  if (cfd != -1) close(cfd);
  if (conn) {
    if (conn->addr) free(conn->addr);
    free(conn);
  }
}

static struct addr* addr_listen(struct event* event, const char* str) {
  const char* host = str;
  if (strstr(str, "tcp://") == str) {
    host = str + 6;
  } else if (strstr(str, "://")) {
    eprintf(true, "Invalid address: %s", str);
  }
  char* colon = NULL;
  for (int i = strlen(host) - 1; i >= 0; i--) {
    if (host[i] == ':') {
      colon = (char*)host + i;
      break;
    }
  }

  int port = 0;
  int hlen = strlen(host);
  if (colon) {
    char* end = NULL;
    long x = strtol(colon + 1, &end, 10);
    if (!end || *end || x > 0xFFFF || x < 0) {
      eprintf(true, "Invalid address: %s", str);
    }
    port = x;
    hlen = colon - host;
    if (host[0] == '[' && host[hlen - 1] == ']') {
      host++;
      hlen -= 2;
    }
  }
  // Address string looks valid so let's try to bind.
  struct addr* addr = malloc(sizeof(struct addr));
  if (!addr) {
    eprintf(true, "%s", strerror(ENOMEM));
  }
  memset(addr, 0, sizeof(struct addr));
  addr->nfds = 0;
  addr->fds = NULL;
  addr->port = port;
  addr->host = malloc(hlen + 1);
  if (!addr->host) {
    eprintf(true, "%s", strerror(ENOMEM));
  }
  memcpy(addr->host, host, hlen);
  addr->host[hlen] = 0;

  struct addrinfo hints = {0}, *addrs;
  char port_str[16] = "";

  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  sprintf(port_str, "%d", port);
  int err = getaddrinfo(addr->host, port_str, &hints, &addrs);
  if (err != 0) {
    eprintf(true, "getaddrinfo: %s: %s", gai_strerror(err), str);
  }
  int n = 0;
  struct addrinfo* addrinfo = addrs;
  while (addrinfo) {
    n++;
    addrinfo = addrinfo->ai_next;
  }
  addr->fds = malloc(n * sizeof(int));
  if (!addr->fds) {
    eprintf(true, "%s", strerror(ENOMEM));
  }
  addr->addrs = malloc(n * sizeof(char*));
  if (!addr->addrs) {
    eprintf(true, "%s", strerror(ENOMEM));
  }
  addrinfo = addrs;
  char errmsg[256] = "";
#define emsg_continue(msg)                                                     \
  {                                                                            \
    if (fd != -1) close(fd);                                                   \
    snprintf(errmsg, sizeof(errmsg), "%s: %s: %s", msg, strerror(errno), str); \
    continue;                                                                  \
  }
  char saddr[256];
  for (; addrinfo; addrinfo = addrinfo->ai_next) {
    int fd = socket(addrinfo->ai_family, addrinfo->ai_socktype,
                    addrinfo->ai_protocol);
    if (fd == -1) {
      emsg_continue("socket");
    }
    // TIME_WAIT
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) ==
        -1) {
      emsg_continue("setsockopt(SO_REUSEADDR)");
    }
    // non blocking io
    if (setnonblock(fd) == -1) {
      emsg_continue("setnonblock");
    }
    if (bind(fd, addrinfo->ai_addr, addrinfo->ai_addrlen) == -1) {
      emsg_continue("bind");
    }
    if (listen(fd, SOMAXCONN) == -1) {
      emsg_continue("listen");
    }
    err = 0;
    addr->fds[addr->nfds] = fd;
    ipstr(addrinfo->ai_addr, saddr, sizeof(saddr) - 1);
    sprintf(saddr + strlen(saddr), ":%d", port);

    addr->addrs[addr->nfds] = malloc(strlen(saddr) + 1);
    if (!addr->addrs[addr->nfds]) {
      eprintf(true, "%s", strerror(ENOMEM));
    }
    strcpy(addr->addrs[addr->nfds], saddr);
    addr->nfds++;
  }
  if (err) {
    eprintf(true, "%s", errmsg);
  }
  if (addr->nfds == 0) {
    if (strlen(errmsg)) {
      eprintf(true, "%s", errmsg);
    } else {
      eprintf(true, "Address fail: %s", str);
    }
  }
  freeaddrinfo(addrs);

  return addr;
}

static void close_remove_conn(struct event_conn* conn, struct event* event) {
  buf_clear(&conn->wbuf);
  close(conn->fd);
  hashmap_delete(event->conns, &conn);
  free(conn->addr);
  free(conn);
}

static int conn_compare(const void* a, const void* b) {
  struct event_conn* ca = *(struct event_conn**)a;
  struct event_conn* cb = *(struct event_conn**)b;
  return ca->fd < cb->fd ? -1 : ca->fd > cb->fd ? 1 : 0;
}

static uint64_t conn_hash(const void* item) {
  return (*(struct event_conn**)item)->fd;
}

static bool wake(struct event_conn* conn) {
  if (!conn->woke) {
    if (net_addwr(conn->qfd, conn->fd) == -1) {
      return false;
    }
    conn->woke = true;
  }
  return true;
}

static bool unwake(struct event_conn* conn) {
  if (conn->woke) {
    if (net_delwr(conn->qfd, conn->fd) == -1) {
      return false;
    }
    conn->woke = false;
  }
  return true;
}

static bool conn_flush(struct event* event, struct event_conn* conn) {
  if (conn->wbuf.len > 0) {
    for (size_t i = conn->wbuf_idx; i < conn->wbuf.len;) {
      int n = write(conn->fd, conn->wbuf.data + i, conn->wbuf.len - i);
      if (n == -1) {
        if (errno == EAGAIN) {
          if (!wake(conn)) {
            close_remove_conn(conn, event);
          } else {
            conn->wbuf_idx = i;
          }
        } else {
          close_remove_conn(conn, event);
        }
        return false;
      }
      i += n;
    }
    conn->wbuf.len = 0;
    conn->wbuf_idx = 0;
  }
  if (conn->closed) {
    close_remove_conn(conn, event);
    return false;
  }
  if (!unwake(conn)) {
    close_remove_conn(conn, event);
    return false;
  }
  return true;
}

int64_t event_now() {
  struct timespec tm;
  if (clock_gettime(CLOCK_MONOTONIC, &tm) == -1) {
    panic("clock_gettime: %s", strerror(errno));
  }
  return tm.tv_sec * 1000000000 + tm.tv_nsec;
}

static int which_socketfd(int fd, struct addr** addrs, int naddrs) {
  for (int j = 0; j < naddrs; j++) {
    for (int k = 0; k < addrs[j]->nfds; k++) {
      if (fd == addrs[j]->fds[k]) {
        return j;
      }
    }
  }
  return -1;
}

struct event_conn* get_conn(struct event* event, int fd) {
  struct event_conn key = {.fd = fd};
  struct event_conn* keyptr = &key;
  void* v = hashmap_get(event->conns, &keyptr);
  if (!v) {
    return NULL;
  }
  return *(struct event_conn**)v;
}

struct thread_context {
  bool serving;
  int server_id;
  struct event_events events;
  void* udata;
  struct addr** paddrs;
  int naddrs;
};

static void* thread(void* thdata) {
  struct thread_context* thctx = thdata;
  struct event _event;
  struct event* event = &_event;
  memset(event, 0, sizeof(struct event));
  event->events = thctx->events;
  event->udata = thctx->udata;
  event->conns = hashmap_new(sizeof(struct conn*), 0, conn_hash, conn_compare);
  if (!event->conns) {
    eprintf(true, "%s", strerror(ENOMEM));
  }
  int qfd = net_queue();
  if (qfd == -1) {
    eprintf(true, "net_queue: %s", strerror(errno));
  }
  // add all socket fds to queue
  int naddrsfds = 0;
  for (int i = 0; i < thctx->naddrs; i++) {
    for (int j = 0; j < thctx->paddrs[i]->nfds; j++) {
      int sfd = thctx->paddrs[i]->fds[j];
      if (net_addrd(qfd, sfd) == -1) {
        eprintf(true, "net_addrd(socket): %s", strerror(errno));
      }
      naddrsfds++;
    }
  }

  int64_t tick_delay = -1;
  thctx->server_id++;

  if (!thctx->serving) {
    thctx->serving = true;
    if (event->events.serving) {
      char** saddrs = malloc(naddrsfds * sizeof(char*));
      if (!saddrs) {
        eprintf(true, "%s", strerror(ENOMEM));
      }
      int k = 0;
      for (int i = 0; i < thctx->naddrs; i++) {
        for (int j = 0; j < thctx->paddrs[i]->nfds; j++) {
          saddrs[k++] = thctx->paddrs[i]->addrs[j];
        }
      }
      event->events.serving((const char**)saddrs, naddrsfds, event->udata);
    }
  }

  bool synced = false;
  char buffer[4096];
  int fds[128];

  for (;;) {
    int64_t delay = synced ? tick_delay : 0;
    int n = net_events(qfd, fds, sizeof(fds) / sizeof(int), delay);
    if (n == -1) {
      panic("net_events: %s", strerror(errno));
    }

    if (event->faulty) {
      // close faulty connections
      while (event->faulty) {
        close_remove_conn(event->faulty, event);
        event->faulty = event->faulty->next_faulty;
      }
      event->faulty = false;
      continue;
    }

    for (int i = 0; i < n; i++) {
      // not a connection, check if it's a server socket.
      int j = which_socketfd(fds[i], thctx->paddrs, thctx->naddrs);
      if (j != -1) {
        // accept the incoming connection.
        net_accept(event, qfd, fds[i]);
        continue;
      }
      struct event_conn* conn = get_conn(event, fds[i]);
      if (!conn) {
        continue;
      }
      if (!conn_flush(event, conn)) {
        continue;
      }
      while (true) {
        int n = read(conn->fd, buffer, sizeof(buffer) - 1);
        if (n <= 0) {
          if (n != -1 || errno != EAGAIN) {
            close_remove_conn(conn, event);
          }
          break;
        }
        buffer[n] = '\0';
        if (event->events.data) {
          conn->woke = true;
          event->events.data(conn, buffer, n, event->udata);
          conn->woke = false;
        }
      }
    }

    for (int i = 0; i < n; i++) {
      if (which_socketfd(fds[i], thctx->paddrs, thctx->naddrs) != -1) {
        continue;
      }
      struct event_conn* conn = get_conn(event, fds[i]);
      if (!conn) {
        continue;
      }
      if (!conn_flush(event, conn)) {
        continue;
      }
      if (conn->wbuf.cap > 4096) {
        free(conn->wbuf.data);
        conn->wbuf.data = NULL;
        conn->wbuf.cap = 0;
      }
    }
  }
  return NULL;
}

void event_main(const char* addrs[], int naddrs, struct event_events events,
                void* udata) {
  // create local event for the purpose of error logging only.
  struct event _event;
  struct event* event = &_event;
  memset(event, 0, sizeof(struct event));
  event->events = events;
  event->udata = udata;
  struct addr** paddrs = malloc(naddrs * sizeof(struct addr*));
  if (!paddrs) {
    eprintf(true, "%s", strerror(ENOMEM));
  }
  memset(paddrs, 0, naddrs * sizeof(struct addr*));
  for (int i = 0; i < naddrs; i++) {
    paddrs[i] = addr_listen(event, addrs[i]);
  }
  struct thread_context thctx;
  memset(&thctx, 0, sizeof(struct thread_context));
  thctx.serving = false;
  thctx.events = events;
  thctx.udata = udata;
  thctx.paddrs = paddrs;
  thctx.naddrs = naddrs;

  thread(&thctx);

  while (1) {
    sleep(10);
  }
}
