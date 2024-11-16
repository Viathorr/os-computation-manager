#include <fcntl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
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
  int limit;
};

struct Group {
  int ind;
  std::vector<Component> components;
  int limit;
};

Group createGroup(int groupId, int x, int limit) {
  Group group;
  group.ind = groupId;
  group.components = std::vector<Component>();
  group.limit = limit;

  std::cout << "New group " << groupId << " with x = " << x
            << " and limit = " << (limit == -1 ? 0 : limit) << std::endl;

  return group;
}

void createComponent(char sym, Group& group, int limit) {
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
  component.limit = limit;
  component.fifoPath = BASE_FIFO_PATH + "component_" +
                       std::to_string(group.ind) + "_" +
                       std::to_string(component.ind);

  mknod(component.fifoPath.c_str(), S_IFIFO | 0666, 0);

  group.components.push_back(component);

  std::cout << "Computational component " + std::to_string(component.sym) +
                   " with idx " + std::to_string(component.ind) +
                   " and limit " + std::to_string(limit == -1 ? 0 : limit) +
                   " added to group " + std::to_string(group.ind)
            << std::endl;
}

void runGroup(Group group) {
  if (group.components.empty()) {
    cout << "No components to run.\n";
    return;
  }

  std::cout << "Computing..." << std::endl;
  return;
}

void printSummary(Group group) {}

int main() {
  std::string command;
  Group group;
  int currInd = 0;

  while (true) {
    std::cout << "> ";
    std::getline(std::cin, command);

    std::istringstream iss(command);
    std::string cmd;
    iss >> cmd;

    if (cmd == "group") {
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

      group = createGroup(currInd++, x, limit);
    } else if (cmd == "new") {
      char componentType;
      int limit;
      std::string next;
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
      } else {
        limit = -1;
      }
      createComponent(componentType, group, limit);
    } else if (cmd == "run") {
      runGroup(group);
    } else if (cmd == "summary") {
      printSummary(group);
    } else if (cmd == "exit") {
      std::cout << "Exiting...\n";
      break;
    } else {
      std::cout << "Invalid command. Please try again.\n";
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