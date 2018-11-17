#include <iostream>
#include "kcp/ikcp.h"
#include <sys/poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <vector>
#include <unistd.h>

using namespace std;


vector<pollfd> vecFd;

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

int main()
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

    vecFd.push_back(spfd);

    while(1)
    {
        int ret = poll(&*vecFd.begin(), vecFd.size(), -1);
        if(ret < 0)
        {
            break;
        }

        int count = 0;
        for(size_t i = 0; i < vecFd.size(); i++)
        {
            if(count == ret)
            {
                break;
            }

            if(vecFd[i].revents == 0 || vecFd[i].fd < 0)
            {
                continue;
            }

            count++;


            if(vecFd[i].revents & (POLLHUP|POLLERR))
            {
                close(vecFd[i].fd);
                vecFd[i].fd = -1;
                vecFd[i].events = 0;
                vecFd[i].revents = 0;
            }

            if(vecFd[i].revents | POLLIN)
            {
                if(vecFd[i].fd == s)
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
                    addFd(vecFd, c, POLLIN);
                }
                else
                {
                    char buf[4096] = {0};
                    ssize_t bytes = read(vecFd[i].fd, buf, sizeof(buf));
                    if(bytes <= 0)
                    {
                        close(vecFd[i].fd);
                        vecFd[i].fd = -1;
                        vecFd[i].events = 0;
                        vecFd[i].revents = 0;
                    }
                    else
                    {
                        printf("recv: %s\n", buf);
                        write(vecFd[i].fd, buf, (size_t)bytes);
                    }
                }
            }
        }
    }


    return 0;
}