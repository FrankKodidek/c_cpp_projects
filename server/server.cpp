#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <climits>
#include <cfloat>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <string>
#include <vector>
#include <array>
#include <map>
#include <thread>
#include <pthread.h>
#include <chrono>
#include <errno.h>

#include <netdb.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <arpa/inet.h>

#pragma region defines
#define PORT 6969
#define IP_ADD "127.0.0.1"
#define SA struct sockaddr

#define SERVER_MOVE "102 MOVE\a\b"
#define SERVER_TURN_LEFT "103 TURN LEFT\a\b"
#define SERVER_TURN_RIGHT "104 TURN RIGHT\a\b"
#define SERVER_PICK_UP "105 GET MESSAGE\a\b"
#define SERVER_LOGOUT "106 LOGOUT\a\b"
#define SERVER_KEY_REQUEST "107 KEY REQUEST\a\b"
#define SERVER_OK "200 OK\a\b"
#define SERVER_LOGIN_ERROR "300 LOGIN FAILED\a\b"
#define SERVER_SYNTAX_ERROR "301 SYNTAX ERROR\a\b"
#define SERVER_LOGIC_ERROR "302 LOGIC ERROR\a\b"
#define SERVER_KEY_OUT_OF_RANGE_ERROR "303 KEY OUT OF RANGE\a\b"
#pragma endregion

bool isNumber(const std::string& str)
{
    for (char const &c : str) {
        if (std::isdigit(c) == 0) return false;
    }
    return true;
}

struct SCord {
    int x = 0;
    int y = 0;

    int getQuadrant() {
        if(x > 0) return (y > 0 ? 0 : 1);
        else return (y > 0 ? 1 : 0);
    }

    std::string toString() {
        return "(" + std::to_string(x) + ", " + std::to_string(y) + ")";
    }
};

SCord operator-(const SCord & lhs, const SCord & rhs) {
    return {lhs.x - rhs.x, lhs.y - rhs.y};
}

bool operator==(const SCord & lhs, const SCord & rhs) {
    return (lhs.x == rhs.x) && (lhs.y == rhs.y);
}

struct SClient {
    int stage = 0;
    
    int hash;

    char * cache = (char *)malloc(sizeof(char)*256);
    int cache_size = 0;

    std::pair<int, int> key;
    SCord location;
    SCord old_way = {0,1};
    int turn = 0;
    bool recharging = false;

    ~SClient() {
        free(cache);
    };
};

int get_hash(char * str, int len) {
    int ret = 0;

    for(int i = 0; i < len; i++) {
        ret += str[i];
    }
    
    return ((ret * 1000) % 65536);   
}

void end_conn(int sck, const char * msg) {//, std::unique_lock & lck
    if(std::string(msg) != "") send(sck, msg, strlen(msg), 0);

    struct sockaddr_in servaddr;
    int addrlen;

    getpeername(sck, (SA *)&servaddr, (socklen_t *)&addrlen);
    printf("Host disconnected %d, ip %s , port %d\n\n\n\n\n", sck, inet_ntoa(servaddr.sin_addr) , ntohs(servaddr.sin_port));

    close(sck);
}

void send_msg(int sck, std::string str) {
    printf("sck %d msg_send: %s\n", sck, str.c_str());
    str.push_back('\a');
    str.push_back('\b');
    send(sck, str.c_str(), str.length(), 0);
}

const char * getDirection(SCord pos, SCord way) {
    if(abs(pos.x) > abs(pos.y)) {
        return way.x != 0 ? (
            SERVER_MOVE
        ) : (
            !pos.getQuadrant() ? SERVER_TURN_RIGHT : SERVER_TURN_LEFT
        ) ; 
    } else {
        return way.x != 0 ? (
            !pos.getQuadrant() ? SERVER_TURN_LEFT : SERVER_TURN_RIGHT
        ) : (
            SERVER_MOVE
        );
    }
}

