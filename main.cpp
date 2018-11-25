#include <iostream>
#include "kcp/ikcp.h"
#include <sys/poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <vector>
#include <unistd.h>
#include <arpa/inet.h>
#include <queue>
#include <memory>

using namespace std;


#define log_info(fmt, ...) printf(fmt" - %s:%d\n", ## __VA_ARGS__, __FILE__, __LINE__)
//#define log_info(fmt, ...)
#define log_error log_info

struct Session
{
    Session()
        : tcpSock(-1)
        , udpSock(-1)
    {
    }

    uint32_t id;
    int tcpSock;
    int udpSock;
    queue<string> udpPackets;
};

typedef std::shared_ptr<Session> SessionPtr;

vector<SessionPtr> g_sessions;
vector<pollfd> g_vecPfd;

int g_tcpSvr = -1;
int g_udpSock = -1;




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


void setFdEvts(vector<pollfd>& fds, int fd, short evts)
{
    for(size_t i = 0; i < fds.size(); i++)
    {
        if(fds[i].fd == fd)
        {
            fds[i].events = evts;
            return;
        }
    }
}


void initTcpServer()
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    assert(0 == fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK));

    int enable = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

    sockaddr_in local;
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = htons(6666);
    assert(0 == bind(s, (sockaddr*)&local, sizeof(local)));
    assert(0 == listen(s, 128));

    addFd(g_vecPfd, s, POLLIN);

    g_tcpSvr = s;

    log_info("tcpsvr: %d", g_tcpSvr);
}

void initUdpSock()
{
    g_udpSock = socket(AF_INET, SOCK_DGRAM, 0);
    assert(0 == fcntl(g_udpSock, F_SETFL, fcntl(g_udpSock, F_GETFL) | O_NONBLOCK));
    sockaddr_in udpRemote;
    udpRemote.sin_family = AF_INET;
    udpRemote.sin_addr.s_addr = inet_addr("127.0.0.1");
    udpRemote.sin_port = htons(6666);
    assert(0 == connect(g_udpSock, (sockaddr*)&udpRemote, sizeof(udpRemote)));

    addFd(g_vecPfd, g_udpSock, POLLIN);
}


vector<SessionPtr>::iterator findSessionByTcpSock(int sock)
{
    assert(sock != g_udpSock);
    for(auto it = g_sessions.begin(); it != g_sessions.end(); ++it)
    {
        if((*it)->tcpSock == sock)
        {
            return it;
        }
    }

    return g_sessions.end();
}

vector<SessionPtr>::iterator findSessionById(uint32_t id)
{
    for(auto it = g_sessions.begin(); it != g_sessions.end(); ++it)
    {
        if((*it)->id == id)
        {
            return it;
        }
    }

    return g_sessions.end();
}

void delSession(int fd)
{
    auto it = findSessionByTcpSock(fd);
    assert(it != g_sessions.end());
    g_sessions.erase(it);
}

void onAccept(size_t i)
{
    if(g_vecPfd[i].revents & (POLLHUP|POLLERR))
    {
        assert(0);
    }

    assert(g_vecPfd[i].revents & POLLIN);

    sockaddr_in remote;
    socklen_t len = sizeof(remote);
    int c = accept(g_tcpSvr, (sockaddr*)&remote, &len);
    assert(c >= 0);

    fcntl(c, F_SETFL, fcntl(c, F_GETFL) | O_NONBLOCK);
    addFd(g_vecPfd, c, POLLIN);

    static uint32_t g_sid = 0;
    SessionPtr sess = make_shared<Session>();

    sess->id = g_sid ++;
    sess->tcpSock = c;
    sess->udpSock = g_udpSock;
    g_sessions.push_back(sess);

    log_info("accept a connection : %d", sess->tcpSock);
}

