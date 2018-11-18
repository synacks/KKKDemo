#include <iostream>
#include "kcp/ikcp.h"
#include <sys/poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <vector>
#include <unistd.h>
#include <arpa/inet.h>

using namespace std;

struct Session
{
    Session()
        : tcpSock(-1)
        , udpSock(-1)
    {}

    int tcpSock;
    int udpSock;
};

vector<Session> g_sessions;

vector<pollfd> g_vecPfd;

void addFd(vector<pollfd>& fds, int fd, short evts)
{
    pollfd pfd;
    pfd.fd = fd;
    pfd.events =evts;
    pfd.revents = 0;

    for(size_t i = 0; i < fds.size(); i++)
    {
        if(fds[i].fd == -1)
        {
            fds[i] = pfd;
            return;
        }
    }

    fds.push_back(pfd);
}

vector<Session>::iterator findSessionBySock(int sock)
{
    for(auto it = g_sessions.begin(); it != g_sessions.end(); ++it)
    {
        if(it->tcpSock == sock || it->udpSock == sock)
        {
            return it;
        }
    }

    return g_sessions.end();
}

void delSession(int fd)
{
    auto it = findSessionBySock(fd);
    assert(it != g_sessions.end());
    g_sessions.erase(it);
}

void handleEvent(int s, size_t i)
{
    if(g_vecPfd[i].revents & (POLLHUP|POLLERR))
    {
        delSession(g_vecPfd[i].fd);
        close(g_vecPfd[i].fd);
        g_vecPfd[i].fd = -1;
        g_vecPfd[i].events = 0;
        g_vecPfd[i].revents = 0;
    }
    else if(g_vecPfd[i].revents & POLLIN)
    {
        if(g_vecPfd[i].fd == s)
        {
            sockaddr_in remote;
            socklen_t len = sizeof(remote);
            int c = accept(s, (sockaddr*)&remote, &len);
            if(c < 0)
            {
                printf("accept failed, errno : %d\n", errno);
                assert(0);
            }

            fcntl(c, F_SETFL, fcntl(c, F_GETFL) | O_NONBLOCK);
            addFd(g_vecPfd, c, POLLIN);

            Session sess;
            sess.tcpSock = c;
            sess.udpSock = socket(AF_INET, SOCK_DGRAM, 0);
            fcntl(sess.udpSock, F_SETFL, fcntl(sess.udpSock, F_GETFL) | O_NONBLOCK);
            assert(sess.udpSock);
            sockaddr_in udpRemote;
            udpRemote.sin_family = AF_INET;
            udpRemote.sin_addr.s_addr = inet_addr("127.0.0.1");
            udpRemote.sin_port = htons(6666);
            assert(0 == connect(sess.udpSock, (sockaddr*)&udpRemote, sizeof(udpRemote)));
            g_sessions.push_back(sess);
            addFd(g_vecPfd, sess.udpSock, POLLIN);
        }
        else
        {
            auto it = findSessionBySock(g_vecPfd[i].fd);
            assert(it != g_sessions.end());

            char buf[1400] = {0};
            ssize_t bytes = read(g_vecPfd[i].fd, buf, sizeof(buf));
            if(bytes <= 0)
            {
                g_sessions.erase(it);

                close(g_vecPfd[i].fd);
                g_vecPfd[i].fd = -1;
                g_vecPfd[i].events = 0;
                g_vecPfd[i].revents = 0;
            }
            else
            {
                if(g_vecPfd[i].fd == it->tcpSock)
                {
                    ssize_t udpBytes = write(it->udpSock, buf, (size_t)bytes);
                    if(udpBytes <= 0)
                    {
                        printf("udp write failed, errno : %d\n", errno);
                        return;
                    }
                    printf("udp write %d bytes\n", (int)udpBytes);
                }
                else
                {
                    ssize_t tcpBytes = write(it->tcpSock, buf, (size_t) bytes);
                    if (tcpBytes <= 0)
                    {
                        printf("tcp write failed, errno : %d\n", errno);
                        return;
                    }
                    printf("tcp write %d bytes\n", (int)tcpBytes);
                }
            }
        }
    }
}

bool runTcpServer(int s)
{
    int ret = poll(&*g_vecPfd.begin(), g_vecPfd.size(), -1);
    if(ret < 0)
    {
        return false;
    }

    int count = 0;
    for(size_t i = 0; i < g_vecPfd.size(); i++)
    {
        if(count == ret)
        {
            break;
        }

        if(g_vecPfd[i].revents == 0 || g_vecPfd[i].fd < 0)
        {
            continue;
        }

        count++;


        handleEvent(s, i);
    }

    return true;
}

