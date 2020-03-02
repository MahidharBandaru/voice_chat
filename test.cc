#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>

using namespace std;

vector<int> pids;

void sig_handler (int signum) {
    for (auto e : pids ) {
        kill (e, SIGKILL);
    }
    exit (0);
}

int main (int argc, char *argv[]) {
    int nclients = atoi(argv[1]);
    for (int i = 0; i < nclients; ++i) {
        int pid = fork ();
        if (!pid ) {
            execl ("listener_client", "listener_client", "mahi", "127.0.0.1", "50001", "abcd", NULL);
        } else {
            pids.push_back (pid);
        }
        usleep(600000);
    }
    // for(auto &e : pids) {
    //     int returnStatus;    
    // waitpid(e, &returnStatus, WNOHANG);
    // }
    sleep(100);
    return 0;
}