/**
 * File: lightsConfig.h
 *
 * Author: Raj-jyot
 *
 * Created: 21/09/2025
 *
 * Description:
 *   Choose backpack lights thing
 *
 * Copyright: Raj-jyot, 2025
 **/

#ifndef ANKI_VECTOR_LIGHTS_CONFIG_H
#define ANKI_VECTOR_LIGHTS_CONFIG_H

#include <sys/stat.h>

namespace Anki {
namespace Vector {

  inline bool& _wireoslights() {
    static bool initialized = false;
    static bool value = false;
    
    if (!initialized) {
      struct stat buffer;
      value = (stat("/data/data/wirelights", &buffer) == 0);
      initialized = true;
    }
    
    return value;
  }


  inline bool& _userlights() {
    static bool initialized = false;
    static bool value = false;
    
    if (!initialized) {
      struct stat buffer;
      value = (stat("/data/data/customBackpackLights/off.json", &buffer) == 0);
      initialized = true;
    }
    
    return value;
  }

}
}

#endif // ANKI_VECTOR_LIGHTS_CONFIG_H