const char * check_wrong_way(SCord pos, SCord way) {
    if(pos.x >= 0 && pos.y >= 0) {
        if(way.x == 1) return SERVER_TURN_RIGHT;
        else if(way.y == 1) return SERVER_TURN_LEFT;
    } else if(pos.x <= 0 && pos.y >= 0) {
        if(way.x == -1) return SERVER_TURN_LEFT;
        else if(way.y == 1) return SERVER_TURN_RIGHT;
    } else if(pos.x <= 0 && pos.y <= 0) {
        if(way.x == -1) return SERVER_TURN_RIGHT;
        else if(way.y == -1) return SERVER_TURN_LEFT;
    } else {
        if(way.x == 1) return SERVER_TURN_LEFT;
        else if(way.y == -1) return SERVER_TURN_RIGHT;
    }

    return "";
}

const char * calculate_route(SCord * old_pos, SCord new_pos, SCord * old_way, int * turn) {
    SCord new_way = new_pos - *old_pos;
    const char * ret;
    
    printf("      old ,  new\n");
    printf("pos: %s, %s\n", old_pos->toString().c_str(), new_pos.toString().c_str());
    printf("way: %s, %s\n", old_way->toString().c_str(), new_way.toString().c_str());

    if(strcmp(ret = check_wrong_way(new_pos, new_way), "")) {
        printf("wrong way\n");
    } else if(*turn) {
        ret = SERVER_MOVE;
    } else if(!new_way.x && !new_way.y) {
        if(!old_way->x && !old_way->y) {
            ret = SERVER_TURN_RIGHT;
        } else {
            printf("--------------not 0\n");
            ret = getDirection(*old_pos, *old_way);
        }
    } else {
        ret = getDirection(*old_pos, new_way);
    }

    if(!strcmp(ret, SERVER_TURN_LEFT) || !strcmp(ret, SERVER_TURN_RIGHT)) *turn = 1;
    else *turn = 0;

    printf("%s\n", ret);
    *old_way = new_way;
    *old_pos = new_pos;

    return ret;
};

int num_tokens(char * str, int len) {
    int ret = 0;
    for(int i = 0; i < len; i++) {
        if(str[i] == ' ') ret++;
    }

    return ret;
}

void manage_response(int sck, SClient * client, char ** error) {
    int stage = client->stage;

    if(stage == 0) {
        printf("sck %d cash_size: %d\n", sck, client->cache_size);
        client->hash = get_hash(client->cache, client->cache_size);
        printf("sck %d hash: %d\n", sck, client->hash);

    } else if(stage == 1) {
        printf("sck %d Hash: %d, key: %d\n", sck, client->hash, client->key.first);

        send_msg(sck, std::string(std::to_string((client->hash + client->key.first) % 65536)));

    } else if(stage == 2) {
        send(sck, SERVER_OK, strlen(SERVER_OK), 0);
        
        send(sck, SERVER_MOVE, strlen(SERVER_MOVE), 0);
        

    } else if(stage == 3) {
        std::string tmp;
        int x, y;
        float xt, yt;

        sscanf(client->cache, "%*s %f %f", &xt, &yt);
        sscanf(client->cache, "%*s %d %d", &x, &y);
        if(((xt - x) != 0) || (((yt-y)) != 0)) {
            *error = SERVER_SYNTAX_ERROR;
            return;
        }

        if(num_tokens(client->cache, client->cache_size) > 2) {
            *error = SERVER_SYNTAX_ERROR;
            return;
        }
        
        if(!x && !y) {
            printf("sck %d -------FOUND MSG-------\n", sck);
            send(sck, SERVER_PICK_UP, strlen(SERVER_PICK_UP), 0);
            client->stage = 5;
        } else {
            client->location = {x, y};    
        
            printf("\n\n\nsck %d --------------------MOVE START-------------------\n\n\n", sck);
            send(sck, SERVER_MOVE, strlen(SERVER_MOVE), 0);
        }
        
        
    } else if(stage == 4) {
        std::string tmp;
        int x,y;
        float xt, yt;
        
        sscanf(client->cache, "%*s %f %f", &xt, &yt);
        sscanf(client->cache, "%*s %d %d", &x, &y);
        
        if(((xt - x) != 0) || (((yt-y)) != 0)) {
            *error = SERVER_SYNTAX_ERROR;
            return;
        }
        if(num_tokens(client->cache, client->cache_size) > 2) {
            *error = SERVER_SYNTAX_ERROR;
            return;
        }

        if(!x && !y) {
            printf("sck %d -------FOUND MSG-------\n", sck);
            send(sck, SERVER_PICK_UP, strlen(SERVER_PICK_UP), 0);
            client->stage++;

        } else {
            const char * tmp;

            tmp = calculate_route(&client->location, {x, y}, &client->old_way, &client->turn);
            if(strlen(tmp)) send(sck, tmp, strlen(tmp), 0);
            
        }
    }

    if(client->stage < 4) {
        client->stage++;
    }

    memset(client->cache, 0, client->cache_size);
    client->cache_size = 0;
}

