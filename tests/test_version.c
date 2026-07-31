#include "bk_test.h"

#include <bielik/bk_app.h>

#include <string.h>

int main(void) {
  REQUIRE(strcmp(bk_version_string(), "0.1.0") == 0);
  printf("test_version: OK\n");
  return 0;
}
