#pragma once
#include <imgui.h>
#include <cmath>
#include <cstdint>

// Small 2D glyphs drawn over the 3D viewport (screen space, anchored at a projected world point).

// A pixel-art player (Minecraft-style proportions) standing on `feet`; `unit` is the pixel size of one skin pixel.
inline void drawPlayerIcon(ImDrawList* list,ImVec2 feet,float unit,const char* label=nullptr){
    auto box=[&](float x0,float y0,float x1,float y1,ImU32 fill){list->AddRectFilled({feet.x+(x0-8)*unit,feet.y+(y0-32)*unit},{feet.x+(x1-8)*unit,feet.y+(y1-32)*unit},fill);};
    // Shadow ring on the ground so the feet position reads clearly on slopes.
    list->AddEllipseFilled({feet.x,feet.y},{9*unit,3*unit},IM_COL32(0,0,0,110));
    const ImU32 outline=IM_COL32(8,12,16,255),skin=IM_COL32(222,166,120,255),hair=IM_COL32(86,52,32,255),shirt=IM_COL32(0,170,170,255),shirtShade=IM_COL32(0,128,132,255),pants=IM_COL32(58,60,150,255),shoes=IM_COL32(112,112,118,255);
    box(3.4f,-.6f,12.6f,8.6f,outline);box(-.6f,7.4f,16.6f,20.6f,outline);box(3.4f,19.4f,12.6f,32.6f,outline);
    box(4,0,12,8,skin);box(4,0,12,2.5f,hair);box(4,2.5f,5,4,hair);box(11,2.5f,12,4,hair);
    box(5,4,7,5.5f,IM_COL32(255,255,255,255));box(9,4,11,5.5f,IM_COL32(255,255,255,255));box(6,4,7,5.5f,IM_COL32(58,70,160,255));box(9,4,10,5.5f,IM_COL32(58,70,160,255));box(7,6.4f,9,7.4f,IM_COL32(150,92,72,255));
    box(4,8,12,20,shirt);box(5.5f,8,10.5f,9,shirtShade);box(0,8,4,20,skin);box(12,8,16,20,skin);box(0,8,4,11,shirt);box(12,8,16,11,shirt);
    box(4,20,12,32,pants);box(7.6f,22,8.4f,32,outline);box(4,29,8,32,shoes);box(8,29,12,32,shoes);
    if(label&&*label){auto size=ImGui::CalcTextSize(label);ImVec2 at{feet.x-size.x*.5f,feet.y-32*unit-size.y-4};list->AddRectFilled({at.x-5,at.y-2},{at.x+size.x+5,at.y+size.y+2},IM_COL32(11,19,25,215),4);list->AddText(at,IM_COL32(236,244,240,255),label);}
}

// A map pin whose tip sits on `tip`; the head is drawn in `color` with a dark outline and an optional label.
inline void drawPinIcon(ImDrawList* list,ImVec2 tip,uint32_t rgb,float size,const char* label=nullptr,bool highlighted=false){
    ImU32 fill=IM_COL32((rgb>>16)&255,(rgb>>8)&255,rgb&255,255),outline=IM_COL32(8,12,16,255);
    ImVec2 head{tip.x,tip.y-size*1.55f};float radius=size*.62f;
    list->AddTriangleFilled({head.x-radius*.88f,head.y+radius*.5f},{head.x+radius*.88f,head.y+radius*.5f},tip,outline);
    list->AddCircleFilled(head,radius+1.6f,outline,24);list->AddTriangleFilled({head.x-radius*.78f,head.y+radius*.48f},{head.x+radius*.78f,head.y+radius*.48f},{tip.x,tip.y-1.5f},fill);
    list->AddCircleFilled(head,radius,fill,24);list->AddCircleFilled(head,radius*.36f,IM_COL32(255,255,255,235),16);
    if(highlighted)list->AddCircle(head,radius+4,IM_COL32(255,255,255,230),24,1.8f);
    list->AddCircleFilled(tip,2.2f,outline);
    if(label&&*label){auto text=ImGui::CalcTextSize(label);ImVec2 at{head.x+radius+6,head.y-text.y*.5f};list->AddRectFilled({at.x-4,at.y-2},{at.x+text.x+4,at.y+text.y+2},IM_COL32(11,19,25,215),4);list->AddText(at,IM_COL32(236,244,240,255),label);}
}
