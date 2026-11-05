/* GC Station sur PC : reseau de libogc (net_*) -> vrais sockets du PC */
#ifndef PCSIM_NETWORK_H
#define PCSIM_NETWORK_H
#include <gccore.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#define INADDR_ANY_PCSIM INADDR_ANY
s32 net_init(void); s32 net_init_async(void *cb, void *usr); void net_deinit(void);
s32 net_socket(u32 domain, u32 type, u32 protocol); s32 net_close(s32 s);
s32 net_connect(s32 s, struct sockaddr *a, socklen_t l); s32 net_bind(s32 s, struct sockaddr *a, socklen_t l);
s32 net_listen(s32 s, u32 backlog); s32 net_accept(s32 s, struct sockaddr *a, socklen_t *l);
s32 net_send(s32 s, const void *d, s32 l, u32 f); s32 net_recv(s32 s, void *d, s32 l, u32 f);
s32 net_sendto(s32 s, const void *d, s32 l, u32 f, struct sockaddr *a, socklen_t al);
s32 net_recvfrom(s32 s, void *d, s32 l, u32 f, struct sockaddr *a, socklen_t *al);
s32 net_write(s32 s, const void *d, s32 l); s32 net_read(s32 s, void *d, s32 l);
s32 net_setsockopt(s32 s, u32 level, u32 opt, const void *v, socklen_t l);
s32 net_getsockopt(s32 s, u32 level, u32 opt, void *v, socklen_t *l);
s32 net_ioctl(s32 s, u32 cmd, void *arg); s32 net_fcntl(s32 s, u32 cmd, u32 flags);
s32 net_select(s32 maxfd, fd_set *r, fd_set *w, fd_set *e, struct timeval *t);
s32 net_poll(struct pollfd *f, u32 n, s32 t); s32 net_shutdown(s32 s, u32 how);
struct hostent *net_gethostbyname(const char *n);
u32 net_gethostip(void);
s32 if_config(char *ip, char *mask, char *gw, bool dhcp, int retries);
s32 if_configex(struct in_addr *ip, struct in_addr *mask, struct in_addr *gw, bool dhcp, int retries);
#endif
