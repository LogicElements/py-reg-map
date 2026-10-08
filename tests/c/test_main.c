#include "check.h"

int check_failures;

void test_configuration(void);
void test_modbus(void);
void test_lebin(void);
void test_upgrade(void);

int main(void)
{
  test_configuration();
  test_modbus();
  test_lebin();
  test_upgrade();

  if (check_failures != 0)
  {
    printf("%d check(s) failed\n", check_failures);
    return 1;
  }
  printf("all checks passed\n");
  return 0;
}
