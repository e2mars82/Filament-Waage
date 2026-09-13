#pragma once
#include <stddef.h>
#include <stdint.h>

// Antwort auf RFConfiguration (0x32), einschließlich I²C-RDY-Byte.
// Ein bloßes ACK, eine alte Scanantwort oder ein kaputter Rahmen reicht nicht.
constexpr bool validRfConfigurationResponse(const uint8_t *frame, size_t size) {
  return frame && size==10 && frame[0]==1 && frame[1]==0 && frame[2]==0 &&
         frame[3]==0xFF && frame[4]==2 && frame[5]==0xFE && frame[6]==0xD5 &&
         frame[7]==0x33 && uint8_t(frame[6]+frame[7]+frame[8])==0 && frame[9]==0;
}