class Server {
    #pragma region private_sec
    private:
        std::vector<std::pair<int, int>> keys = {
            {23019, 32037},
            {32037, 29295},
            {18789, 13603},
            {16443, 29533},
            {18189, 21952}
        };

        std::vector<int> stage_b_limit = {20, 5, 7, 12, 12, 99};

        int master_socketfd, socketfd = 0, opt, addrlen;

        struct sockaddr_in servaddr;
    
        std::vector<std::thread> threads;

    #pragma endregion
    public:
        int Initialize();
        void Start_Listening();

        void test(int sck) {
            int len_read;
            char buff[256];
            SClient client;
            time_t start, end;
            
            fd_set readfds;
            
            while(true) {
                struct timeval timeout;
                if(client.recharging) {
                    timeout.tv_sec = 5;
                    timeout.tv_usec = 300000;
                } else {
                    timeout.tv_sec = 1;
                    timeout.tv_usec = 300000;
                }
                
                FD_ZERO(&readfds);
                FD_SET(sck, &readfds);

                time(&start);
                int ret = select(sck + 1, &readfds, NULL, NULL, &timeout);
                time(&end);
                
//                printf("sck %d select: %d\n", sck, ret);
//                printf("sck %d difftime %.3f\n", sck, difftime(end, start));
                
                if(!FD_ISSET(sck, &readfds)) {
                    printf("sck %d FD_ISSET fail\n", sck);
                    end_conn(sck, "");
                    return;
                }
                
                len_read = recv(sck, buff, 255, MSG_DONTWAIT);

                if(len_read <= 0) {
                    if(errno == EWOULDBLOCK || errno == EAGAIN) continue;
//                    printf("sck %d len_read: %d\n", sck, len_read);
                    end_conn(sck, "");
                    return;
                }
                int last_read = 0;
                
                while(len_read > 0) {
                    
//                    printf("\n\n---------\n\n");
//                    printf("sck %d CURR STAGE: %d\n", sck, client.stage);
                    char last_c = client.cache[client.cache_size-1];
                    
                    bool done = false;
                    
//                    printf("sck %d last_read: %d\n", sck, last_read);
//                    printf("sck %d first len_read: %d\n", sck, len_read);
//                    printf("sck %d first cache_size: %d\n", sck, client.cache_size);
                    int i;

                    // printf("sck %d last_c: %d, %c<\n", sck, last_c, last_c);
                    for(i = last_read; i < last_read + len_read; i++) {
                        printf("%d: %d, %c<\n", i, buff[i], buff[i]);
                        if(last_c == '\a' && buff[i] == '\b') {
                            printf("%d FOUND\n", sck);
                            client.cache_size -= 1;

                            client.cache[client.cache_size] = '\0';
                            done = true;

                            break;
                        } else last_c = buff[i];
                        
                        

                        client.cache[client.cache_size] = buff[i];
                        client.cache_size++;
                    }

                    len_read -= (done ? i + 1: i) - last_read;
                    last_read += i-last_read+1;

//                    printf("sck %d first len_read: %d\n", sck, len_read);
//                    printf("sck %d first cache_size: %d\n", sck, client.cache_size);

                    if(!strcmp(client.cache, "RECHARGING")) {
                        printf("recharging\n");
                        timeout.tv_sec = 5;
                        timeout.tv_usec = 300000;

                        // setsockopt(sck, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
                        client.recharging = true;
                        memset(client.cache, 0, client.cache_size);
                        client.cache_size = 0;
                        continue;
                    } else if(!strcmp(client.cache, "FULL POWER")) {
                        timeout.tv_sec = 1;
                        timeout.tv_usec = 300000;

                        // setsockopt(sck, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

                        client.recharging = false;
                        memset(client.cache, 0, client.cache_size);
                        client.cache_size = 0;
                        
                        continue;
                    } else {
                        if(client.recharging) {
                            end_conn(sck, SERVER_LOGIC_ERROR);
                            return;
                        }
                    }


                    if(client.cache_size + (done ? 2 : 1) > stage_b_limit[client.stage]) {
//                        printf("sck %d fail cache_size: %d > %d\n", sck, client.cache_size, stage_b_limit[client.stage]);
                    
                        end_conn(sck, SERVER_SYNTAX_ERROR);
                        return;
                    }
                    
                    printf("\n");

                    if(!done) {
                        printf("sck %d NOT DONE\n", sck);
                        continue;
                    }

                    if(client.stage == 0) send(socketfd, SERVER_KEY_REQUEST, strlen(SERVER_KEY_REQUEST), 0);
                    
                    if(client.stage == 1) {
                        if(!isNumber(client.cache)) {
                            printf("sck %d not number\n", sck);
                            
                            end_conn(sck, SERVER_SYNTAX_ERROR);
                            return;
                            
                        } else if(std::stoi(client.cache) < 0 || std::stoi(client.cache) > 4) {
                            printf("stoi end\n");
                            
                            end_conn(sck, SERVER_KEY_OUT_OF_RANGE_ERROR);
                            return;
                            
                        }
                        else client.key = keys[std::stoi(client.cache)];

                    } else if(client.stage == 2) {
                        if(client.cache_size > std::to_string(std::stoi(client.cache)).length()) {
                            
                            end_conn(sck, SERVER_SYNTAX_ERROR);
                            return;
                        }
                        else if(((client.key.second + client.hash) % 65536) != std::stoi(client.cache)) {
                            printf("%d != %d\n", ((client.key.second + client.hash) % 65536), std::stoi(client.cache));
                            
                            end_conn(sck, SERVER_LOGIN_ERROR);
                            return;
                            
                        }
                    } else if(client.stage == 5) {
                        printf("sck %d end\n", sck);
                        end_conn(sck, SERVER_LOGOUT);
                        return;
                        
                    }

                    char * tmp = nullptr;
                    if(sck != 0) manage_response(sck, &client, &tmp);
                    if(tmp != nullptr) {
                        end_conn(sck, SERVER_SYNTAX_ERROR);
                        return;
                        
                    }
                }
                
                time(&start);
            }
        }
};


