#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "buf.h"

struct miniredis_conn;

void* miniredis_conn_udata(struct miniredis_conn* conn);
void miniredis_conn_set_udata(struct miniredis_conn* conn, void* udata);
void miniredis_conn_close(struct miniredis_conn* conn);
const char* miniredis_conn_addr(struct miniredis_conn* conn);
void miniredis_conn_write_raw(struct miniredis_conn* conn, const void* data,
                              ssize_t len);
void miniredis_conn_write_array(struct miniredis_conn* conn, int count);
void miniredis_conn_write_string(struct miniredis_conn* conn, const char* str);
void miniredis_conn_write_error(struct miniredis_conn* conn, const char* err);
void miniredis_conn_write_uint(struct miniredis_conn* conn, uint64_t value);
void miniredis_conn_write_int(struct miniredis_conn* conn, int64_t value);
void miniredis_conn_write_bulk(struct miniredis_conn* conn, const void* data,
                               ssize_t len);
void miniredis_conn_write_null(struct miniredis_conn* conn);

struct miniredis_args;

const char* miniredis_args_at(struct miniredis_args* args, int index,
                              size_t* len);
int miniredis_args_count(struct miniredis_args* args);
bool miniredis_args_eq(struct miniredis_args* args, int index, const char* cmd);

struct miniredis_events {
  int64_t (*tick)(void* udata);
  bool (*sync)(void* udata);
  void (*command)(struct miniredis_conn* conn, struct miniredis_args* args,
                  void* udata);
  void (*opened)(struct miniredis_conn* conn, void* udata);
  void (*closed)(struct miniredis_conn* conn, void* udata);
  void (*serving)(const char** addrs, int naddrs, void* udata);
  void (*error)(const char* message, bool fatal, void* udata);
};

void miniredis_main(const char** addrs, int naddrs,
                    struct miniredis_events events, void* udata);
void miniredis_main_mt(const char** addrs, int naddrs,
                       struct miniredis_events events, void* udata,
                       int nthreads);

// general purpose resp message writing

bool miniredis_write_array(struct buf* buf, int count);
bool miniredis_write_string(struct buf* buf, const char* str);
bool miniredis_write_error(struct buf* buf, const char* err);
bool miniredis_write_uint(struct buf* buf, uint64_t value);
bool miniredis_write_int(struct buf* buf, int64_t value);
bool miniredis_write_bulk(struct buf* buf, const void* data, ssize_t len);
bool miniredis_write_null(struct buf* buf);

int64_t miniredis_now();
