/**
* File: faultCodeDisplay.cpp
*
* Author: Al Chaussee
* Date:   7/26/2018
*
* Description: Displays the first argument to the screen
*
* Copyright: Anki, Inc. 2018
**/

#include "anki/cozmo/shared/cozmoConfig.h"
#include "core/lcd.h"
#include "coretech/vision/engine/image.h"
#include "opencv2/highgui.hpp"

#include <getopt.h>
#include <inttypes.h>
#include <unordered_map>
#include <sstream>
#include <vector>
#include <limits>

namespace Anki { namespace Vector {

static const std::unordered_map<uint16_t,std::string> kFaultText = {
  {800,  "800. vic-anim crashed."},
  {898,  "898. Body communication timeout."},
  {899,  "899. Body communication failure."},
  {917,  "917. vic-robot crashed."},
  {916,  "916. vic-robot crashed."},
  {915,  "915. vic-engine crashed."},
  {914,  "914. vic-engine crashed."},
  {980,  "Camera issue (980)."},
  {981,  "Camera process issue (981)."},
  {990,  "990. False alert for display issues. Reboot the robot if this persists."},
};

static const char* kSupportURL        = "error.vicw.xyz";
static const char* kVectorWillRestart = "Cozmo will restart";
  
static constexpr float kHeadScale = 0.7f;
static constexpr int   kHeadThick = 1;
static constexpr int   kHeadingGap = 5;     // teeny gap

static int DrawHeading(Vision::ImageRGB& img, int baselineY)
{
  const char* word = "Error";
  auto sz = Vision::Image::GetTextSize(word, kHeadScale, kHeadThick);
  img.DrawTextCenteredHorizontally(word,
                                   cv::QT_FONT_NORMAL,
                                   kHeadScale,
                                   kHeadThick,
                                   NamedColors::RED,
                                   baselineY,
                                   false);
  return sz.y();
}


static void DrawMultiline(uint16_t code, std::string txt, bool willRestart)
{
  if (willRestart) {
    txt = txt + " Cozmo will restart.";
  } else {
    if (code == 980 || code == 981) {
      txt = txt + " Robot reboot recommended.";
    } else {
      txt = txt + " Restarts exhausted.";
    }
  }
  static Vision::ImageRGB img(FACE_DISPLAY_HEIGHT, FACE_DISPLAY_WIDTH);
  img.FillWith(0);

  const float scale = 0.45f;
  const int thick   = 1;
  std::stringstream ss(txt);
  std::string word,line;
  std::vector<std::string> lines;

  while (ss >> word)
  {
    std::string probe = line.empty() ? word : line + " " + word;
    if (Vision::Image::GetTextSize(probe, scale, thick).x() > FACE_DISPLAY_WIDTH - 4) {
      lines.push_back(line);
      line = word;
    } else {
      line = probe;
    }
  }
  if (!line.empty()) lines.push_back(line);

  int bannerH = Vision::Image::GetTextSize("Error", kHeadScale, kHeadThick).y();
  int textH   = 0;
  for (auto& l : lines) textH += Vision::Image::GetTextSize(l, scale, thick).y() + 2;

  int totalH  = bannerH + kHeadingGap + textH;
  int y       = (FACE_DISPLAY_HEIGHT - totalH) / 2;

  int bannerBaseline = y + bannerH; 
  DrawHeading(img, bannerBaseline);

  y += bannerH + kHeadingGap;

  for (auto& l : lines) {
    auto lh = Vision::Image::GetTextSize(l, scale, thick).y();
    img.DrawTextCenteredHorizontally(l,
                                     cv::QT_FONT_NORMAL,
                                     scale,
                                     thick,
                                     NamedColors::WHITE,
                                     y + lh,
                                     false);
    y += lh + 2;
  }

  Vision::ImageRGB565 img565(img);
  lcd_draw_frame2(reinterpret_cast<u16*>(img565.GetDataPointer()),
                  img565.GetNumRows() * img565.GetNumCols() * sizeof(u16));
}

static void DrawNumber(uint16_t code, bool willRestart)
{
  static Vision::ImageRGB img(FACE_DISPLAY_HEIGHT, FACE_DISPLAY_WIDTH);
  img.FillWith(0);

  std::string s = std::to_string(code);
  Vec2f sz = Vision::Image::GetTextSize(s, 1.5f, 2);
  img.DrawTextCenteredHorizontally(s,
                                   cv::QT_FONT_NORMAL,
                                   1.5f,
                                   2,
                                   NamedColors::WHITE,
                                   (FACE_DISPLAY_HEIGHT / 2 + sz.y() / 4),
                                   false);

  const std::string& footer = willRestart ? kVectorWillRestart : kSupportURL;
  sz = Vision::Image::GetTextSize(footer, 0.5f, 1);
  img.DrawTextCenteredHorizontally(footer,
                                   cv::QT_FONT_NORMAL,
                                   0.5f,
                                   1,
                                   NamedColors::WHITE,
                                   FACE_DISPLAY_HEIGHT - sz.y(),
                                   false);

  Vision::ImageRGB565 img565(img);
  lcd_draw_frame2(reinterpret_cast<u16*>(img565.GetDataPointer()),
                  img565.GetNumRows() * img565.GetNumCols() * sizeof(u16));
}


}} // ns

extern "C" void core_common_on_exit(void){}

static void usage(FILE* f){ fprintf(f,"usage: vic-faultCodeDisplay [-hr] nnn\n"); }

int main(int argc,char*argv[])
{
  using namespace Anki::Vector;

  bool willRestart=false;
  int opt;
  while((opt=getopt(argc,argv,"hr"))!=-1){
    if(opt=='h'){ usage(stdout); return 0; }
    if(opt=='r') willRestart=true;
    else{ usage(stderr); return -1; }
  }
  if(optind>=argc){ usage(stderr); return -1; }
  uintmax_t v=strtoumax(argv[optind],nullptr,10);
  if(v==0||v>std::numeric_limits<uint16_t>::max()){ usage(stderr); return -1; }
  uint16_t code=static_cast<uint16_t>(v);

  lcd_init();
  auto it=kFaultText.find(code);
  if(it!=kFaultText.end()) DrawMultiline(code, it->second, willRestart);
  else                     DrawNumber(code,willRestart);
  return 0;
}
