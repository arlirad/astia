#pragma once
#ifdef __cplusplus
extern "C" {
#endif
enum { AF_UNIX=1, AF_INET=2, SOCK_STREAM=1, SOCK_DGRAM=2, SOL_SOCKET=1,
       SHUT_RD=0, SHUT_WR=1, SHUT_RDWR=2 };
struct sockaddr { unsigned short sa_family; char sa_data[14]; };
int socket(int,int,int); int bind(int,const struct sockaddr*,unsigned int);
int listen(int,int); int accept(int,struct sockaddr*,unsigned int*);
int connect(int,const struct sockaddr*,unsigned int);
int socketpair(int,int,int,int[2]);
int shutdown(int,int);
long send(int,const void*,unsigned long,int);
long recv(int,void*,unsigned long,int);
long sendto(int,const void*,unsigned long,int,const struct sockaddr*,unsigned int);
long recvfrom(int,void*,unsigned long,int,struct sockaddr*,unsigned int*);
#ifdef __cplusplus
}
#endif
