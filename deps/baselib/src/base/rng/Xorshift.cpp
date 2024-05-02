#include "Xorshift.hpp"
#include "SystemRng.hpp"

void base::Xorshift::reseed(base::SeedFromSystemRng) {
  reseed(SystemRng64{}.gen());
}