extern "C" {
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
//#include <netinet/in.h>
//#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <getopt.h>
}

#include <cstdio>
#include <cstring>
#include <cstdlib>

void assertErrno(bool cond, const char* message, int code)
{
    if (!cond)
    {
        syslog(LOG_MAKEPRI(LOG_DAEMON, LOG_ERR), "%s: %s\n", message, strerror(errno));
        exit(code);
    }
}

int main(int argc, char* argv[])
{
    signal(SIGPIPE, SIG_IGN);

    if (argc == 2)
    {
        char* numEnd;
        int port = strtoul(argv[1], &numEnd, 0);
        if ((port != 0) && (port <= 0xffff) && (*numEnd == '\0'))
        {
            assertErrno(daemon(0, 0) == 0, "Failed to daemonize", 7);

            int srvSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
            assertErrno(srvSocket >= 0, "Failed to create server socket", 2);

            sockaddr_in server = {0};
            server.sin_family = AF_INET;
            server.sin_addr.s_addr = htonl(INADDR_ANY);
            server.sin_port = htons(port);

            assertErrno(bind(srvSocket, (struct sockaddr*)&server, sizeof(server)) == 0, "Failed to bind server socket", 3);
            assertErrno(listen(srvSocket, SOMAXCONN) == 0, "Failed to start listening", 4);

            fd_set activeFdSet, readFdSet;
            FD_ZERO(&activeFdSet);
            FD_SET(srvSocket, &activeFdSet);

            while (true)
            {
                readFdSet = activeFdSet;
                assertErrno(select(FD_SETSIZE, &readFdSet, nullptr, nullptr, nullptr) > 0, "select()", 5);

                for (int recvSocket = 0; recvSocket < FD_SETSIZE; recvSocket++)
                {
                    if (FD_ISSET(recvSocket, &readFdSet))
                    {
                        if (recvSocket == srvSocket)
                        {
                            // client connected
                            int client = accept(srvSocket, nullptr, nullptr);
                            assertErrno(client >= 0, "accept()", 6);
                            if (client < FD_SETSIZE)
                                FD_SET(client, &activeFdSet);
                            else
                                close(client); // sorry :/
                        }
                        else
                        {
                            // data received from client
                            uint8_t buf[4096];
                            ssize_t recvd = recv(recvSocket, buf, sizeof(buf), 0);
                            if (recvd > 0)
                            {
                                // actually got data
                                // so let's relay it to the other clients
                                for (int other = 0; other < FD_SETSIZE; other++)
                                {
                                    if (FD_ISSET(other, &activeFdSet) && (other != srvSocket))
                                    {
                                        // fscking partial sends
                                        // I hate everything
                                        ssize_t partial = 0;
                                        while (partial < recvd)
                                        {
                                            ssize_t thisOne = send(other, buf + partial, recvd - partial, 0);
                                            if (thisOne == -1)
                                            {
                                                // send error; let's just kill this client and move on
                                                close(other);
                                                FD_CLR(other, &activeFdSet);
                                                break;
                                            }
                                            partial += thisOne;
                                        }
                                    }
                                }
                            }
                            else
                            {
                                // if (recvd == -1) it's an error; if (recvd == 0) it's not
                                // but no one cares
                                close(recvSocket);
                                FD_CLR(recvSocket, &activeFdSet);
                            }
                        }
                    }
                }
            }

            return 0;
        }
    }

    fprintf(stderr, "Usage: %s <port>\n", argv[0]);
    return 1;
}
