#include <fcntl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
using namespace std;

const std::string BASE_FIFO_PATH = "/tmp/";

enum ComponentSymbol { A, B, C };

int componentA(int x);
int componentB(int x);
int componentC(int x);

struct Component {
  int ind;
  pid_t pid;
  int sym;
  std::string fifoPath;
  int result;
  bool resultIsAvailable = false;
  int limit = -1;
};

struct Group {
  int ind;
  std::vector<Component> components;
  int limit;
  int x;
  bool completed = false;
};

Group group;
Component* currentComponent = NULL;
std::mutex myMutex;

bool groupTimeout = false;

void showHelp() {
  std::cout << R"(
  Computation Manager - User Guide

  This tool allows you to create and manage groups of computational components. Each component performs a specific task on a given input (`x`).

  Available Commands:
  1. group <x> [limit <time>]
    - Creates a new group of components with input x.
    - Optional: Specify a group-level time limit in seconds.
    - Example:
      - group 5 (creates a group with x = 5 and no time limit).
      - group 10 limit 20 (creates a group with x = 10 and a 20-second time limit).

  Note: Every time you type in this command, the previous group and its summary will be cleared.

  2. new <type> [limit <time>]
    - Adds a new component to the current group.
    - <type> specifies the type of computation:
      - A: Computes the square of x.
      - B: Adds 10 to x.
      - C: Subtracts 5 from x.
    - Optional: Specify a component-level time limit in seconds.
    - Example:
      - new A (adds a type A component to the group).
      - new B limit 5 (adds a type B component to the group and a 5-second time limit).

  3. run
    - Executes all components in the current group.
    - Components with group time limits will be terminated if they exceed the limit.

  4. summary
    - Displays the results of computations for all components in the group.
    - Includes details for components that failed due to time limits.

  5. exit
    - Exits the program.

  6. help
    - Displays this help message.
)";
}

void createGroup(int groupId, int x, int limit) {
  group.ind = groupId;
  group.x = x;
  group.components = std::vector<Component>();
  group.limit = limit;

  std::cout << "New group " << groupId << " with x = " << x
            << (limit > 0 ? " (time limit: " + std::to_string(limit) + "s)"
                          : "")
            << std::endl;
}

void handleGroupTimeout(int signal) {
  std::lock_guard<std::mutex> lock(myMutex);
  groupTimeout = true;
}

void clearGroup() {
  group = Group();
  groupTimeout = false;
}

void createComponent(char sym, int limit) {
  Component component;

  component.ind = group.components.size() + 1;
  component.sym = std::tolower(sym) == 'a'   ? A
                  : std::tolower(sym) == 'b' ? B
                  : std::tolower(sym) == 'c' ? C
                                             : -1;
  if (component.sym == -1) {
    std::cout << "Invalid component symbol. Please try again." << std::endl;
    return;
  }

  component.fifoPath = BASE_FIFO_PATH + "component_" +
                       std::to_string(group.ind) + "_" +
                       std::to_string(component.ind);
  component.limit = limit;

  mknod(component.fifoPath.c_str(), S_IFIFO | 0666, 0);

  group.components.push_back(component);

  std::cout << "Computational component '" << sym
            << "' with idx " + std::to_string(component.ind)
            << (limit > 0 ? " (time limit: " + std::to_string(limit) + "s)"
                          : "") +
                   " added to group " + std::to_string(group.ind)
            << std::endl;
}

void monitorComponent(Component* component, std::map<int, Component*>& fds) {
  std::this_thread::sleep_for(std::chrono::seconds(component->limit));

  std::lock_guard<std::mutex> lock(myMutex);

  if (!component->resultIsAvailable && !group.completed && !groupTimeout) {
    std::cout << "Component " << component->ind
              << " is cancelled due to the timeout." << std::endl;

    kill(component->pid, SIGKILL);

    auto it = std::find_if(
        fds.begin(), fds.end(),
        [component](const auto& pair) { return pair.second == component; });

    if (it != fds.end()) {
      close(it->first);
      fds.erase(it);
    }
  }
}