void handleEvent(size_t i)
{
    if(g_vecPfd[i].fd == g_tcpSvr)
    {
        onAccept(i);
        return;
    }


    //handle exception
    if(g_vecPfd[i].revents & (POLLHUP|POLLERR))
    {
        log_info("session exception at %d", g_vecPfd[i].fd);
        delSession(g_vecPfd[i].fd);
        close(g_vecPfd[i].fd);
        g_vecPfd[i].fd = -1;
        g_vecPfd[i].events = 0;
        g_vecPfd[i].revents = 0;
    }

    //handle tcp read and udp read
    if(g_vecPfd[i].revents & POLLIN)
    {
        vector<SessionPtr>::iterator it;

        struct {
            uint32_t id;
            char data[2048];
        }buf;

        ssize_t bytes = 0;

        if(g_vecPfd[i].fd == g_udpSock)
        {
            bytes = read(g_vecPfd[i].fd, &buf, sizeof(buf));
            if(bytes < 4)
            {
                log_error("invalid packet from udpsock!");
                return;
            }

            uint32_t id = ntohl(buf.id);

            it = findSessionById(id);
            if(it == g_sessions.end())
            {
                log_error("invalid session id : %u!", id);
                return;
            }
        }
        else
        {
            it = findSessionByTcpSock(g_vecPfd[i].fd);
            assert(it != g_sessions.end());

            bytes = read(g_vecPfd[i].fd, buf.data, sizeof(buf.data));
        }

        if(bytes <= 0)
        {
            if(g_vecPfd[i].fd == (*it)->tcpSock)
            {
                g_sessions.erase(it);

                close(g_vecPfd[i].fd);
                g_vecPfd[i].fd = -1;
                g_vecPfd[i].events = 0;
                g_vecPfd[i].revents = 0;
            }
            else
            {
                log_info("read from udp socket failed?");
            }
        }
        else
        {
            if (g_vecPfd[i].fd == (*it)->tcpSock)
            {
                log_info("recv from tcp %u bytes", (unsigned)bytes);
                buf.id = htonl((*it)->id);
                ssize_t udpBytes = 0;
                do
                {
                    udpBytes = write(g_udpSock, &buf, (size_t)bytes + 4);
                } while(udpBytes == 0 && errno == EAGAIN);

                if (udpBytes < 0)
                {
                    log_info("udp write failed, errno : %d", errno);
                    return;
                }
                log_info("%u: udp write %d bytes", (*it)->id, (int)udpBytes);
            }
            else
            {
                log_info("recv from udp %u bytes", (unsigned)bytes);
                assert(g_vecPfd[i].fd == (*it)->udpSock);
                if((*it)->udpPackets.empty())
                {
                    setFdEvts(g_vecPfd, (*it)->tcpSock, POLLIN|POLLOUT);
                }
                (*it)->udpPackets.push(string(buf.data, bytes));
            }
        }
    }

    //handle tcp socket write
    if(g_vecPfd[i].revents & POLLOUT)
    {
        assert(g_vecPfd[i].fd != g_udpSock);

        auto it = findSessionByTcpSock(g_vecPfd[i].fd);
        SessionPtr& sess = *it;

        string& pack = sess->udpPackets.front();
        ssize_t tcpBytes = write(g_vecPfd[i].fd, pack.c_str(), pack.size());
        if(tcpBytes < 0)
        {
            log_info("write tcp socket failed, remove tcp socket %d from session", g_vecPfd[i].fd);
            g_sessions.erase(it);

            close(g_vecPfd[i].fd);
            g_vecPfd[i].fd = -1;
            g_vecPfd[i].events = 0;
            g_vecPfd[i].revents = 0;
        }
        else
        {
            log_info("write to tcp %u bytes", (unsigned)tcpBytes);
            if(tcpBytes == pack.size())
            {
                sess->udpPackets.pop();
                if(sess->udpPackets.empty())
                {
                    g_vecPfd[i].events = POLLIN;
                }
            }
            else
            {
                pack.erase(0, (size_t)tcpBytes);
            }
        }
    }
}

bool runTcpServer()
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


        handleEvent(i);
    }

    return true;
}

void runProxyClient()
{
    initTcpServer();
    initUdpSock();


    while(1)
    {
        if(!runTcpServer())
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