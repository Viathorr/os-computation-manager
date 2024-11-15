#include <fcntl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>
using namespace std;

const std::string BASE_FIFO_PATH = "/tmp/";

enum ComponentSymbol { A, B, C };

int componentA(int x);
int componentB(int x);
double componentC(int x);

struct Component {
  int ind;
  int sym;
  std::string fifoPath;
  int timeLimit;
};

struct Group {
  int ind;
  std::vector<Component> components;
};

Group createGroup(int groupId, int x) {
  Group group;
  group.ind = groupId;
  group.components = std::vector<Component>();

  std::cout << "New group " << groupId << " with x = " << x << "\n";

  return group;
}

void createComponent(char sym, Group& group, int time_limit = -1) {
  Component component;
  std::cout << "Group's components size: " << group.components.size()
            << std::endl;
  component.ind = group.components.size() + 1;
  component.sym = std::tolower(sym) == 'a'   ? A
                  : std::tolower(sym) == 'b' ? B
                  : std::tolower(sym) == 'c' ? C
                                             : -1;
  if (component.sym == -1) {
    std::cout << "Invalid component symbol" << std::endl;
    return;
  }
  component.timeLimit = time_limit;
  component.fifoPath = BASE_FIFO_PATH + "component_" +
                       std::to_string(group.ind) + "_" +
                       std::to_string(component.ind);

  group.components.push_back(component);

  std::cout << "Computational component " + std::to_string(component.sym) +
                   " with idx " + std::to_string(component.ind) +
                   " added to group " + std::to_string(group.ind)
            << std::endl;
}

void runGroup(Group group) {
  std::cout << "Running group..." << std::endl;
  return;
}

int main() {
  std::string command;
  Group group;
  while (true) {
    std::cout << "> ";
    std::getline(std::cin, command);

    std::istringstream iss(command);
    std::string cmd;
    iss >> cmd;

    if (cmd == "group") {
      int x;
      iss >> x;
      group = createGroup(0, x);  // TODO: change id
    } else if (cmd == "new") {
      int limit;
      char componentType;
      iss >> componentType; /* >> limit; */

      createComponent(componentType, group, limit);
    } else if (cmd == "run") {
      runGroup(group);
    } else if (cmd == "exit") {
      std::cout << "Exiting.\n";
      break;
    } else {
      std::cout << "Invalid command.\n";
    }
  }

  return 0;
}

// Component functions that perform simple computations with some delay >>

int componentA(int x) {
  // square of x
  sleep(5);
  return x * x;
}

int componentB(int x) {
  // add 10 to x
  sleep(6);
  return x + 10;
}

double componentC(int x) {
  // divide x by 2
  sleep(7);
  return x / 2.0;
}