#include <Arduino.h>

extern "C" {
extern int myargc;
extern char **myargv;
void D_DoomMain(void);
}

static char arg0[] = "embedded-doom";
static char *doomArgv[] = { arg0, nullptr };

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("embeddedDOOM badge boot");

  myargc = 1;
  myargv = doomArgv;
  D_DoomMain();
}

void loop() {
}
