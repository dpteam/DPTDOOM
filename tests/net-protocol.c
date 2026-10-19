// Data-free byte codec checks, including malformed input. GPL-2.0-or-later.
#include "dpt_protocol.h"
#include <stdio.h>
int main(void)
{
    byte bytes[8];
    ticcmd_t input = {0}, output;
    DPT_Put32(bytes, 0x89abcdefu);
    if (memcmp(bytes, "\x89\xab\xcd\xef", 4) || DPT_Get32(bytes) != 0x89abcdefu) return 1;
    input.forwardmove = -50; input.sidemove = 40; input.angleturn = -32768;
    input.buttons = 3; input.chatchar = 'x'; input.consistancy = 255;
    DPT_EncodeCommand(bytes, &input);
    if (!DPT_DecodeCommand(bytes, &output) || memcmp(&input, &output, sizeof(input))) return 2;
    bytes[0] = 51;
    if (DPT_DecodeCommand(bytes, &output)) return 3;
    bytes[0] = 0; bytes[7] = 1;
    if (DPT_DecodeCommand(bytes, &output)) return 4;
    puts("Direct coop protocol: explicit endian encoding and command bounds OK");
    return 0;
}