int Server::Initialize() {
    if((master_socketfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        printf("MasterSocket creation failed.\n");
        return 1;

    } else printf("Socket creation successful.\n");

    if(setsockopt(master_socketfd, SOL_SOCKET, SO_REUSEADDR, (char *)&(opt = 1), sizeof(opt)) < 0) {
        printf("setsockopt failed.\n");
        return 1;
    }

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(IP_ADD);
    servaddr.sin_port = htons(PORT);

    if (bind(master_socketfd, (SA*) &servaddr, sizeof(servaddr)) < 0) {
        printf("MasterSocket bind failed.\n");
        return 1;
    } else printf("MasterSocket bind successful.\n");

    if(listen(master_socketfd, 4) < 0) {
        printf("Listen failed.\n");
        return 1;
    }

    printf("Server established. socket fd is %d , ip is : %s , port : %d\n" , master_socketfd , inet_ntoa(servaddr.sin_addr) , ntohs(servaddr.sin_port));

    addrlen = sizeof(servaddr);

    printf("initialization done\n");
    printf("Waiting on connections ...\n");

    return 0;

};

void Server::Start_Listening() {
    while(true) {
        usleep(400000);
        if ((socketfd = accept(master_socketfd, (SA *)&servaddr, (socklen_t*)&addrlen))<0) {
            printf("SOCKET END\n");
            break;
        }

        // setsockopt(socketfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

        printf("New connection , socket fd is %d , ip is : %s , port : %d\n" , socketfd , inet_ntoa(servaddr.sin_addr) , ntohs(servaddr.sin_port));

        printf("Socket successfully added.\n");
        

        std::thread(&Server::test, this, socketfd).detach();
    }
}


int main(int argc, char ** argv) {
    Server s;

    if(s.Initialize()) {
        printf("init failed\n");
    }

    s.Start_Listening();

    return 0;
}
