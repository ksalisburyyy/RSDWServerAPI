// A harmless host for loader/listener tests. It contains no Unreal objects.
#include <unistd.h>
int main() { char c; return read(STDIN_FILENO, &c, 1) < 0 ? 1 : 0; }
