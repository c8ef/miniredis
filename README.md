## miniredis

A redis-compatible server.

Support `SET`, `GET`, `PING`, `DEL`, `TTL`, `KEYS`, `DBSIZE`, `FLUSHDB`.

### dependency

```bash
sudo apt install libxxhash-dev
```

### build

```bash
gcc *.c -O3 -march=native -lxxhash -o server
```

### demo

```bash
$ redis-cli -p 6382
127.0.0.1:6382> set a 1
OK
127.0.0.1:6382> get a
"1"
127.0.0.1:6382> set b "redis"
OK
127.0.0.1:6382> get b
"redis"
127.0.0.1:6382> ping
PONG
```

### benchmark

- miniredis

```bash
$ redis-benchmark -q -t set,get,ping -r 1000000 -n 10000000 -p 6382 -P 16
WARNING: Could not fetch server CONFIG
PING_INLINE: 1648804.62 requests per second, p50=0.247 msec
PING_MBULK: 1600256.00 requests per second, p50=0.255 msec
SET: 1144950.75 requests per second, p50=0.575 msec
GET: 1069976.50 requests per second, p50=0.583 msec
```

- redis 7.0.7

```bash
$ redis-server --version
Redis server v=7.0.7 sha=00000000:0 malloc=jemalloc-5.2.1 bits=64 build=60db4852972c3375
$ redis-benchmark -q -t set,get,ping -r 1000000 -n 10000000 -p 6382 -P 16
PING_INLINE: 1397819.50 requests per second, p50=0.463 msec
PING_MBULK: 1845699.50 requests per second, p50=0.327 msec
SET: 632471.06 requests per second, p50=1.159 msec
GET: 742886.81 requests per second, p50=0.975 msec
```
