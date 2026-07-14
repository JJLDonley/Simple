#include <chrono>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char** argv) {
  if (argc < 2) return 64;
  const std::string mode = argv[1];
  if (mode == "emit") {
    if (argc > 2) std::cout << argv[2];
    if (argc > 3) std::cerr << argv[3];
    return argc > 4 ? std::stoi(argv[4]) : 0;
  }
  if (mode == "stdin") {
    std::string input((std::istreambuf_iterator<char>(std::cin)),
                      std::istreambuf_iterator<char>());
    std::cout << input;
    return 0;
  }
  if (mode == "sleep") {
    std::this_thread::sleep_for(std::chrono::seconds(30));
    return 0;
  }
  return 65;
}
