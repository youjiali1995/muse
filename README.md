# muse

基于I/O多路复用epoll的并发http服务器。

## 用法
```bash
$ make
$ ./muse --help
Usage: muse [OPTION]
    --stop      Stop muse.
    --restart   Restart muse and reload config.json.
    --help      Print usage.
```
需要使用`root`权限运行，使用`SIGINT`或`muse --stop`停止。

## 配置

使用`config.json`作为配置文件，利用[json-c(自己写的)](https://github.com/zhaolei1023/json-c)解析，支持运行时更新配置。

支持配置如下：

* port:     监听端口号
* daemon:   设置守护进程
* worker:   设置并发进程数
* timeout:  超时时间
* src_root: 资源根目录
* err_root: 错误响应根目录

## 并发

采用多进程 + `epoll`实现并发，每个进程有一个事件循环。利用`SO_REUSEPORT`由内核实现简单的负载均衡。

## 解析

利用**状态机**按行(`\r\n`)实现解析，一趟解析行，一趟解析报文，时间复杂度O(2n)。

## 连接管理

使用时间堆(最小堆)管理连接，接受连接时会插入堆中，连接关闭会移出堆，超时连接会从堆顶移出。默认为长连接，请求不成功会关闭连接。

## 发送响应

使用`sendfile`并设置`TCP_CORK`发送文件。

## benchmark
![image](https://github.com/zhaolei1023/muse/blob/master/www/benchmark.png)

## TODO

1. 动态内容：CGI、FastCGI、uWSGI等
2. 目前只支持GET方法，其余方法也会解析，只是最后响应为`501 Not Implement`。
3. 内存池
