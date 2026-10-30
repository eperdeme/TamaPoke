#pragma once
// Test hooks for the emulator's box file store (boxfs_host.cpp).
#include <cstdint>
#include <map>
#include <string>
#include <vector>

std::map<std::string, std::vector<uint8_t>> &bxfsFiles();
// true: the partition will not mount at all (a board without one).
bool &bxfsHostBroken();
// Writes allowed before the store refuses every write; -1 = unlimited.
int &bxfsHostWriteBudget();
// The next write lands only its first N bytes and then the board "dies":
// every later write is refused until the test resets the budget.
int &bxfsHostTearAt();
// Forget the mount, as a reboot does.
void bxfsHostUnmount();
