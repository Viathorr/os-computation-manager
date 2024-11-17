#include <fcntl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
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

bool groupTimeout = false;
void handleGroupTimeout(int signal) { groupTimeout = true; }

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

  2. new <type>
    - Adds a new component to the current group.
    - <type> specifies the type of computation:
      - A: Computes the square of x.
      - B: Adds 10 to x.
      - C: Subtracts 5 from x.
    - Example:
      - new A (adds a type A component to the group).
      - new B (adds a type B component to the group).

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
            << " and limit = " << (limit == -1 ? 0 : limit) << std::endl;
}

void clearGroup() {
  group = Group();
  groupTimeout = false;
}

void createComponent(char sym) {
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

  mknod(component.fifoPath.c_str(), S_IFIFO | 0666, 0);

  group.components.push_back(component);

  std::cout << "Computational component '" << sym
            << "' with idx " + std::to_string(component.ind) +
                   " added to group " + std::to_string(group.ind)
            << std::endl;
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

  cout << "Running components..." << std::endl;

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
    }
  }

  while (!fds.empty() && !groupTimeout) {
    FD_ZERO(&readfds);

    for (const auto& [fd, _] : fds) {
      FD_SET(fd, &readfds);
    }

    int activity = select(maxFd + 1, &readfds, NULL, NULL, NULL);

    if (activity < 0) {
      perror("select error");
      break;
    }

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
    std::cout << "Group timeout. Cancelling all components." << std::endl;
    for (auto& component : group.components) {
      kill(component.pid, SIGKILL);
    }
  }

  for (auto& component : group.components) {
    waitpid(component.pid, NULL, 0);
    unlink(component.fifoPath.c_str());
  }

  cout << "Computation finished." << std::endl;
  group.completed = true;
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
  std::string command;
  int currInd = 0;

  while (true) {
    std::cout << "> ";
    std::getline(std::cin, command);

    std::istringstream iss(command);
    std::string cmd;
    iss >> cmd;

    if (cmd == "group") {
      clearGroup();
      std::string next;
      int x;
      int limit;
      iss >> x;
      if (iss >> next) {
        std::transform(next.begin(), next.end(), next.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (next == "limit") {
          iss >> limit;
        } else {
          std::cout << "Invalid command. Please try again." << std::endl;
        }
      } else {
        limit = -1;
      }

      createGroup(currInd++, x, limit);
    } else if (cmd == "new") {
      char componentType;
      int limit;
      std::string next;
      iss >> componentType;

      createComponent(componentType);
    } else if (cmd == "run") {
      runGroup();
    } else if (cmd == "summary") {
      printSummary();
    } else if (cmd == "exit") {
      std::cout << "Exiting..." << std::endl;
      break;
    } else if (cmd == "help") {
      showHelp();
    } else {
      std::cout << "Invalid command. Please try again." << std::endl;
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