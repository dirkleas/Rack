
/*
  see src/app/r4xh4x.cpp for deetz...

  This file is covered by the LICENSING file in the root of this project.
*/

#pragma once

namespace rack {

struct R4xH4x {
  void normalizeLayout();
  void catalog(bool);
  json_t *settingsToJson();
  void settingsFromJson(json_t *rootJ);
  void settingsSave(std::string filename);
  void settingsLoad(std::string filename);
};

}