void runGroup() {
  if (group.completed) {
    std::cout << "Computations are already completed." << std::endl;
    return;
  }

  if (group.components.empty()) {
    cout << "No components to run." << std::endl;
    return;
  }

  cout << "Computing..." << std::endl;

  std::map<int, Component*> fds;
  fd_set readfds;
  int maxFd = 0;

  if (group.limit > 0) {
    groupTimeout = false;
    signal(SIGALRM, handleGroupTimeout);
    alarm(group.limit);
  }

  for (auto& component : group.components) {
    pid_t pid = fork();

    if (pid == 0) {
      int fifoFd = open(component.fifoPath.c_str(), O_WRONLY);

      if (fifoFd < 0) {
        perror("Failed to open FIFO in child process");
        exit(1);
      }

      if (component.sym == A) {
        int result = componentA(group.x);

        write(fifoFd, &result, sizeof(result));
      } else if (component.sym == B) {
        int result = componentB(group.x);

        write(fifoFd, &result, sizeof(result));
      } else if (component.sym == C) {
        int result = componentC(group.x);

        write(fifoFd, &result, sizeof(result));
      } else {
        std::cout << "Error" << std::endl;
      }

      close(fifoFd);
      exit(0);
    } else {
      component.pid = pid;

      int fifoFd = open(component.fifoPath.c_str(), O_RDONLY | O_NONBLOCK);

      if (fifoFd < 0) {
        perror("Failed to open FIFO in parent process");
        exit(1);
      }

      fds[fifoFd] = &component;

      if (fifoFd > maxFd) {
        maxFd = fifoFd;
      }

      if (component.limit > 0) {
        std::thread timerThread(monitorComponent, &component, std::ref(fds));
        timerThread.detach();
      }
    }
  }

  while (!fds.empty() && !groupTimeout) {
    FD_ZERO(&readfds);

    for (const auto& [fd, _] : fds) {
      FD_SET(fd, &readfds);
    }

    struct timeval timeout = {1, 0};
    int activity = select(maxFd + 1, &readfds, NULL, NULL, &timeout);

    if (activity < 0) {
      if (errno == EINTR) {
        continue;
      }
      perror("select error");
      break;
    } else if (activity == 0) {
      continue;
    }

    std::lock_guard<std::mutex> lock(myMutex);
    for (auto it = fds.begin(); it != fds.end();) {
      int fd = it->first;
      Component* component = it->second;

      if (FD_ISSET(fd, &readfds)) {
        int result;
        ssize_t bytesRead = read(fd, &result, sizeof(result));
        if (bytesRead > 0) {
          cout << "Component " << component->ind << " finished." << std::endl;
          component->result = result;
          component->resultIsAvailable = true;
          close(fd);
          it = fds.erase(it);
        } else {
          ++it;
        }
      } else {
        ++it;
      }
    }
  }

  if (groupTimeout) {
    std::cout << "Cancelling all components due to the group timeout."
              << std::endl;
    for (auto& component : group.components) {
      kill(component.pid, SIGKILL);
      unlink(component.fifoPath.c_str());
    }

    group.completed = true;
  } else {
    for (auto& component : group.components) {
      waitpid(component.pid, NULL, 0);
      unlink(component.fifoPath.c_str());
    }
  }

  group.completed = true;
  cout << "Computation finished." << std::endl;
}

void printSummary() {
  if (group.components.empty()) {
    std::cout << "No summary is available yet." << std::endl;
    return;
  }

  std::cout << "Summary of Computations:" << std::endl;
  for (const auto& component : group.components) {
    std::cout << "Component (ind " << component.ind << ") ";

    switch (component.sym) {
      case A:
        std::cout << "[Type A]: ";
        break;
      case B:
        std::cout << "[Type B]: ";
        break;
      case C:
        std::cout << "[Type C]: ";
        break;
      default:
        std::cout << "[Unknown Type]: ";
    }

    if (component.resultIsAvailable) {
      std::cout << "Result: " << component.result << std::endl;
    } else {
      std::cout << "Result is not available (Component's computation was "
                   "cancelled due to the time limit or hasn't started yet)."
                << std::endl;
    }
  }
}

int main() {
  std::cout << "~~~~~~ Computation Manager ~~~~~"
            << "\n\nType 'help' for a list of commands.\n"
            << std::endl;
  std::string command;
  int groupId = 0;

  while (true) {
    std::cout << "> ";
    std::getline(std::cin, command);

    std::istringstream iss(command);
    std::string cmd;
    iss >> cmd;

    if (cmd == "group") {
      clearGroup();
      int x;
      int limit = -1;
      iss >> x;
      if (iss.good()) {
        std::string limitStr;
        iss >> limitStr;
        if (limitStr == "limit") {
          iss >> limit;
        } else {
          std::cerr << "Invalid command. Please try again." << std::endl;
        }
      }

      createGroup(groupId++, x, limit);
    } else if (cmd == "new") {
      int limit = -1;
      string next;
      char componentType;
      iss >> componentType;

      if (iss >> next) {
        std::transform(next.begin(), next.end(), next.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (next == "limit") {
          iss >> limit;
        } else {
          std::cout << "Invalid command. Please try again." << std::endl;
          continue;
        }
      }

      createComponent(componentType, limit);
    } else if (cmd == "run") {
      runGroup();
    } else if (cmd == "summary") {
      printSummary();
    } else if (cmd == "exit") {
      break;
    } else if (cmd == "help") {
      showHelp();
    } else {
      std::cerr << "Invalid command. Please try again." << std::endl;
    }
  }

  return 0;
}

// Component functions that perform simple computations with some delay >>

int componentA(int x) {
  // square of x
  sleep(7);
  return x * x;
}

int componentB(int x) {
  // add 10 to x
  sleep(5);
  return x + 10;
}

int componentC(int x) {
  // subtract 5 from x
  sleep(3);
  return x - 5;
}