void runProxyClient()
{
    int s = socket(AF_INET, SOCK_STREAM, 0);

    int enable = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

    sockaddr_in local;
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = htons(6666);
    assert(0 == bind(s, (sockaddr*)&local, sizeof(local)));

    assert(0 == listen(s, 128));

    assert(0 == fcntl(s, F_SETFL, fcntl(s, F_GETFL) & O_NONBLOCK));
    pollfd spfd;
    spfd.fd = s;
    spfd.events = POLLIN;

    g_vecPfd.push_back(spfd);

    while(1)
    {
        if(!runTcpServer(s))
        {
            break;
        }
    }
}

void udpServerHandleEvent(int s, size_t i)
{
    if(g_vecPfd[i].revents & (POLLHUP|POLLERR))
    {
        delSession(g_vecPfd[i].fd);
        close(g_vecPfd[i].fd);
        g_vecPfd[i].fd = -1;
        g_vecPfd[i].events = 0;
        g_vecPfd[i].revents = 0;
    }
    else if(g_vecPfd[i].revents & POLLIN)
    {
        if(g_vecPfd[i].fd == s)
        {

            sockaddr_in remote;
            socklen_t len = sizeof(remote);
            int c = accept(s, (sockaddr*)&remote, &len);
            if(c < 0)
            {
                printf("accept failed, errno : %d\n", errno);
                assert(0);
            }

            fcntl(c, F_SETFL, fcntl(c, F_GETFL) | O_NONBLOCK);
            addFd(g_vecPfd, c, POLLIN);

            Session sess;
            sess.tcpSock = c;
            sess.udpSock = socket(AF_INET, SOCK_DGRAM, 0);
            fcntl(sess.udpSock, F_SETFL, fcntl(sess.udpSock, F_GETFL) | O_NONBLOCK);
            assert(sess.udpSock);
            sockaddr_in udpRemote;
            udpRemote.sin_family = AF_INET;
            udpRemote.sin_addr.s_addr = inet_addr("127.0.0.1");
            udpRemote.sin_port = htons(6666);
            assert(0 == connect(sess.udpSock, (sockaddr*)&udpRemote, sizeof(udpRemote)));
            g_sessions.push_back(sess);
            addFd(g_vecPfd, sess.udpSock, POLLIN);
        }
        else
        {
            auto it = findSessionBySock(g_vecPfd[i].fd);
            assert(it != g_sessions.end());

            char buf[1400] = {0};
            ssize_t bytes = read(g_vecPfd[i].fd, buf, sizeof(buf));
            if(bytes <= 0)
            {
                g_sessions.erase(it);

                close(g_vecPfd[i].fd);
                g_vecPfd[i].fd = -1;
                g_vecPfd[i].events = 0;
                g_vecPfd[i].revents = 0;
            }
            else
            {
                if(g_vecPfd[i].fd == it->tcpSock)
                {
                    ssize_t udpBytes = write(it->udpSock, buf, (size_t)bytes);
                    if(udpBytes <= 0)
                    {
                        printf("udp write failed, errno : %d\n", errno);
                        return;
                    }
                    printf("udp write %d bytes\n", (int)udpBytes);
                }
                else
                {
                    ssize_t tcpBytes = write(it->tcpSock, buf, (size_t) bytes);
                    if (tcpBytes <= 0)
                    {
                        printf("tcp write failed, errno : %d\n", errno);
                        return;
                    }
                    printf("tcp write %d bytes\n", (int)tcpBytes);
                }
            }
        }
    }
}

bool runUdpServer(int s)
{
    int ret = poll(&*g_vecPfd.begin(), g_vecPfd.size(), -1);
    if(ret < 0)
    {
        return false;
    }

    int count = 0;
    for(size_t i = 0; i < g_vecPfd.size(); i++)
    {
        if(count == ret)
        {
            break;
        }

        if(g_vecPfd[i].revents == 0 || g_vecPfd[i].fd < 0)
        {
            continue;
        }

        count++;


        udpServerHandleEvent(s, i);
    }

    return true;
}

void runProxyServer()
{
    int udpServer = socket(AF_INET, SOCK_DGRAM, 0);

    int enable = 1;
    setsockopt(udpServer, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

    sockaddr_in local;
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = htons(6666);
    assert(0 == bind(udpServer, (sockaddr*)&local, sizeof(local)));
    assert(0 == fcntl(udpServer, F_SETFL, fcntl(udpServer, F_GETFL) & O_NONBLOCK));
    pollfd spfd;
    spfd.fd = udpServer;
    spfd.events = POLLIN;

    g_vecPfd.push_back(spfd);

    while(1)
    {
        if(!runUdpServer(udpServer))
        {
            break;
        }
    }
}



int main()
{
    runProxyClient();

    return 0;
}