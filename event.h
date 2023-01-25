#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "buf.h"

struct addr {
  char* host;
  int port;
  int nfds;
  int* fds;
  char** addrs;
};

struct event_conn;

struct event_events {
  void (*data)(struct event_conn* conn, const void* data, size_t len,
               void* udata);
  void (*serving)(const char** addrs, int naddrs, void* udata);
  void (*error)(const char* message, bool fatal, void* udata);
};

struct event {
  struct event_events events;
  char errmsg[256];
  struct hashmap* conns;
  struct event_conn* faulty;
  void* udata;
};

struct event_conn {
  int qfd;
  int fd;
  bool closed;
  bool woke;
  bool faulty;
  struct buf wbuf;
  size_t wbuf_idx;
  void* udata;
  struct event* event;
  char* addr;
  struct event_conn* next_faulty;
};

void event_conn_close(struct event_conn* conn);
void* event_conn_udata(struct event_conn* conn);
void event_conn_set_udata(struct event_conn* conn, void* udata);
void event_conn_write(struct event_conn* conn, const void* data, ssize_t len);
const char* event_conn_addr(struct event_conn* conn);
int64_t event_now();
void event_main(const char* addrs[], int naddrs, struct event_events events,
                void* udata);
