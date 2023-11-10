#include "hello.h"

int main(int argc, char *argv[]) {
  
  //do I need to do anything with the arg? not really.
  if (argc > 1) {
    say_hello_to(argv[1]);
  } else {
    say_hello_to("world");
  }
  return 0;
}
